#pragma once
#include <QObject>
#include <QVariantList>
#include <QJsonArray>
#include "models/AgentData.h"

class LLMClient;
class ToolExecutor;

class ComparisonEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)

public:
    explicit ComparisonEngine(QObject *parent = nullptr);
    ~ComparisonEngine();

    bool isRunning() const { return m_running; }

    Q_INVOKABLE void startComparison(const QVariantList &agentIds,
                                     const QString &question,
                                     int summaryAgentId = -1);
    Q_INVOKABLE void cancel();

signals:
    void isRunningChanged();
    void agentResult(int agentId, const QString &agentName, const QString &modelName,
                     const QString &content, const QString &error, int tokens);
    void summaryReady(const QString &summary, const QString &error);
    void finished();

private:
    void checkAllDone();
    void runSummary();
    void continueSlot(int slotIdx);

    struct SlotState {
        int        agentId   = -1;
        QString    agentName;
        QString    modelName;
        QString    content;
        QString    error;
        int        tokens    = 0;
        bool       done      = false;
        LLMClient *client    = nullptr;
        QJsonArray context;   // full message history for tool loop
        AgentData  agent;
    };

    QList<SlotState> m_slots;
    int              m_pendingCount   = 0;
    int              m_summaryAgentId = -1;
    QString          m_question;
    bool             m_running        = false;
    LLMClient       *m_summaryClient  = nullptr;
    ToolExecutor    *m_executor       = nullptr;
};
