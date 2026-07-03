#include "AgentListModel.h"

AgentListModel::AgentListModel(QObject *parent) : QAbstractListModel(parent) {}

int AgentListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_agents.size();
}

QVariant AgentListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_agents.size()) return {};
    const AgentData &a = m_agents[index.row()];
    switch (role) {
    case IdRole:          return a.id;
    case NameRole:        return a.name;
    case ProviderRole:    return a.provider;
    case ModelNameRole:   return a.modelName;
    case SystemPromptRole:return a.systemPrompt;
    case TemperatureRole: return a.temperature;
    case MaxTokensRole:   return a.maxTokens;
    default: return {};
    }
}

QHash<int, QByteArray> AgentListModel::roleNames() const
{
    return {
        {IdRole,          "agentId"},
        {NameRole,        "agentName"},
        {ProviderRole,    "agentProvider"},
        {ModelNameRole,   "agentModelName"},
        {SystemPromptRole,"agentSystemPrompt"},
        {TemperatureRole, "agentTemperature"},
        {MaxTokensRole,   "agentMaxTokens"},
    };
}

void AgentListModel::setAgents(const QVector<AgentData> &agents)
{
    beginResetModel();
    m_agents = agents;
    endResetModel();
}

void AgentListModel::addAgent(const AgentData &agent)
{
    beginInsertRows({}, m_agents.size(), m_agents.size());
    m_agents.append(agent);
    endInsertRows();
}

void AgentListModel::updateAgent(const AgentData &agent)
{
    for (int i = 0; i < m_agents.size(); ++i) {
        if (m_agents[i].id == agent.id) {
            m_agents[i] = agent;
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx);
            return;
        }
    }
}

void AgentListModel::removeAgent(int id)
{
    for (int i = 0; i < m_agents.size(); ++i) {
        if (m_agents[i].id == id) {
            beginRemoveRows({}, i, i);
            m_agents.removeAt(i);
            endRemoveRows();
            return;
        }
    }
}
