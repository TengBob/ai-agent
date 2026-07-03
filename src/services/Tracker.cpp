#include "Tracker.h"
#include <QSqlQuery>
#include "database/DatabaseManager.h"

Tracker::Tracker(QObject *parent) : QObject(parent) {}

void Tracker::record(int agentId, int convId, const QString &model,
                      int tokIn, int tokOut, int latencyMs,
                      const QString &error)
{
    m_dao.insertLog(agentId, convId,
                    error.isEmpty() ? "chat_complete" : "error",
                    model, tokIn, tokOut, latencyMs, error);
    if (error.isEmpty()) {
        m_tokIn  += tokIn;
        m_tokOut += tokOut;
        ++m_calls;
        emit statsChanged();
    }
}

QVariantList Tracker::getLogs(int limit)
{
    const auto logs = m_dao.getLogs(limit);
    QVariantList result;
    for (const auto &l : logs) result.append(l);
    return result;
}

void Tracker::clearLogs()
{
    QSqlQuery q("DELETE FROM logs", DatabaseManager::instance().db());
    m_tokIn = m_tokOut = m_calls = 0;
    emit statsChanged();
}

QVariantMap Tracker::getStats()
{
    return {
        {"totalTokensIn",  m_tokIn},
        {"totalTokensOut", m_tokOut},
        {"totalCalls",     m_calls},
    };
}
