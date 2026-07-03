#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include "models/AgentListModel.h"
#include "database/AgentDao.h"

class AgentManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(AgentListModel* agentListModel READ agentListModel CONSTANT)

public:
    explicit AgentManager(QObject *parent = nullptr);

    AgentListModel *agentListModel() { return m_model; }

    Q_INVOKABLE bool createAgent(const QString &name, const QString &provider,
                                  const QString &modelName, const QString &apiKey,
                                  const QString &baseUrl, const QString &systemPrompt,
                                  double temperature, int maxTokens,
                                  const QString &templateName);
    Q_INVOKABLE int  createAgentEx(const QString &name, const QString &provider,
                                   const QString &modelName, const QString &apiKey,
                                   const QString &baseUrl, const QString &systemPrompt,
                                   double temperature, int maxTokens,
                                   const QString &templateName);
    Q_INVOKABLE bool updateAgent(int id, const QString &name, const QString &provider,
                                  const QString &modelName, const QString &apiKey,
                                  const QString &baseUrl, const QString &systemPrompt,
                                  double temperature, int maxTokens);
    Q_INVOKABLE bool deleteAgent(int id);
    Q_INVOKABLE QVariantMap getAgent(int id);
    Q_INVOKABLE void searchAgents(const QString &keyword);
    Q_INVOKABLE void refreshAll();

signals:
    void agentsChanged();
    void agentError(const QString &message);

private:
    AgentListModel *m_model;
    AgentDao        m_dao;
};
