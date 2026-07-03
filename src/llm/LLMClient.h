#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <QJsonObject>

class QTimer;

// Represents a single tool call requested by the LLM
struct ToolCall {
    QString callId;
    QString name;
    QJsonObject arguments;
};

class LLMClient : public QObject
{
    Q_OBJECT
public:
    explicit LLMClient(QObject *parent = nullptr);

    void sendMessage(const QString &provider,
                     const QString &modelName,
                     const QString &apiKey,
                     const QString &baseUrl,
                     const QJsonArray &messages,
                     double temperature,
                     int maxTokens,
                     const QJsonArray &tools = QJsonArray());  // optional tools

    void cancelCurrent();

signals:
    void responseReady(const QString &content, int tokensIn, int tokensOut);
    void toolCallsReady(const QList<ToolCall> &calls, int tokensIn);  // LLM wants to call tools
    void errorOccurred(const QString &errorMessage);

private:
    void sendOpenAI(const QString &modelName, const QString &apiKey,
                    const QString &baseUrl, const QJsonArray &messages,
                    double temperature, int maxTokens, const QJsonArray &tools);
    void sendOllama(const QString &modelName, const QString &baseUrl,
                    const QJsonArray &messages);
    void armTimeout(int ms);

    QNetworkAccessManager *m_nam;
    QNetworkReply         *m_reply        = nullptr;
    QTimer                *m_timeoutTimer = nullptr;

    static int estimateTokens(const QString &text) { return qMax(1, text.length() / 4); }
};
