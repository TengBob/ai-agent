#include "LLMClient.h"
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QDebug>

LLMClient::LLMClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

void LLMClient::cancelCurrent()
{
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
        m_timeoutTimer->deleteLater();
        m_timeoutTimer = nullptr;
    }
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}

static QString buildOpenAIUrl(const QString &baseUrl)
{
    QString base = baseUrl.isEmpty() ? "https://api.openai.com" : baseUrl;
    while (base.endsWith('/')) base.chop(1);
    if (base.endsWith("/v1")) base.chop(3);
    return base + "/v1/chat/completions";
}

void LLMClient::armTimeout(int ms)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        qDebug() << "[LLMClient] timed out";
        emit errorOccurred("请求超时，请检查：1) 网络是否正常  2) Base URL 是否正确  3) API Key 是否有效");
        cancelCurrent();
    });
    m_timeoutTimer->start(ms);
}

void LLMClient::sendMessage(const QString &provider,
                             const QString &modelName,
                             const QString &apiKey,
                             const QString &baseUrl,
                             const QJsonArray &messages,
                             double temperature,
                             int maxTokens,
                             const QJsonArray &tools)
{
    cancelCurrent();
    if (provider == "ollama")
        sendOllama(modelName, baseUrl.isEmpty() ? "http://localhost:11434" : baseUrl, messages);
    else
        sendOpenAI(modelName, apiKey, baseUrl, messages, temperature, maxTokens, tools);
}

void LLMClient::sendOpenAI(const QString &modelName, const QString &apiKey,
                             const QString &baseUrl, const QJsonArray &messages,
                             double temperature, int maxTokens, const QJsonArray &tools)
{
    const QString url = buildOpenAIUrl(baseUrl);
    qDebug() << "[LLMClient] POST" << url << "model:" << modelName
             << "tools:" << tools.size();

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    QJsonObject body;
    body["model"]       = modelName;
    body["messages"]    = messages;
    body["temperature"] = temperature;
    body["max_tokens"]  = maxTokens;
    if (!tools.isEmpty()) {
        body["tools"]       = tools;
        body["tool_choice"] = "auto";
    }

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    armTimeout(60000);  // 60s — tool-using models can be slower

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        if (m_timeoutTimer) { m_timeoutTimer->stop(); m_timeoutTimer->deleteLater(); m_timeoutTimer = nullptr; }

        if (m_reply->error() != QNetworkReply::NoError) {
            QString err = m_reply->errorString();
            const QByteArray raw = m_reply->readAll();
            if (!raw.isEmpty()) {
                const QJsonObject obj = QJsonDocument::fromJson(raw).object();
                if (obj.contains("error"))
                    err = obj["error"].toObject()["message"].toString();
            }
            qDebug() << "[LLMClient] error:" << err;
            emit errorOccurred(err);
            m_reply->deleteLater(); m_reply = nullptr;
            return;
        }

        const QByteArray raw = m_reply->readAll();
        m_reply->deleteLater(); m_reply = nullptr;
        qDebug() << "[LLMClient] response:" << raw.left(300);

        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        if (obj.contains("error")) {
            emit errorOccurred(obj["error"].toObject()["message"].toString());
            return;
        }

        const QJsonObject usage   = obj["usage"].toObject();
        const int tokIn           = usage["prompt_tokens"].toInt(0);
        const int tokOut          = usage["completion_tokens"].toInt(0);
        const QJsonObject choice  = obj["choices"].toArray().first().toObject();
        const QJsonObject message = choice["message"].toObject();
        const QString finishReason = choice["finish_reason"].toString();

        // Check if model wants to call tools
        const bool hasToolCalls = message.contains("tool_calls") && message["tool_calls"].toArray().size() > 0;
        const QString content  = message["content"].toString();

        qDebug() << "[LLMClient] finish_reason:" << finishReason
                 << "hasToolCalls:" << hasToolCalls
                 << "content_len:" << content.length()
                 << "content_preview:" << content.left(100);

        if (hasToolCalls) {
            QList<ToolCall> calls;
            const QJsonArray tcs = message["tool_calls"].toArray();
            for (const QJsonValue &v : tcs) {
                const QJsonObject tc = v.toObject();
                ToolCall c;
                c.callId    = tc["id"].toString();
                c.name      = tc["function"].toObject()["name"].toString();
                const QString argsStr = tc["function"].toObject()["arguments"].toString();
                c.arguments = QJsonDocument::fromJson(argsStr.toUtf8()).object();
                calls.append(c);
            }
            emit toolCallsReady(calls, tokIn);
            return;
        }

        if (!content.isEmpty()) {
            emit responseReady(content, tokIn, tokOut);
        } else {
            // Empty content with no tool calls — likely a stop response
            emit responseReady("抱歉，我没能找到合适的答案。", tokIn, tokOut);
        }
    });
}

void LLMClient::sendOllama(const QString &modelName, const QString &baseUrl,
                            const QJsonArray &messages)
{
    QString base = baseUrl;
    while (base.endsWith('/')) base.chop(1);
    const QString url = base + "/api/chat";
    qDebug() << "[LLMClient] POST" << url << "model:" << modelName;

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["model"]    = modelName;
    body["messages"] = messages;
    body["stream"]   = false;

    m_reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    armTimeout(60000);

    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        if (m_timeoutTimer) { m_timeoutTimer->stop(); m_timeoutTimer->deleteLater(); m_timeoutTimer = nullptr; }

        if (m_reply->error() != QNetworkReply::NoError) {
            qDebug() << "[LLMClient] error:" << m_reply->errorString();
            emit errorOccurred(m_reply->errorString());
            m_reply->deleteLater(); m_reply = nullptr;
            return;
        }
        const QByteArray raw = m_reply->readAll();
        m_reply->deleteLater(); m_reply = nullptr;

        const QJsonObject obj = QJsonDocument::fromJson(raw).object();
        const QString content = obj["message"].toObject()["content"].toString();
        int tokIn  = obj["prompt_eval_count"].toInt(estimateTokens(content));
        int tokOut = obj["eval_count"].toInt(estimateTokens(content));
        emit responseReady(content, tokIn, tokOut);
    });
}
