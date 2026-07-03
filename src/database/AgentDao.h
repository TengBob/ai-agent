#pragma once
#include "models/AgentData.h"
#include "models/MessageData.h"
#include "models/TemplateData.h"
#include <QVector>
#include <QString>

class AgentDao
{
public:
    // ---- Agent ----
    int  insertAgent(const AgentData &a);
    bool updateAgent(const AgentData &a);
    bool deleteAgent(int id);
    AgentData getAgent(int id);
    QVector<AgentData> getAllAgents();
    QVector<AgentData> searchAgents(const QString &keyword);

    // ---- Conversation ----
    int  insertConversation(int agentId, const QString &title = "新对话");
    int  insertGroupConversation(int groupId, const QString &title = "新对话");
    bool deleteConversation(int convId);
    QVector<QPair<int,QString>> getConversations(int agentId); // id, title

    // ---- Message ----
    int  insertMessage(const MessageData &m);
    QVector<MessageData> getMessages(int conversationId);
    bool deleteMessagesByConversation(int conversationId);

    // ---- Log ----
    bool insertLog(int agentId, int convId, const QString &eventType,
                   const QString &modelName, int tokIn, int tokOut,
                   int latencyMs, const QString &errMsg = {});
    QVector<QVariantMap> getLogs(int limit = 100);

    // ---- Template ----
    bool insertTemplate(const TemplateData &t);
    QVector<TemplateData> getAllTemplates();
    TemplateData getTemplateByName(const QString &name);
    int templateCount();

    // ---- Group ----
    int  insertGroup(const QString &name);
    bool updateGroup(int groupId, const QString &name);
    bool deleteGroup(int groupId);
    QVariantList getGroups(); // [{id, name, agentCount, createdAt}]
    QVariantList getGroupAgents(int groupId); // [{id, name, provider, modelName}]

    // ---- Group-Agent relation ----
    bool addAgentToGroup(int groupId, int agentId);
    bool removeAgentFromGroup(int groupId, int agentId);
    bool setGroupAgents(int groupId, const QVector<int> &agentIds); // replace all
};
