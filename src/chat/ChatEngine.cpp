#include "ChatEngine.h"
#include "tools/ToolRegistry.h"
#include "models/NoteData.h"
#include "services/PdfConverter.h"
#include "services/PageTextCache.h"
#include <QDateTime>
#include <QJsonObject>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <QFutureWatcher>

PdfConverter *ChatEngine::s_pdfConverter = nullptr;

void ChatEngine::setPdfConverter(PdfConverter *converter)
{
    s_pdfConverter = converter;
}

ChatEngine::ChatEngine(QObject *parent)
    : QObject(parent)
    , m_client(new LLMClient(this))
    , m_executor(new ToolExecutor(this))
    , m_pageCache(new PageTextCache(this))
{
    // LLM returned plain text response
    connect(m_client, &LLMClient::responseReady, this,
            [this](const QString &content, int tokIn, int tokOut) {
        onLLMResponse(content, tokIn, tokOut);
    });

    // LLM wants to call tools
    connect(m_client, &LLMClient::toolCallsReady, this,
            [this](const QList<ToolCall> &calls, int tokIn) {
        onToolCallsReady(calls, tokIn);
    });

    connect(m_client, &LLMClient::errorOccurred, this,
            [this](const QString &err) {
        setLoading(false);
        m_dao.insertLog(m_agent.id, m_convId, "error", m_agent.modelName, 0, 0, 0, err);
        saveAndEmit("assistant", QStringLiteral(u"抱歉，我没能找到合适的答案。"));
        emit errorOccurred(err);
    });

    // Tool progress goes to a separate log signal, not the chat dialog
    connect(m_executor, &ToolExecutor::toolStarted, this,
            [this](const QString &toolName, const QString &argsJson) {
        emit toolLogReceived(toolName, argsJson, "",
                             QDateTime::currentDateTime().toString("hh:mm:ss"));
    });

    connect(m_executor, &ToolExecutor::toolFinished, this,
            [this](const QString &toolName, const QString &result) {
        const QString preview = result.length() > 500
            ? result.left(500) + "\n...[省略]" : result;
        emit toolLogReceived(toolName, "", preview,
                             QDateTime::currentDateTime().toString("hh:mm:ss"));
    });
}

void ChatEngine::setLoading(bool v)
{
    if (m_loading == v) return;
    m_loading = v;
    emit loadingChanged();
}

void ChatEngine::setTavilyKey(const QString &key)  { m_executor->setTavilyKey(key); }
void ChatEngine::setWorkDir(const QString &dir)     { m_executor->setWorkDir(dir); }
void ChatEngine::setContextFile(const QString &filePath) { m_contextFile = filePath; }

void ChatEngine::setCurrentFileContext(const QString &filePath, int page)
{
    m_contextFile = filePath;
    m_contextPage = page;
}

void ChatEngine::prefetchPage(const QString &filePath, int page)
{
    if (filePath.isEmpty() || page < 1) return;
    if (!s_pdfConverter) return;

    // Already cached — nothing to do.
    if (m_pageCache->get(filePath, page).has_value()) return;

    QtConcurrent::run([this, filePath, page]() {
        QString text = s_pdfConverter->readHtmlPage(filePath, page);
        if (text.startsWith("Error:") || text.startsWith("无法") || text.startsWith("未找到")) {
            qWarning() << "prefetchPage:" << text;
            return;
        }
        if (!text.isEmpty()) {
            m_pageCache->put(filePath, page, text);
            qDebug() << "[Chat] prefetched page" << page << "len" << text.length()
                     << "for" << filePath;
        }
    });
}

static QString fastHtmlToText(QString html)
{
    // Quick-and-dirty HTML to text: remove script/style blocks and tags, collapse whitespace.
    // This is faster than full regex on large files because it scans linearly.
    QString out;
    out.reserve(html.length() / 2);
    int i = 0;
    const int n = html.length();
    while (i < n) {
        if (html.at(i) == '<') {
            int tagEnd = html.indexOf('>', i);
            if (tagEnd < 0) break;
            const QString tagLower = QStringView(html).mid(i + 1, tagEnd - i - 1).toString().toLower();
            if (tagLower.startsWith("script") || tagLower.startsWith("style")) {
                const QString close = (tagLower.at(0) == 's' && tagLower.at(1) == 'c')
                    ? "</script>" : "</style>";
                int closeEnd = html.indexOf(close, tagEnd + 1, Qt::CaseInsensitive);
                i = (closeEnd < 0) ? tagEnd + 1 : closeEnd + close.length();
                out.append(' ');
            } else {
                i = tagEnd + 1;
                out.append(' ');
            }
        } else {
            out.append(html.at(i));
            ++i;
        }
    }
    out.replace("&nbsp;", " ");
    out.replace("&lt;", "<");
    out.replace("&gt;", ">");
    out.replace("&amp;", "&");
    out.replace("&quot;", "\"");
    out.replace(QRegularExpression("\\s+"), " ");
    return out.trimmed();
}

void ChatEngine::loadFilePage(const QString &filePath, int page)
{
    m_contextFile = filePath;
    m_contextContent.clear();
    if (page < 1 || filePath.isEmpty()) return;

    if (!s_pdfConverter) {
        qWarning() << "loadFilePage: PdfConverter not set";
        return;
    }

    // Read the specific page in a background thread so the UI stays smooth.
    QtConcurrent::run([this, filePath, page]() {
        QString text = s_pdfConverter->readHtmlPage(filePath, page);
        if (text.startsWith("Error:") || text.startsWith("无法") || text.startsWith("未找到")) {
            qWarning() << "loadFilePage:" << text;
            return;
        }
        constexpr int MAX_LEN = 4000;
        if (text.length() > MAX_LEN) {
            text = text.left(MAX_LEN) + "\n...[内容已截断]";
        }
        QMetaObject::invokeMethod(this, [this, text]() {
            m_contextContent = text;
        }, Qt::QueuedConnection);
        qDebug() << "loadFilePage: cached page" << page << "len" << text.length();
    });
}

void ChatEngine::loadFileContent(const QString &filePath)
{
    m_contextFile = filePath;
    m_contextContent.clear();

    QFileInfo fi(filePath);
    if (fi.size() > 50 * 1024 * 1024) {
        qWarning() << "loadFileContent: file too large (>50MB)" << filePath;
        m_contextContent = "[文件过大，无法加载内容]";
        return;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "loadFileContent: cannot open" << filePath;
        return;
    }

    // Only read the first 100KB — enough for context, avoids UI blocking on huge files
    constexpr qint64 READ_LIMIT = 100 * 1024;
    QByteArray data = f.read(READ_LIMIT);
    f.close();

    QString html = QString::fromUtf8(data);
    QString text = fastHtmlToText(html);

    constexpr int MAX_LEN = 4000;
    if (text.length() > MAX_LEN) {
        text = text.left(MAX_LEN) + "\n...[内容过长，已截断]";
    }
    m_contextContent = text;
    qDebug() << "loadFileContent: cached" << text.length() << "chars from" << filePath;
}

// ── conversation management ──────────────────────────────────────────────────

void ChatEngine::startConversation(int agentId)
{
    m_agent    = m_dao.getAgent(agentId);
    m_totalIn  = 0;
    m_totalOut = 0;
    m_messages.clear();
    m_context  = QJsonArray();

    const auto convs = m_dao.getConversations(agentId);
    if (!convs.isEmpty()) {
        m_convId   = convs.first().first;
        m_messages = m_dao.getMessages(m_convId);
        for (const MessageData &m : m_messages) {
            m_totalIn  += m.tokensIn;
            m_totalOut += m.tokensOut;
        }
        // Rebuild context from persisted messages
        for (const MessageData &m : m_messages) {
            if (m.role == "tool_call" || m.role == "tool_result") continue;
            m_context.append(QJsonObject{{"role", m.role}, {"content", m.content}});
        }
    } else {
        newConversation();
    }

    emit tokensUpdated();
    emit conversationChanged();
}

void ChatEngine::newConversation()
{
    m_convId   = m_dao.insertConversation(m_agent.id,
        "对话 " + QDateTime::currentDateTime().toString("MM-dd hh:mm"));
    m_messages.clear();
    m_context  = QJsonArray();

    if (!m_agent.systemPrompt.isEmpty()) {
        MessageData sys;
        sys.conversationId = m_convId;
        sys.role    = "system";
        sys.content = m_agent.systemPrompt;
        m_dao.insertMessage(sys);
        m_messages.append(sys);
        m_context.append(QJsonObject{{"role","system"},{"content",m_agent.systemPrompt}});
    }
    emit conversationChanged();
}

void ChatEngine::loadConversation(int conversationId)
{
    m_convId   = conversationId;
    m_messages = m_dao.getMessages(conversationId);
    m_totalIn  = 0;
    m_totalOut = 0;
    m_context  = QJsonArray();

    for (const MessageData &m : m_messages) {
        m_totalIn  += m.tokensIn;
        m_totalOut += m.tokensOut;
        if (m.role == "tool_call" || m.role == "tool_result") continue;
        m_context.append(QJsonObject{{"role", m.role}, {"content", m.content}});
    }
    emit tokensUpdated();
    emit conversationChanged();
}

// ── send & agent loop ────────────────────────────────────────────────────────

void ChatEngine::sendMessage(const QString &content)
{
    if (m_loading || content.trimmed().isEmpty()) return;
    if (m_convId < 0) { emit errorOccurred("未选择对话"); return; }

    const QString userContent = content.trimmed();
    QString finalContent = userContent;

    // ── Contextual prompt: only look up cache on the main thread ──
    if (!m_contextFile.isEmpty() && m_contextPage > 0) {
        auto cached = m_pageCache->get(m_contextFile, m_contextPage);

        if (cached.has_value()) {
            // ═══ Fast path: prefetch already finished ═══
            finalContent = buildContextualPrompt(m_contextFile, m_contextPage,
                                                 cached.value(), finalContent);
            saveAndEmit("user", userContent);
            doSendMessage(finalContent);
        } else {
            // ═══ Fallback: rare case where user sends before prefetch completes ═══
            m_pendingMessage = finalContent;
            saveAndEmit("user", userContent);
            setLoading(true);

            QtConcurrent::run([this, path = m_contextFile, page = m_contextPage]() {
                QString text;
                if (s_pdfConverter) {
                    text = s_pdfConverter->readHtmlPage(path, page);
                    if (!text.isEmpty() &&
                        !text.startsWith("Error:") &&
                        !text.startsWith("无法") &&
                        !text.startsWith("未找到")) {
                        m_pageCache->put(path, page, text);
                    }
                }
                QMetaObject::invokeMethod(this, [this, path, page, text]() {
                    QString final = buildContextualPrompt(path, page, text,
                                                          m_pendingMessage);
                    m_pendingMessage.clear();
                    doSendMessage(final);
                }, Qt::QueuedConnection);
            });
        }
        return;
    }

    // No file context — send directly.
    saveAndEmit("user", userContent);
    doSendMessage(finalContent);
}

QString ChatEngine::buildContextualPrompt(const QString &filePath, int page,
                                          const QString &pageText,
                                          const QString &userContent)
{
    if (pageText.isEmpty() || filePath.isEmpty() || page < 1)
        return userContent;

    const QString fileName = filePath.split('/').last().split('\\').last();
    constexpr int MAX_LEN = 4000;
    QString clipped = pageText;
    if (clipped.length() > MAX_LEN) {
        clipped = clipped.left(MAX_LEN) + "\n...[内容已截断]";
    }

    return "【以下是我正在浏览的文件《" + fileName + "》第 "
           + QString::number(page) + " 页的内容片段，已经直接提供给你。"
           "请直接基于以下内容回答我的问题，不要调用 read_file 或 read_html_page 等文件读取工具。】\n"
           + clipped + "\n\n【我的问题】\n" + userContent;
}

void ChatEngine::doSendMessage(const QString &finalContent)
{
    m_context.append(QJsonObject{{"role","user"},{"content",finalContent}});

    m_toolCancelled = false;
    setLoading(true);
    m_loopIter = 0;
    m_dao.insertLog(m_agent.id, m_convId, "chat_start", m_agent.modelName, 0, 0, 0);

    m_client->sendMessage(m_agent.provider, m_agent.modelName,
                          m_agent.apiKey, m_agent.baseUrl,
                          m_context, m_agent.temperature, m_agent.maxTokens,
                          ToolRegistry::instance().toOpenAITools());
}

void ChatEngine::onLLMResponse(const QString &content, int tokIn, int tokOut)
{
    qDebug() << "[Chat] onLLMResponse content_len:" << content.length()
             << "tokIn:" << tokIn << "tokOut:" << tokOut;

    m_totalIn  += tokIn;
    m_totalOut += tokOut;
    setLoading(false);

    saveAndEmit("assistant", content, tokIn, tokOut);
    m_context.append(QJsonObject{{"role","assistant"},{"content",content}});

    m_dao.insertLog(m_agent.id, m_convId, "chat_complete",
                    m_agent.modelName, tokIn, tokOut, 0);
    emit tokensUpdated();
}

void ChatEngine::onToolCallsReady(const QList<ToolCall> &calls, int tokIn)
{
    m_totalIn += tokIn;
    m_loopIter++;
    qDebug() << "[Chat] onToolCallsReady iter:" << m_loopIter << "calls:" << calls.size();

    if (m_loopIter > MAX_ITERATIONS) {
        setLoading(false);
        emit errorOccurred("Agent 达到最大迭代次数限制，已停止。");
        return;
    }

    // Add the assistant's tool_call turn to context
    QJsonArray toolCallsJson;
    for (const ToolCall &c : calls) {
        QJsonObject funcObj;
        funcObj["name"]      = c.name;
        funcObj["arguments"] = QString::fromUtf8(QJsonDocument(c.arguments).toJson(QJsonDocument::Compact));
        QJsonObject tcObj;
        tcObj["id"]       = c.callId;
        tcObj["type"]     = "function";
        tcObj["function"] = funcObj;
        toolCallsJson.append(tcObj);
    }
    m_context.append(QJsonObject{
        {"role",       "assistant"},
        {"content",    QJsonValue::Null},
        {"tool_calls", toolCallsJson}
    });

    // Execute tools asynchronously so the UI stays responsive
    executeToolsAsync(calls);
}

void ChatEngine::executeToolsAsync(const QList<ToolCall> &calls)
{
    // Run tools in the global thread pool; the UI thread stays responsive.
    // Capture a copy of calls so the lambda is safe after this function returns.
    QFuture<QStringList> future = QtConcurrent::run([this, calls]() -> QStringList {
        QStringList results;
        for (const ToolCall &c : calls) {
            if (m_toolCancelled.load()) break;
            results.append(m_executor->execute(c.name, c.arguments));
        }
        return results;
    });

    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher]() {
        if (m_toolCancelled.load()) {
            m_toolCancelled = false;
            watcher->deleteLater();
            return;
        }
        QStringList results = watcher->result();
        watcher->deleteLater();
        handleToolResults(results);
    });
    watcher->setFuture(future);
}

void ChatEngine::handleToolResults(QStringList results)
{
    if (m_toolCancelled.load()) {
        m_toolCancelled = false;
        return;
    }

    // The caller (executeToolsAsync) passes results in the same order as the calls.
    // We recover the last assistant tool_call turn from context to match ids.
    QJsonArray lastToolCalls;
    if (!m_context.isEmpty()) {
        for (auto it = m_context.end() - 1; it != m_context.begin(); --it) {
            const QJsonObject obj = (*it).toObject();
            if (obj["role"].toString() == "assistant" && obj.contains("tool_calls")) {
                lastToolCalls = obj["tool_calls"].toArray();
                break;
            }
        }
    }

    for (int i = 0; i < lastToolCalls.size() && i < results.size(); ++i) {
        const QString callId = lastToolCalls[i].toObject()["id"].toString();
        m_context.append(QJsonObject{
            {"role",         "tool"},
            {"tool_call_id", callId},
            {"content",      results[i]}
        });
    }

    // Continue the agent loop — send updated context back to LLM
    m_client->sendMessage(m_agent.provider, m_agent.modelName,
                          m_agent.apiKey, m_agent.baseUrl,
                          m_context, m_agent.temperature, m_agent.maxTokens,
                          ToolRegistry::instance().toOpenAITools());
}

void ChatEngine::cancelRequest()
{
    m_toolCancelled = true;
    m_client->cancelCurrent();
    setLoading(false);
}

// ── helpers ──────────────────────────────────────────────────────────────────

void ChatEngine::saveAndEmit(const QString &role, const QString &content,
                              int tokIn, int tokOut)
{
    MessageData msg;
    msg.conversationId = m_convId;
    msg.role      = role;
    msg.content   = content;
    msg.tokensIn  = tokIn;
    msg.tokensOut = tokOut;
    msg.modelName = m_agent.modelName;
    msg.createdAt = QDateTime::currentDateTime().toString("hh:mm:ss");
    m_dao.insertMessage(msg);
    m_messages.append(msg);
    emit messageReceived(role, content, msg.createdAt);

    // Auto-save assistant responses as learning notes
    if (role == "assistant") {
        NoteData note;
        note.agentId    = m_agent.id;
        note.sourceFile = m_contextFile;
        note.content    = content;
        QString title = content.trimmed();
        int lineEnd = title.indexOf('\n');
        if (lineEnd > 3) title = title.left(lineEnd);
        if (title.length() > 60) title = title.left(60) + "...";
        if (title.isEmpty()) title = "笔记 " + msg.createdAt;
        note.title = title;
        m_noteDao.insertNote(note);
    }
}

QVariantList ChatEngine::getConversations(int agentId)
{
    const auto convs = m_dao.getConversations(agentId);
    QVariantList result;
    for (const auto &[id, title] : convs)
        result.append(QVariantMap{{"id", id}, {"title", title}});
    return result;
}

QVariantList ChatEngine::currentMessages()
{
    QVariantList result;
    for (const MessageData &m : m_messages) {
        if (m.role == "system") continue;
        result.append(QVariantMap{
            {"role",    m.role},
            {"content", m.content},
            {"timestamp", m.createdAt},
        });
    }
    return result;
}
