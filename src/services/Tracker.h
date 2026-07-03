#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include "database/AgentDao.h"

class Tracker : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int totalTokensIn  READ totalTokensIn  NOTIFY statsChanged)
    Q_PROPERTY(int totalTokensOut READ totalTokensOut NOTIFY statsChanged)
    Q_PROPERTY(int totalCalls     READ totalCalls     NOTIFY statsChanged)

public:
    explicit Tracker(QObject *parent = nullptr);

    int totalTokensIn()  const { return m_tokIn; }
    int totalTokensOut() const { return m_tokOut; }
    int totalCalls()     const { return m_calls; }

    Q_INVOKABLE void record(int agentId, int convId, const QString &model,
                             int tokIn, int tokOut, int latencyMs,
                             const QString &error = {});
    Q_INVOKABLE QVariantList getLogs(int limit = 100);
    Q_INVOKABLE void clearLogs();
    Q_INVOKABLE QVariantMap getStats();

signals:
    void statsChanged();

private:
    AgentDao m_dao;
    int m_tokIn  = 0;
    int m_tokOut = 0;
    int m_calls  = 0;
};
