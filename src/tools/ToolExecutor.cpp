#include "ToolExecutor.h"
#include "services/PdfConverter.h"
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDir>
#include <QDebug>

PdfConverter *ToolExecutor::s_pdfConverter = nullptr;

ToolExecutor::ToolExecutor(QObject *parent) : QObject(parent) {}

void ToolExecutor::setPdfConverter(PdfConverter *converter)
{
    s_pdfConverter = converter;
}

QString ToolExecutor::execute(const QString &toolName, const QJsonObject &args)
{
    qDebug() << "[ToolExecutor] execute:" << toolName << "args:" << QJsonDocument(args).toJson(QJsonDocument::Compact);
    emit toolStarted(toolName, QJsonDocument(args).toJson(QJsonDocument::Compact));
    QString result;
    if      (toolName == "web_search")           result = webSearch(args);
    else if (toolName == "read_file")            result = readFile(args);
    else if (toolName == "write_file")           result = writeFile(args);
    else if (toolName == "run_command")          result = runCommand(args);
    else if (toolName == "http_request")         result = httpRequest(args);
    else if (toolName == "convert_pdf_to_html")  result = convertPdfToHtml(args);
    else if (toolName == "read_html_page")       result = readHtmlPage(args);
    else result = "Unknown tool: " + toolName;
    qDebug() << "[ToolExecutor] result:" << toolName << "len:" << result.length();
    emit toolFinished(toolName, result);
    return result;
}

// ── web_search ──────────────────────────────────────────────────────────────
QString ToolExecutor::webSearch(const QJsonObject &args)
{
    const QString query = args["query"].toString();
    const int maxResults = args["max_results"].toInt(5);

    if (m_tavilyKey.isEmpty())
        return "Error: Tavily API key not set. Please add it in Settings.";

    QJsonObject body;
    body["query"]          = query;
    body["max_results"]    = maxResults;
    body["search_depth"]   = "basic";
    body["include_answer"] = true;

    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl("https://api.tavily.com/search")};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", ("Bearer " + m_tavilyKey).toUtf8());

    QEventLoop loop;
    QNetworkReply *reply = nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        const QString err = reply->errorString();
        reply->deleteLater();
        return "Search error: " + err;
    }

    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    QString out;
    if (obj.contains("answer"))
        out += "Summary: " + obj["answer"].toString() + "\n\n";

    const QJsonArray results = obj["results"].toArray();
    for (int i = 0; i < results.size(); ++i) {
        const QJsonObject r = results[i].toObject();
        out += QString("[%1] %2\n%3\nURL: %4\n\n")
               .arg(i+1)
               .arg(r["title"].toString())
               .arg(r["content"].toString().left(300))
               .arg(r["url"].toString());
    }
    return out.isEmpty() ? "No results found." : out;
}

// ── read_file ────────────────────────────────────────────────────────────────
QString ToolExecutor::readFile(const QJsonObject &args)
{
    QString path = args["path"].toString();
    if (path.isEmpty())
        return "Error: path is required.";

    if (!QDir::isAbsolutePath(path) && !m_workDir.isEmpty())
        path = m_workDir + "/" + path;

    // Security: only allow reading files under the work directory or knowledge base directory
    const QString absPath = QFileInfo(path).canonicalFilePath();
    if (absPath.isEmpty())
        return "Error: Invalid file path.";

    bool allowed = false;
    if (!m_workDir.isEmpty()) {
        const QString absWorkDir = QFileInfo(m_workDir).canonicalFilePath();
        if (!absWorkDir.isEmpty() && absPath.startsWith(absWorkDir)) allowed = true;
    }
    if (!allowed && s_pdfConverter) {
        const QString kbDir = QFileInfo(s_pdfConverter->knowledgeBaseDir()).canonicalFilePath();
        if (!kbDir.isEmpty() && absPath.startsWith(kbDir)) allowed = true;
    }
    if (!allowed) {
        return "Error: Access denied. Can only read files under work directory or knowledge base directory.";
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return "Error: Cannot open file: " + path + " (" + f.errorString() + ")";

    const QString content = QTextStream(&f).readAll();
    f.close();
    if (content.length() > 8000)
        return content.left(8000) + "\n...[truncated, file is larger]";
    return content;
}

// ── write_file ───────────────────────────────────────────────────────────────
QString ToolExecutor::writeFile(const QJsonObject &args)
{
    QString path = args["path"].toString();
    if (!QDir::isAbsolutePath(path) && !m_workDir.isEmpty())
        path = m_workDir + "/" + path;

    const QString content = args["content"].toString();
    const bool append     = args["append"].toBool(false);

    // ensure parent directory exists
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile f(path);
    const QIODevice::OpenMode mode = append
        ? (QIODevice::Append | QIODevice::Text)
        : (QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);

    if (!f.open(mode))
        return "Error: Cannot write file: " + path + " (" + f.errorString() + ")";

    QTextStream(&f) << content;
    f.close();
    return QString("OK: Written %1 characters to %2").arg(content.length()).arg(path);
}

// ── run_command ──────────────────────────────────────────────────────────────
// DISABLED: LLM-generated commands can crash or damage the system.
QString ToolExecutor::runCommand(const QJsonObject &args)
{
    Q_UNUSED(args)
    return "Error: run_command is disabled for security reasons.";
}

// ── http_request ─────────────────────────────────────────────────────────────
QString ToolExecutor::httpRequest(const QJsonObject &args)
{
    const QString url    = args["url"].toString();
    const QString method = args["method"].toString("GET").toUpper();
    const QJsonObject headers = args["headers"].toObject();
    const QString body   = args["body"].toString();

    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};

    for (auto it = headers.begin(); it != headers.end(); ++it)
        req.setRawHeader(it.key().toUtf8(), it.value().toString().toUtf8());

    QEventLoop loop;
    QNetworkReply *reply = nullptr;
    const QByteArray bodyData = body.toUtf8();

    if      (method == "GET")    reply = nam.get(req);
    else if (method == "POST")   reply = nam.post(req, bodyData);
    else if (method == "PUT")    reply = nam.put(req, bodyData);
    else if (method == "DELETE") reply = nam.deleteResource(req);
    else return "Error: Unsupported method " + method;

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    QString result;
    if (reply->error() != QNetworkReply::NoError)
        result = "HTTP Error: " + reply->errorString();
    else
        result = reply->readAll();

    reply->deleteLater();
    if (result.length() > 4000) result = result.left(4000) + "\n...[truncated]";
    return result;
}

// ── convert_pdf_to_html ───────────────────────────────────────────────────────
QString ToolExecutor::convertPdfToHtml(const QJsonObject &args)
{
    if (!s_pdfConverter)
        return "Error: PdfConverter is not initialized.";

    QString pdfPath = args["pdf_path"].toString();
    if (pdfPath.isEmpty())
        return "Error: pdf_path is required.";

    if (!QDir::isAbsolutePath(pdfPath) && !m_workDir.isEmpty())
        pdfPath = m_workDir + "/" + pdfPath;

    if (!QFileInfo::exists(pdfPath))
        return "Error: PDF file does not exist: " + pdfPath;

    QEventLoop loop;
    int finishedDocId = -1;
    bool finishedSuccess = false;
    QString finishedError;

    connect(s_pdfConverter, &PdfConverter::conversionFinished,
            &loop, [&](int docId, bool success, const QString &error) {
        finishedDocId = docId;
        finishedSuccess = success;
        finishedError = error;
        loop.quit();
    });

    const int docId = s_pdfConverter->convertPdf(pdfPath);
    if (docId < 0)
        return "Error: Failed to start PDF conversion (another conversion may be running).";

    loop.exec();

    if (!finishedSuccess)
        return "Error: " + finishedError;

    const QVariantList docs = s_pdfConverter->documents();
    for (const QVariant &v : docs) {
        const QVariantMap m = v.toMap();
        if (m["id"].toInt() == finishedDocId)
            return "OK: Converted to " + m["htmlPath"].toString();
    }
    return "OK: Converted (document id " + QString::number(finishedDocId) + ")";
}

// ── read_html_page ────────────────────────────────────────────────────────────
QString ToolExecutor::readHtmlPage(const QJsonObject &args)
{
    if (!s_pdfConverter)
        return "Error: PdfConverter is not initialized.";

    const QString path = args["file_path"].toString();
    const int page     = args["page"].toInt();

    if (path.isEmpty())
        return "Error: file_path is required.";
    if (page < 1)
        return "Error: page must be >= 1.";
    if (!QFileInfo::exists(path))
        return "Error: HTML file does not exist: " + path;

    return s_pdfConverter->readHtmlPage(path, page);
}
