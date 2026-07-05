#pragma once
#include <QObject>
#include <QJsonArray>
#include <QVariantList>
#include <atomic>
#include "models/AgentData.h"
#include "models/MessageData.h"
#include "database/AgentDao.h"
#include "database/NoteDao.h"
#include "llm/LLMClient.h"
#include "tools/ToolExecutor.h"

class PageTextCache;

class ChatEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(int totalTokensIn  READ totalTokensIn  NOTIFY tokensUpdated)
    Q_PROPERTY(int totalTokensOut READ totalTokensOut NOTIFY tokensUpdated)

public:
    explicit ChatEngine(QObject *parent = nullptr);

    bool isLoading()      const { return m_loading; }
    int  totalTokensIn()  const { return m_totalIn; }
    int  totalTokensOut() const { return m_totalOut; }

    Q_INVOKABLE void startConversation(int agentId);
    Q_INVOKABLE void loadConversation(int conversationId);
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void sendMessage(const QString &content);
    Q_INVOKABLE QVariantList getConversations(int agentId);
    Q_INVOKABLE QVariantList currentMessages();
    Q_INVOKABLE void cancelRequest();

    // Tool settings (exposed to QML via context property)
    Q_INVOKABLE void setTavilyKey(const QString &key);
    Q_INVOKABLE void setWorkDir(const QString &dir);

    // Current file context for note-taking and RAG
    Q_INVOKABLE void setContextFile(const QString &filePath);
    Q_INVOKABLE void loadFileContent(const QString &filePath);
    Q_INVOKABLE void loadFilePage(const QString &filePath, int page);
    Q_INVOKABLE void setCurrentFileContext(const QString &filePath, int page);

    // Prefetch page text in background after scroll stops.
    Q_INVOKABLE void prefetchPage(const QString &filePath, int page);

    static void setPdfConverter(class PdfConverter *converter);

signals:
    // role: "user" | "assistant" | "tool_call" | "tool_result"
    void messageReceived(const QString &role, const QString &content, const QString &createdAt);
    void toolLogReceived(const QString &toolName, const QString &argsJson,
                         const QString &resultPreview, const QString &createdAt);
    void loadingChanged();
    void tokensUpdated();
    void errorOccurred(const QString &message);
    void conversationChanged();

private:
    void setLoading(bool v);
    QJsonArray buildContext();          // messages → QJsonArray for LLM
    void runAgentLoop(const QString &userContent);
    void onLLMResponse(const QString &content, int tokIn, int tokOut);
    void onToolCallsReady(const QList<ToolCall> &calls, int tokIn);
    void executeToolsAsync(const QList<ToolCall> &calls);
    void saveAndEmit(const QString &role, const QString &content,
                     int tokIn = 0, int tokOut = 0);

    QString buildContextualPrompt(const QString &filePath, int page,
                                  const QString &pageText,
                                  const QString &userContent);
    void doSendMessage(const QString &finalContent);

private slots:
    void handleToolResults(QStringList results);

private:
    LLMClient    *m_client;
    ToolExecutor *m_executor;
    AgentDao      m_dao;
    NoteDao       m_noteDao;
    AgentData     m_agent;
    static class PdfConverter *s_pdfConverter;
    PageTextCache *m_pageCache = nullptr;
    QString       m_contextFile;
    QString       m_contextContent;
    int           m_contextPage = 0;
    QString       m_pendingMessage;  // stored when async fallback is needed
    int           m_convId   = -1;
    bool          m_loading  = false;
    int           m_totalIn  = 0;
    int           m_totalOut = 0;
    int           m_loopIter = 0;
    std::atomic<bool> m_toolCancelled{false};
    static constexpr int MAX_ITERATIONS = 10;

    QJsonArray    m_context;
    QVector<MessageData> m_messages;
};
