#include "ComparisonEngine.h"
#include "llm/LLMClient.h"
#include "tools/ToolRegistry.h"
#include "tools/ToolExecutor.h"
#include "database/AgentDao.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>

ComparisonEngine::ComparisonEngine(QObject *parent)
    : QObject(parent)
    , m_executor(new ToolExecutor(this))
{}

ComparisonEngine::~ComparisonEngine() {}

void ComparisonEngine::startComparison(const QVariantList &agentIds,
                                       const QString &question,
                                       int summaryAgentId)
{
    if (m_running) return;

    m_slots.clear();
    m_question       = question;
    m_summaryAgentId = summaryAgentId;
    m_running        = true;
    m_pendingCount   = 0;
    emit isRunningChanged();

    AgentDao dao;

    for (const QVariant &v : agentIds) {
        const int id = v.toInt();
        const AgentData a = dao.getAgent(id);
        if (a.id <= 0) continue;

        SlotState slot;
        slot.agentId   = id;
        slot.agentName = a.name;
        slot.modelName = a.modelName;
        slot.agent     = a;
        slot.client    = new LLMClient(this);

        // Build initial context
        if (!a.systemPrompt.isEmpty())
            slot.context.append(QJsonObject{{"role","system"},{"content", a.systemPrompt}});
        slot.context.append(QJsonObject{{"role","user"},{"content", question}});

        const int slotIdx = m_slots.size();
        m_slots.append(slot);
        ++m_pendingCount;

        continueSlot(slotIdx);
    }

    if (m_pendingCount == 0) {
        m_running = false;
        emit isRunningChanged();
        emit finished();
    }
}

void ComparisonEngine::continueSlot(int slotIdx)
{
    SlotState &s = m_slots[slotIdx];
    LLMClient *client = s.client;
    const AgentData &a = s.agent;

    // Disconnect previous connections to avoid duplicate signals
    client->disconnect();

    connect(client, &LLMClient::responseReady, this,
            [this, slotIdx](const QString &content, int /*in*/, int tokOut) {
        auto &s = m_slots[slotIdx];
        s.content = content;
        s.tokens  = tokOut;
        s.done    = true;
        emit agentResult(s.agentId, s.agentName, s.modelName, content, QString(), tokOut);
        --m_pendingCount;
        checkAllDone();
    });

    connect(client, &LLMClient::toolCallsReady, this,
            [this, slotIdx](const QList<ToolCall> &calls, int /*tokIn*/) {
        auto &s = m_slots[slotIdx];

        // Append assistant tool_calls turn to context
        QJsonArray toolCallsJson;
        for (const ToolCall &c : calls) {
            toolCallsJson.append(QJsonObject{
                {"id",   c.callId},
                {"type", "function"},
                {"function", QJsonObject{
                    {"name",      c.name},
                    {"arguments", QString::fromUtf8(
                        QJsonDocument(c.arguments).toJson(QJsonDocument::Compact))}
                }}
            });
        }
        s.context.append(QJsonObject{
            {"role",       "assistant"},
            {"content",    QJsonValue::Null},
            {"tool_calls", toolCallsJson}
        });

        // Execute tools and append results
        for (const ToolCall &c : calls) {
            const QString result = m_executor->execute(c.name, c.arguments);
            s.context.append(QJsonObject{
                {"role",         "tool"},
                {"tool_call_id", c.callId},
                {"content",      result}
            });
        }

        // Continue loop
        continueSlot(slotIdx);
    });

    connect(client, &LLMClient::errorOccurred, this,
            [this, slotIdx](const QString &err) {
        auto &s = m_slots[slotIdx];
        s.error = err;
        s.done  = true;
        emit agentResult(s.agentId, s.agentName, s.modelName, QString(), err, 0);
        --m_pendingCount;
        checkAllDone();
    });

    client->sendMessage(a.provider, a.modelName, a.apiKey, a.baseUrl,
                        s.context, a.temperature, a.maxTokens,
                        ToolRegistry::instance().toOpenAITools());
}

void ComparisonEngine::cancel()
{
    for (auto &s : m_slots)
        if (s.client) s.client->cancelCurrent();
    if (m_summaryClient) m_summaryClient->cancelCurrent();
    m_running = false;
    emit isRunningChanged();
}

void ComparisonEngine::checkAllDone()
{
    if (m_pendingCount > 0) return;

    if (m_summaryAgentId > 0) {
        runSummary();
    } else {
        m_running = false;
        emit isRunningChanged();
        emit summaryReady(QString(), QString());
        emit finished();
    }
}

void ComparisonEngine::runSummary()
{
    AgentDao dao;
    const AgentData sa = dao.getAgent(m_summaryAgentId);
    if (sa.id <= 0) {
        m_running = false;
        emit isRunningChanged();
        emit summaryReady(QString(), "总结 Agent 不存在");
        emit finished();
        return;
    }

    QString prompt = "以下是不同 AI 模型对同一问题的回答，请做出综合总结和对比分析。\n\n";
    prompt += "【问题】\n" + m_question + "\n\n";
    for (const auto &s : m_slots) {
        prompt += "【" + s.agentName + " / " + s.modelName + "】\n";
        prompt += s.error.isEmpty() ? s.content : ("（错误：" + s.error + "）");
        prompt += "\n\n";
    }
    prompt += "请综合以上回答，给出总结和对比分析。";

    QJsonArray msgs;
    if (!sa.systemPrompt.isEmpty())
        msgs.append(QJsonObject{{"role","system"},{"content", sa.systemPrompt}});
    msgs.append(QJsonObject{{"role","user"},{"content", prompt}});

    m_summaryClient = new LLMClient(this);

    connect(m_summaryClient, &LLMClient::responseReady, this,
            [this](const QString &content, int, int) {
        m_running = false;
        emit isRunningChanged();
        emit summaryReady(content, QString());
        emit finished();
    });

    connect(m_summaryClient, &LLMClient::errorOccurred, this,
            [this](const QString &err) {
        m_running = false;
        emit isRunningChanged();
        emit summaryReady(QString(), err);
        emit finished();
    });

    // Summary agent also has tool access
    m_summaryClient->sendMessage(sa.provider, sa.modelName, sa.apiKey, sa.baseUrl,
                                 msgs, sa.temperature, sa.maxTokens,
                                 ToolRegistry::instance().toOpenAITools());
}
