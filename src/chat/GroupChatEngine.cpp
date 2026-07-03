#include "GroupChatEngine.h"
#include "tools/ToolRegistry.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>
#include <QRegularExpression>

GroupChatEngine::GroupChatEngine(QObject *parent)
    : QObject(parent)
    , m_client(new LLMClient(this))
    , m_executor(new ToolExecutor(this))
{
    connect(m_client, &LLMClient::responseReady, this,
            [this](const QString &content, int tokIn, int tokOut) {
        onLLMResponse(content, tokIn, tokOut);
    });

    connect(m_client, &LLMClient::toolCallsReady, this,
            [this](const QList<ToolCall> calls, int tokIn) {
        onToolCallsReady(calls, tokIn);
    });

    connect(m_client, &LLMClient::errorOccurred, this,
            [this](const QString &err) {
        setLoading(false);
        m_dao.insertLog(m_currentTargetId, m_convId, "error",
                        m_groupAgents.value(m_currentTargetId).modelName,
                        0, 0, 0, err);
        const AgentData agent = m_groupAgents.value(m_currentTargetId);
        if (agent.id > 0) {
            const QString role = "agent:" + QString::number(agent.id) + ":" + agent.name;
            saveAndEmit(role, QStringLiteral(u"抱歉，我没能找到合适的答案。"));
        }
        emit errorOccurred(err);
        processNextTask();
    });

    // Tool progress in chat
    connect(m_executor, &ToolExecutor::toolStarted, this,
            [this](const QString &toolName, const QString &argsJson) {
        const QString msg = "🔧 调用工具：" + toolName + "\n参数：" + argsJson;
        emit messageReceived("tool_call", msg,
                             QDateTime::currentDateTime().toString("hh:mm:ss"));
    });

    connect(m_executor, &ToolExecutor::toolFinished, this,
            [this](const QString &toolName, const QString &result) {
        const QString preview = result.length() > 500
            ? result.left(500) + "\n...[省略]" : result;
        emit messageReceived("tool_result",
                             "✅ " + toolName + " 结果：\n" + preview,
                             QDateTime::currentDateTime().toString("hh:mm:ss"));
    });
}

void GroupChatEngine::setLoading(bool v)
{
    if (m_loading == v) return;
    m_loading = v;
    emit loadingChanged();
}

void GroupChatEngine::setTavilyKey(const QString &key) { m_executor->setTavilyKey(key); }
void GroupChatEngine::setWorkDir(const QString &dir)   { m_executor->setWorkDir(dir); }

// ── Group management ────────────────────────────────────────────────

int GroupChatEngine::createGroup(const QString &name)
{
    int id = m_dao.insertGroup(name);
    if (id > 0) emit groupsChanged();
    return id;
}

bool GroupChatEngine::updateGroup(int groupId, const QString &name)
{
    bool ok = m_dao.updateGroup(groupId, name);
    if (ok) emit groupsChanged();
    return ok;
}

bool GroupChatEngine::deleteGroup(int groupId)
{
    bool ok = m_dao.deleteGroup(groupId);
    if (ok) emit groupsChanged();
    return ok;
}

QVariantList GroupChatEngine::getGroups()
{
    return m_dao.getGroups();
}

QVariantList GroupChatEngine::getGroupAgents(int groupId)
{
    return m_dao.getGroupAgents(groupId);
}

QVariantList GroupChatEngine::getAllAgents()
{
    const auto agents = m_dao.getAllAgents();
    QVariantList result;
    for (const AgentData &a : agents) {
        result.append(QVariantMap{
            {"id",        a.id},
            {"name",      a.name},
            {"provider",  a.provider},
            {"modelName", a.modelName},
        });
    }
    return result;
}

bool GroupChatEngine::setGroupAgents(int groupId, const QVariantList &agentIds)
{
    QVector<int> ids;
    for (const QVariant &v : agentIds) ids.append(v.toInt());
    bool ok = m_dao.setGroupAgents(groupId, ids);
    if (ok) emit groupsChanged();
    return ok;
}

// ── Conversation ────────────────────────────────────────────────────

void GroupChatEngine::startGroupConversation(int groupId)
{
    m_groupId = groupId;
    m_messages.clear();
    m_sharedContext = QJsonArray();
    m_groupAgents.clear();

    const QVariantList agents = m_dao.getGroupAgents(groupId);
    for (const QVariant &v : agents) {
        const QVariantMap m = v.toMap();
        const int aid = m["id"].toInt();
        m_groupAgents[aid] = m_dao.getAgent(aid);
    }

    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare("SELECT id FROM conversations WHERE group_id=:g ORDER BY id DESC LIMIT 1");
    q.bindValue(":g", groupId);
    if (!q.exec()) qDebug() << "[GroupChat] query conv failed:" << q.lastError().text();
    if (q.next()) {
        m_convId = q.value(0).toInt();
        m_messages = m_dao.getMessages(m_convId);
        qDebug() << "[GroupChat] startGroupConversation: loaded conv" << m_convId
                 << "with" << m_messages.size() << "messages for group" << groupId;
        for (const MessageData &m : m_messages) {
            if (m.role == "tool_call" || m.role == "tool_result") continue;
            if (m.role.startsWith("agent:")) {
                const QString name = m.role.section(':', 2, 2);
                m_sharedContext.append(QJsonObject{{"role", "assistant"}, {"content", name + ": " + m.content}});
            } else {
                m_sharedContext.append(QJsonObject{{"role", m.role}, {"content", m.content}});
            }
        }
    } else {
        m_convId = m_dao.insertGroupConversation(groupId, "群组对话 " +
            QDateTime::currentDateTime().toString("MM-dd hh:mm"));
        qDebug() << "[GroupChat] startGroupConversation: created new conv" << m_convId
                 << "for group" << groupId;
    }
}

void GroupChatEngine::newGroupConversation()
{
    if (m_groupId < 0) return;
    m_convId = m_dao.insertGroupConversation(m_groupId, "群组对话 " +
        QDateTime::currentDateTime().toString("MM-dd hh:mm"));
    qDebug() << "[GroupChat] newGroupConversation: created conv" << m_convId
             << "for group" << m_groupId;
    m_messages.clear();
    m_sharedContext = QJsonArray();
}

QVariantList GroupChatEngine::groupMessages()
{
    QVariantList result;
    for (const MessageData &m : m_messages) {
        if (m.role == "system") continue;
        result.append(QVariantMap{
            {"role",    m.role},
            {"content", m.content},
            {"timestamp", m.createdAt},
        });
    }
    return result;
}

void GroupChatEngine::cancelRequest()
{
    m_client->cancelCurrent();
    m_taskQueue.clear();
    setLoading(false);
}

// ── Parse @mentions ─────────────────────────────────────────────────

QVector<AgentTask> GroupChatEngine::parseMentions(const QString &content)
{
    QVector<AgentTask> tasks;

    QHash<QString, int> nameToId;
    for (auto it = m_groupAgents.constBegin(); it != m_groupAgents.constEnd(); ++it)
        nameToId[it->name.toLower()] = it.key();

    static const QRegularExpression re(R"RE(@(?:"([^"]+)"|(\S+)))RE");
    QRegularExpressionMatchIterator it = re.globalMatch(content);

    int lastEnd = 0;
    QString lastMatchedName;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        const QString matchedName = match.captured(1).isEmpty()
            ? match.captured(2) : match.captured(1);
        const int matchStart = match.capturedStart();

        if (!lastMatchedName.isEmpty()) {
            const QString taskText = content.mid(lastEnd, matchStart - lastEnd).trimmed();
            if (!taskText.isEmpty()) {
                const int aid = nameToId.value(lastMatchedName.toLower(), -1);
                if (aid > 0) tasks.append({aid, taskText});
            }
        }
        lastMatchedName = matchedName;
        lastEnd = match.capturedEnd();
    }

    if (!lastMatchedName.isEmpty()) {
        const QString taskText = content.mid(lastEnd).trimmed();
        if (!taskText.isEmpty()) {
            const int aid = nameToId.value(lastMatchedName.toLower(), -1);
            if (aid > 0) tasks.append({aid, taskText});
        }
    }

    return tasks;
}

// ── Send message ────────────────────────────────────────────────────

void GroupChatEngine::sendGroupMessage(const QString &content)
{
    if (content.trimmed().isEmpty()) return;

    if (m_loading) {
        qDebug() << "[GroupChat] WARNING: sendGroupMessage called while loading=true, force resetting...";
        m_client->cancelCurrent();
        m_taskQueue.clear();
        setLoading(false);
    }

    if (m_convId < 0) { emit errorOccurred("未选择群组对话"); return; }

    saveAndEmit("user", content.trimmed());
    m_taskQueue = parseMentions(content.trimmed());

    qDebug() << "[GroupChat] sendGroupMessage: parsed" << m_taskQueue.size() << "tasks";
    for (int i = 0; i < m_taskQueue.size(); i++) {
        qDebug() << "[GroupChat]   task" << i << ": agentId=" << m_taskQueue[i].agentId
                 << "prompt=" << m_taskQueue[i].prompt.left(50);
    }

    if (m_taskQueue.isEmpty()) {
        emit errorOccurred("请使用 @Agent名 来指定任务");
        return;
    }

    m_sharedContext.append(QJsonObject{{"role", "user"}, {"content", content.trimmed()}});
    setLoading(true);
    processNextTask();
}

void GroupChatEngine::processNextTask()
{
    if (m_taskQueue.isEmpty()) {
        qDebug() << "[GroupChat] processNextTask: queue empty, all tasks done";
        setLoading(false);
        return;
    }

    const AgentTask task = m_taskQueue.takeFirst();
    m_currentTargetId = task.agentId;
    const AgentData agent = m_groupAgents.value(m_currentTargetId);

    qDebug() << "[GroupChat] processNextTask: agent" << m_currentTargetId
             << "name=" << agent.name << "valid=" << (agent.id > 0)
             << "remaining=" << m_taskQueue.size();

    if (agent.id <= 0) {
        emit errorOccurred("Agent 不存在: " + QString::number(m_currentTargetId));
        processNextTask();
        return;
    }

    // Build per-agent context: system prompt + task hint + shared history
    m_currentContext = QJsonArray();
    if (!agent.systemPrompt.isEmpty())
        m_currentContext.append(QJsonObject{{"role", "system"}, {"content", agent.systemPrompt}});

    const QString groupHint =
        "你正在一个群组中协作。你的任务是：" + task.prompt +
        "\n\n以下是群组的对话历史（包含其他成员的发言，请参考）：";
    m_currentContext.append(QJsonObject{{"role", "system"}, {"content", groupHint}});

    for (const QJsonValue &v : m_sharedContext)
        m_currentContext.append(v);

    m_loopIter = 0;

    qDebug() << "[GroupChat] processNextTask: sending LLM request model=" << agent.modelName
             << "context_msgs=" << m_currentContext.size()
             << "tools=" << ToolRegistry::instance().toOpenAITools().size();

    m_client->sendMessage(agent.provider, agent.modelName,
                          agent.apiKey, agent.baseUrl,
                          m_currentContext, agent.temperature, agent.maxTokens,
                          ToolRegistry::instance().toOpenAITools());
}

// ── LLM response handlers (ReAct loop) ─────────────────────────────

void GroupChatEngine::onLLMResponse(const QString &content, int tokIn, int tokOut)
{
    qDebug() << "[GroupChat] onLLMResponse agent:" << m_currentTargetId
             << "content_len:" << content.length() << "tokIn:" << tokIn << "tokOut:" << tokOut;

    const AgentData agent = m_groupAgents.value(m_currentTargetId);
    const QString role = "agent:" + QString::number(agent.id) + ":" + agent.name;

    saveAndEmit(role, content, tokIn, tokOut);
    m_sharedContext.append(QJsonObject{{"role", "assistant"}, {"content", agent.name + ": " + content}});

    m_dao.insertLog(m_currentTargetId, m_convId, "group_chat",
                    agent.modelName, tokIn, tokOut, 0);

    processNextTask();
}

void GroupChatEngine::onToolCallsReady(const QList<ToolCall> &calls, int tokIn)
{
    m_loopIter++;
    qDebug() << "[GroupChat] onToolCallsReady agent:" << m_currentTargetId
             << "iter:" << m_loopIter << "calls:" << calls.size();

    if (m_loopIter > MAX_ITERATIONS) {
        const AgentData agent = m_groupAgents.value(m_currentTargetId);
        if (agent.id > 0) {
            const QString role = "agent:" + QString::number(agent.id) + ":" + agent.name;
            saveAndEmit(role, QStringLiteral(u"抱歉，工具调用达到最大迭代次数，未能完成分析。"));
        }
        emit errorOccurred("Agent 达到最大迭代次数限制");
        processNextTask();
        return;
    }

    // Append assistant tool_call turn to current context
    QJsonArray toolCallsJson;
    for (const ToolCall &c : calls) {
        QJsonObject funcObj;
        funcObj["name"]      = c.name;
        funcObj["arguments"] = QString::fromUtf8(QJsonDocument(c.arguments).toJson(QJsonDocument::Compact));
        QJsonObject tcObj;
        tcObj["id"]       = c.callId;
        tcObj["type"]     = "function";
        tcObj["function"] = funcObj;
        toolCallsJson.append(tcObj);
    }
    m_currentContext.append(QJsonObject{
        {"role",       "assistant"},
        {"content",    QJsonValue::Null},
        {"tool_calls", toolCallsJson}
    });

    // Execute each tool and append results
    for (const ToolCall &c : calls) {
        const QString result = m_executor->execute(c.name, c.arguments);
        m_currentContext.append(QJsonObject{
            {"role",         "tool"},
            {"tool_call_id", c.callId},
            {"content",      result}
        });
    }

    // Re-send to LLM with tools
    const AgentData agent = m_groupAgents.value(m_currentTargetId);
    qDebug() << "[GroupChat] onToolCallsReady: re-sending to LLM after tool execution, context_msgs:"
             << m_currentContext.size() << "iter:" << m_loopIter;
    m_client->sendMessage(agent.provider, agent.modelName,
                          agent.apiKey, agent.baseUrl,
                          m_currentContext, agent.temperature, agent.maxTokens,
                          ToolRegistry::instance().toOpenAITools());
}

// ── Helpers ─────────────────────────────────────────────────────────

void GroupChatEngine::saveAndEmit(const QString &role, const QString &content,
                                   int tokIn, int tokOut)
{
    if (m_convId < 0) {
        qDebug() << "[GroupChat] WARNING: saveAndEmit called with invalid convId! role:" << role;
    }

    MessageData msg;
    msg.conversationId = m_convId;
    msg.role      = role;
    msg.content   = content;
    msg.tokensIn  = tokIn;
    msg.tokensOut = tokOut;
    msg.modelName = m_groupAgents.value(m_currentTargetId).modelName;
    msg.createdAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    const int msgId = m_dao.insertMessage(msg);
    if (msgId < 0)
        qDebug() << "[GroupChat] WARNING: insertMessage failed! conv:" << m_convId << "role:" << role;
    m_messages.append(msg);
    qDebug() << "[GroupChat] saveAndEmit: saved role:" << role
             << "conv:" << m_convId << "msgId:" << msgId << "msgCount:" << m_messages.size();
    emit messageReceived(role, content, msg.createdAt);
}
