#pragma once
#include <QObject>
#include <QJsonArray>
#include <QVariantList>
#include "models/AgentData.h"
#include "models/MessageData.h"
#include "database/AgentDao.h"
#include "llm/LLMClient.h"
#include "tools/ToolExecutor.h"

struct AgentTask {
    int     agentId;
    QString prompt;
};

class GroupChatEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)

public:
    explicit GroupChatEngine(QObject *parent = nullptr);

    bool isLoading() const { return m_loading; }

    // Group management
    Q_INVOKABLE int  createGroup(const QString &name);
    Q_INVOKABLE bool updateGroup(int groupId, const QString &name);
    Q_INVOKABLE bool deleteGroup(int groupId);
    Q_INVOKABLE QVariantList getGroups();
    Q_INVOKABLE QVariantList getGroupAgents(int groupId);
    Q_INVOKABLE QVariantList getAllAgents();
    Q_INVOKABLE bool setGroupAgents(int groupId, const QVariantList &agentIds);

    // Chat
    Q_INVOKABLE void startGroupConversation(int groupId);
    Q_INVOKABLE void sendGroupMessage(const QString &content);
    Q_INVOKABLE QVariantList groupMessages();
    Q_INVOKABLE void newGroupConversation();
    Q_INVOKABLE void cancelRequest();

    // Tool settings
    Q_INVOKABLE void setTavilyKey(const QString &key);
    Q_INVOKABLE void setWorkDir(const QString &dir);

signals:
    void messageReceived(const QString &role, const QString &content, const QString &createdAt);
    void loadingChanged();
    void errorOccurred(const QString &message);
    void groupsChanged();

private:
    void setLoading(bool v);
    QVector<AgentTask> parseMentions(const QString &content);
    void processNextTask();
    void onLLMResponse(const QString &content, int tokIn, int tokOut);
    void onToolCallsReady(const QList<ToolCall> &calls, int tokIn);
    void saveAndEmit(const QString &role, const QString &content, int tokIn = 0, int tokOut = 0);

    AgentDao       m_dao;
    LLMClient     *m_client;
    ToolExecutor  *m_executor;
    int            m_groupId    = -1;
    int            m_convId     = -1;
    bool           m_loading    = false;

    // Task execution
    QVector<AgentTask> m_taskQueue;
    int            m_currentTargetId = -1;
    QJsonArray     m_currentContext;  // per-agent context during ReAct loop
    int            m_loopIter  = 0;
    static constexpr int MAX_ITERATIONS = 10;

    QJsonArray     m_sharedContext;   // shared conversation history
    QHash<int, AgentData> m_groupAgents;

    QVector<MessageData> m_messages;
};
