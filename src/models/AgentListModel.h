#pragma once
#include <QAbstractListModel>
#include <QVector>
#include "AgentData.h"

class AgentListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole       = Qt::UserRole + 1,
        NameRole,
        ProviderRole,
        ModelNameRole,
        SystemPromptRole,
        TemperatureRole,
        MaxTokensRole
    };
    Q_ENUM(Roles)

    explicit AgentListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAgents(const QVector<AgentData> &agents);
    void addAgent(const AgentData &agent);
    void updateAgent(const AgentData &agent);
    void removeAgent(int id);

private:
    QVector<AgentData> m_agents;
};
