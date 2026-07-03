#include "AgentManager.h"

AgentManager::AgentManager(QObject *parent)
    : QObject(parent)
    , m_model(new AgentListModel(this))
{
    refreshAll();
}

void AgentManager::refreshAll()
{
    m_model->setAgents(m_dao.getAllAgents());
}

bool AgentManager::createAgent(const QString &name, const QString &provider,
                                 const QString &modelName, const QString &apiKey,
                                 const QString &baseUrl, const QString &systemPrompt,
                                 double temperature, int maxTokens,
                                 const QString &templateName)
{
    if (name.trimmed().isEmpty()) { emit agentError("名称不能为空"); return false; }
    AgentData a;
    a.name         = name.trimmed();
    a.provider     = provider;
    a.modelName    = modelName;
    a.apiKey       = apiKey;
    a.baseUrl      = baseUrl;
    a.systemPrompt = systemPrompt;
    a.temperature  = temperature;
    a.maxTokens    = maxTokens;
    a.templateName = templateName;

    const int id = m_dao.insertAgent(a);
    if (id < 0) { emit agentError("创建失败"); return false; }
    a.id = id;
    m_model->addAgent(a);
    emit agentsChanged();
    return true;
}

int AgentManager::createAgentEx(const QString &name, const QString &provider,
                                  const QString &modelName, const QString &apiKey,
                                  const QString &baseUrl, const QString &systemPrompt,
                                  double temperature, int maxTokens,
                                  const QString &templateName)
{
    if (name.trimmed().isEmpty()) { emit agentError("名称不能为空"); return -1; }
    AgentData a;
    a.name         = name.trimmed();
    a.provider     = provider;
    a.modelName    = modelName;
    a.apiKey       = apiKey;
    a.baseUrl      = baseUrl;
    a.systemPrompt = systemPrompt;
    a.temperature  = temperature;
    a.maxTokens    = maxTokens;
    a.templateName = templateName;

    const int id = m_dao.insertAgent(a);
    if (id < 0) { emit agentError("创建失败"); return -1; }
    a.id = id;
    m_model->addAgent(a);
    emit agentsChanged();
    return id;
}

bool AgentManager::updateAgent(int id, const QString &name, const QString &provider,
                                 const QString &modelName, const QString &apiKey,
                                 const QString &baseUrl, const QString &systemPrompt,
                                 double temperature, int maxTokens)
{
    AgentData a = m_dao.getAgent(id);
    a.name         = name.trimmed();
    a.provider     = provider;
    a.modelName    = modelName;
    a.apiKey       = apiKey;
    a.baseUrl      = baseUrl;
    a.systemPrompt = systemPrompt;
    a.temperature  = temperature;
    a.maxTokens    = maxTokens;

    if (!m_dao.updateAgent(a)) { emit agentError("更新失败"); return false; }
    m_model->updateAgent(a);
    emit agentsChanged();
    return true;
}

bool AgentManager::deleteAgent(int id)
{
    if (!m_dao.deleteAgent(id)) { emit agentError("删除失败"); return false; }
    m_model->removeAgent(id);
    emit agentsChanged();
    return true;
}

QVariantMap AgentManager::getAgent(int id)
{
    const AgentData a = m_dao.getAgent(id);
    return {
        {"id",           a.id},
        {"name",         a.name},
        {"provider",     a.provider},
        {"modelName",    a.modelName},
        {"apiKey",       a.apiKey},
        {"baseUrl",      a.baseUrl},
        {"systemPrompt", a.systemPrompt},
        {"temperature",  a.temperature},
        {"maxTokens",    a.maxTokens},
        {"templateName", a.templateName},
    };
}

void AgentManager::searchAgents(const QString &keyword)
{
    if (keyword.trimmed().isEmpty())
        m_model->setAgents(m_dao.getAllAgents());
    else
        m_model->setAgents(m_dao.searchAgents(keyword));
}
