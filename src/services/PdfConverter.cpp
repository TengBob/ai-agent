#include "PdfConverter.h"
#include "database/DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QDesktopServices>
#include <QUrl>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDebug>
#include <QtConcurrent/QtConcurrentRun>
#include <QMetaObject>
#include <QThread>
#include <QDateTime>

static const char *SETTING_KB_DIR = "knowledgeBaseDir";
static const char *SETTING_PDF2HTMLEX = "pdf2HtmlEXPath";

PdfConverter::PdfConverter(QObject *parent)
    : QObject(parent)
    , m_settings(this)
{
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PdfConverter::onProcessReadyRead);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PdfConverter::onProcessReadyRead);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &PdfConverter::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred,
            this, &PdfConverter::onProcessError);
}

QString PdfConverter::knowledgeBaseDir() const
{
    QString dir = m_settings.value(SETTING_KB_DIR).toString();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/knowledge_base";
    }
    return QDir::fromNativeSeparators(dir);
}

QString PdfConverter::pdf2HtmlEXPath() const
{
    return m_settings.value(SETTING_PDF2HTMLEX).toString();
}

bool PdfConverter::setKnowledgeBaseDir(const QString &dir)
{
    if (dir.isEmpty()) return false;
    const QString normalized = QDir::fromNativeSeparators(dir);
    if (!QDir().mkpath(normalized)) return false;
    m_settings.setValue(SETTING_KB_DIR, normalized);
    emit knowledgeBaseDirChanged();
    return true;
}

bool PdfConverter::setPdf2HtmlEXPath(const QString &path)
{
    if (path.isEmpty()) {
        m_settings.remove(SETTING_PDF2HTMLEX);
        emit pdf2HtmlEXPathChanged();
        return true;
    }
    if (!QFileInfo::exists(path)) return false;
    m_settings.setValue(SETTING_PDF2HTMLEX, QDir::fromNativeSeparators(path));
    emit pdf2HtmlEXPathChanged();
    return true;
}

bool PdfConverter::checkPdf2HtmlEX(const QString &path) const
{
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
             << "] checkPdf2HtmlEX trying:" << path;
    QProcess proc;
    proc.start(path, {"--version"});
    if (!proc.waitForStarted(3000)) {
        qDebug() << "[PdfConverter] checkPdf2HtmlEX failed to start:" << proc.errorString();
        return false;
    }
    if (!proc.waitForFinished(3000)) {
        qDebug() << "[PdfConverter] checkPdf2HtmlEX timeout";
        return false;
    }
    qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
             << "] checkPdf2HtmlEX exit code:" << proc.exitCode()
             << "elapsed ms:" << (QDateTime::currentMSecsSinceEpoch() - startMs);
    return proc.exitCode() == 0;
}

bool PdfConverter::checkPdf2HtmlEX() const
{
    QString path = pdf2HtmlEXPath();
    if (path.isEmpty()) {
        path = "pdf2htmlEX";
    }
    return checkPdf2HtmlEX(path);
}

void PdfConverter::checkPdf2HtmlEXAsync() const
{
    PdfConverter *self = const_cast<PdfConverter*>(this);
    const QString path = pdf2HtmlEXPath().isEmpty() ? QStringLiteral("pdf2htmlEX") : pdf2HtmlEXPath();
    qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
             << "] checkPdf2HtmlEXAsync scheduling path:" << path;
    QtConcurrent::run([self, path]() {
        qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
                 << "] checkPdf2HtmlEXAsync running";
        const bool available = self ? self->checkPdf2HtmlEX(path) : false;
        if (self) {
            QMetaObject::invokeMethod(self,
                                      "pdf2HtmlEXAvailabilityChanged",
                                      Qt::QueuedConnection,
                                      Q_ARG(bool, available));
        }
    });
}

static bool looksLikePdf2HtmlEX(const QString &name)
{
    const QString lower = name.toLower();
    return lower.startsWith("pdf2html") || lower.contains("pdf2html");
}

static QString findProjectRoot()
{
    QDir dir(QDir::currentPath());
    for (int i = 0; i < 6 && !dir.isRoot(); ++i) {
        if (QFileInfo::exists(dir.absoluteFilePath("CMakeLists.txt")) ||
            QFileInfo::exists(dir.absoluteFilePath(".git"))) {
            return dir.absolutePath();
        }
        if (!dir.cdUp()) break;
    }
    return QString();
}

void PdfConverter::appendDetectLog(const QString &line)
{
    m_detectLog.append(line + "\n");
    emit detectLogChanged();
}

bool PdfConverter::findPdf2HtmlEX()
{
    m_detectLog.clear();
    emit detectLogChanged();
    appendDetectLog("开始自动检测 pdf2htmlEX...");
    qDebug() << "[PdfConverter] findPdf2HtmlEX started";

    // Already configured and valid?
    const QString configured = pdf2HtmlEXPath();
    if (!configured.isEmpty()) {
        appendDetectLog("检查已配置路径: " + configured);
        if (checkPdf2HtmlEX()) {
            appendDetectLog("已配置路径可用");
            qDebug() << "[PdfConverter] current setting is valid:" << configured;
            return true;
        }
        appendDetectLog("已配置路径不可用: " + configured);
    }

#ifdef Q_OS_WIN
    QStringList baseDirs = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath(),
        findProjectRoot(),
        knowledgeBaseDir() + "/bin",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/bin",
        "C:/Program Files/pdf2htmlEX/bin",
        "C:/Program Files (x86)/pdf2htmlEX/bin",
    };
    const QStringList exactNames = {"pdf2htmlEX.exe", "pdf2htmlEX-*.exe"};
#else
    QStringList baseDirs = {
        QDir::currentPath(),
        QCoreApplication::applicationDirPath(),
        findProjectRoot(),
        knowledgeBaseDir() + "/bin",
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/bin",
        "/usr/local/bin",
        "/usr/bin",
    };
    const QStringList exactNames = {"pdf2htmlEX"};
#endif
    baseDirs.removeDuplicates();

    const QString projectRoot = findProjectRoot();

    // Search base dirs (non-recursive for system paths, recursive for project/app dirs)
    for (const QString &dir : baseDirs) {
        if (dir.isEmpty()) continue;
        const bool isProjectOrApp = (dir == QDir::currentPath() ||
                                     dir == QCoreApplication::applicationDirPath() ||
                                     dir == projectRoot ||
                                     dir == knowledgeBaseDir() + "/bin");
        if (isProjectOrApp) {
            appendDetectLog("递归搜索目录: " + dir);
            QDirIterator it(dir, QDir::Files | QDir::Executable,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                const QString filePath = it.filePath();
                const QString fileName = it.fileName();
                if (!looksLikePdf2HtmlEX(fileName))
                    continue;
                appendDetectLog("  尝试: " + filePath);
                if (setPdf2HtmlEXPath(filePath) && checkPdf2HtmlEX()) {
                    appendDetectLog("找到可用文件: " + filePath);
                    qDebug() << "[PdfConverter] auto-detected pdf2htmlEX at:" << filePath;
                    return true;
                }
            }
        } else {
            appendDetectLog("搜索目录: " + dir);
            for (const QString &name : exactNames) {
                const QString path = dir + "/" + name;
                appendDetectLog("  尝试: " + path);
                if (name.contains('*')) {
                    const QDir d(dir);
                    const QStringList matches = d.entryList({name}, QDir::Files);
                    for (const QString &match : matches) {
                        const QString full = dir + "/" + match;
                        appendDetectLog("  匹配到: " + full);
                        if (setPdf2HtmlEXPath(full) && checkPdf2HtmlEX()) {
                            appendDetectLog("找到可用文件: " + full);
                            qDebug() << "[PdfConverter] auto-detected pdf2htmlEX at:" << full;
                            return true;
                        }
                    }
                } else if (QFileInfo::exists(path)) {
                    if (setPdf2HtmlEXPath(path) && checkPdf2HtmlEX()) {
                        appendDetectLog("找到可用文件: " + path);
                        qDebug() << "[PdfConverter] auto-detected pdf2htmlEX at:" << path;
                        return true;
                    }
                    appendDetectLog("  存在但无法执行: " + path);
                }
            }
        }
    }

    // Try PATH
    const QString exeName = "pdf2htmlEX";
    appendDetectLog("尝试 PATH: " + exeName);
    qDebug() << "[PdfConverter] trying PATH:" << exeName;
    QProcess proc;
    proc.start(exeName, {"--version"});
    if (proc.waitForStarted(3000) && proc.waitForFinished(3000) && proc.exitCode() == 0) {
        appendDetectLog("在 PATH 中找到 pdf2htmlEX");
        qDebug() << "[PdfConverter] found in PATH";
        setPdf2HtmlEXPath(exeName);
        return true;
    }
    appendDetectLog("PATH 中未找到: " + proc.errorString());
    qDebug() << "[PdfConverter] not found in PATH, error:" << proc.errorString();

    appendDetectLog("自动检测结束，未找到可用的 pdf2htmlEX。");
    qDebug() << "[PdfConverter] auto-detect failed";
    return false;
}

QString PdfConverter::downloadUrl() const
{
    return "https://github.com/pdf2htmlEX/pdf2htmlEX/releases";
}

void PdfConverter::openDownloadPage() const
{
    QDesktopServices::openUrl(QUrl(downloadUrl()));
}

bool PdfConverter::ensureKnowledgeBaseDir()
{
    const QString dir = knowledgeBaseDir();
    return QDir().mkpath(dir);
}

int PdfConverter::insertDocument(const QString &title, const QString &sourcePath)
{
    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare(R"(
        INSERT INTO documents (title, source_path, status)
        VALUES (:title, :source_path, 'converting')
    )");
    q.bindValue(":title", title);
    q.bindValue(":source_path", sourcePath);
    if (!q.exec()) {
        qWarning() << "[PdfConverter] insert document failed:" << q.lastError().text();
        return -1;
    }
    const int id = q.lastInsertId().toInt();
    emit documentsChanged();
    return id;
}

bool PdfConverter::updateDocumentStatus(int docId, const QString &status, const QString &errorMsg)
{
    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare("UPDATE documents SET status=:status, error_message=:err WHERE id=:id");
    q.bindValue(":status", status);
    q.bindValue(":err", errorMsg);
    q.bindValue(":id", docId);
    if (!q.exec()) {
        qWarning() << "[PdfConverter] update status failed:" << q.lastError().text();
        return false;
    }
    emit documentsChanged();
    return true;
}

bool PdfConverter::updateDocumentOutput(int docId, const QString &outputDir,
                                        const QString &htmlPath, int pageCount)
{
    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare(R"(
        UPDATE documents
        SET output_dir=:out_dir, html_path=:html, page_count=:pages
        WHERE id=:id
    )");
    q.bindValue(":out_dir", outputDir);
    q.bindValue(":html", htmlPath);
    q.bindValue(":pages", pageCount);
    q.bindValue(":id", docId);
    if (!q.exec()) {
        qWarning() << "[PdfConverter] update output failed:" << q.lastError().text();
        return false;
    }
    emit documentsChanged();
    return true;
}

bool PdfConverter::reconvertPdf(int docId)
{
    if (m_isConverting) {
        qWarning() << "[PdfConverter] already converting";
        return false;
    }

    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare("SELECT source_path, output_dir FROM documents WHERE id=:id");
    q.bindValue(":id", docId);
    if (!q.exec() || !q.next()) return false;

    const QString sourcePath = q.value(0).toString();
    QString outputDir = q.value(1).toString();

    if (!QFileInfo::exists(sourcePath)) {
        updateDocumentStatus(docId, "error", "原 PDF 文件不存在: " + sourcePath);
        emit conversionFinished(docId, false, "原 PDF 文件不存在");
        return false;
    }

    // Reuse same output dir, clean it first
    if (outputDir.isEmpty()) {
        outputDir = knowledgeBaseDir() + "/doc_" + QString::number(docId);
    }
    QDir dir(outputDir);
    if (dir.exists()) {
        const auto entries = dir.entryList(QDir::Files);
        for (const QString &f : entries) dir.remove(f);
    }
    QDir().mkpath(outputDir);

    updateDocumentStatus(docId, "converting");

    QString exePath = pdf2HtmlEXPath();
    if (exePath.isEmpty()) exePath = "pdf2htmlEX";

    m_currentDocId = docId;
    m_currentPdfPath = sourcePath;
    m_currentOutputDir = outputDir;

    setIsConverting(true);
    emit conversionProgress(docId, 0, "重新转换...");
    m_processOutput.clear();

    QStringList args;
    args << "--embed-image" << "1"
         << "--embed-font"  << "1"
         << "--embed-css"   << "1"
         << "--dest-dir" << QDir::toNativeSeparators(outputDir)
         << QDir::toNativeSeparators(sourcePath)
         << "index.html";

    qDebug() << "[PdfConverter] reconverting:" << exePath << args;
    m_process->start(exePath, args);
    return true;
}

int PdfConverter::convertPdf(const QString &pdfPath)
{
    if (m_isConverting) {
        qWarning() << "[PdfConverter] already converting";
        return -1;
    }

    QString exePath = pdf2HtmlEXPath();
    if (exePath.isEmpty()) exePath = "pdf2htmlEX";

    if (!ensureKnowledgeBaseDir()) {
        emit conversionFinished(-1, false, "无法创建知识库目录");
        return -1;
    }

    const QFileInfo fi(pdfPath);
    if (!fi.exists()) {
        emit conversionFinished(-1, false, "PDF 文件不存在: " + pdfPath);
        return -1;
    }

    const QString title = fi.completeBaseName();
    const int docId = insertDocument(title, QDir::fromNativeSeparators(pdfPath));
    if (docId < 0) {
        emit conversionFinished(-1, false, "无法创建文档记录");
        return -1;
    }

    const QString outputDir = knowledgeBaseDir() + "/doc_" + QString::number(docId);
    QDir().mkpath(outputDir);

    m_currentDocId = docId;
    m_currentPdfPath = QDir::fromNativeSeparators(pdfPath);
    m_currentOutputDir = outputDir;

    setIsConverting(true);
    emit conversionProgress(docId, 0, "开始转换...");
    m_processOutput.clear();

    QStringList args;
    args << "--embed-image" << "1"
         << "--embed-font"  << "1"
         << "--embed-css"   << "1"
         << "--dest-dir" << QDir::toNativeSeparators(outputDir)
         << QDir::toNativeSeparators(m_currentPdfPath)
         << "index.html";

    qDebug() << "[PdfConverter] starting conversion:" << exePath << args;
    m_process->start(exePath, args);
    return docId;
}

void PdfConverter::onProcessReadyRead()
{
    if (!m_process) return;

    const QByteArray out = m_process->readAllStandardOutput();
    const QByteArray err = m_process->readAllStandardError();
    const QString text = QString::fromUtf8(out + err).trimmed();
    if (text.isEmpty()) return;

    qDebug() << "[PdfConverter] process output:" << text;
    m_processOutput.append(text + "\n");

    // Parse progress like "Working: 3/10" or "Preprocessing: 1/10"
    static const QRegularExpression re(R"((\w+):\s*(\d+)\/(\d+))");
    const QRegularExpressionMatch m = re.match(text);
    if (m.hasMatch()) {
        const int current = m.captured(2).toInt();
        const int total   = m.captured(3).toInt();
        const int percent = total > 0 ? qMin(100, (current * 100) / total) : 0;
        emit conversionProgress(m_currentDocId, percent,
                                m.captured(1) + ": " + QString::number(current) + "/" + QString::number(total));
    } else {
        emit conversionProgress(m_currentDocId, -1, text.left(200));
    }
}

void PdfConverter::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    setIsConverting(false);

    // Drain any remaining output
    const QByteArray remainOut = m_process->readAllStandardOutput();
    const QByteArray remainErr = m_process->readAllStandardError();
    const QString remaining = QString::fromUtf8(remainOut + remainErr).trimmed();
    if (!remaining.isEmpty()) {
        m_processOutput.append(remaining + "\n");
        qDebug() << "[PdfConverter] final output:" << remaining;
    }

    if (status != QProcess::NormalExit || exitCode != 0) {
        // Prefer actual process output over the generic QProcess errorString
        QString errMsg = m_processOutput.trimmed();
        if (errMsg.isEmpty()) errMsg = m_process->errorString();
        const QString fullErr = "转换失败 (exit " + QString::number(exitCode) + "): " + errMsg;
        updateDocumentStatus(m_currentDocId, "error", fullErr);
        emit conversionFinished(m_currentDocId, false, fullErr);
        m_currentDocId = -1;
        m_processOutput.clear();
        return;
    }

    const QString htmlPath = m_currentOutputDir + "/index.html";
    if (!QFileInfo::exists(htmlPath)) {
        const QString errMsg = m_processOutput.trimmed().isEmpty()
            ? "未生成 index.html"
            : "未生成 index.html\n" + m_processOutput.trimmed();
        updateDocumentStatus(m_currentDocId, "error", errMsg);
        emit conversionFinished(m_currentDocId, false, errMsg);
        m_currentDocId = -1;
        m_processOutput.clear();
        return;
    }

    updateDocumentOutput(m_currentDocId, m_currentOutputDir, htmlPath, 0);
    updateDocumentStatus(m_currentDocId, "done");
    emit conversionProgress(m_currentDocId, 100, "完成");
    emit conversionFinished(m_currentDocId, true, QString());
    m_currentDocId = -1;
    m_processOutput.clear();
}

void PdfConverter::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        setIsConverting(false);
        const QString errMsg = "无法启动 pdf2htmlEX，请检查路径配置";
        updateDocumentStatus(m_currentDocId, "error", errMsg);
        emit conversionFinished(m_currentDocId, false, errMsg);
        m_currentDocId = -1;
        m_processOutput.clear();
    }
}

bool PdfConverter::deleteDocument(int docId)
{
    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare("SELECT output_dir FROM documents WHERE id=:id");
    q.bindValue(":id", docId);
    if (!q.exec() || !q.next()) return false;

    const QString outputDir = q.value(0).toString();
    if (!outputDir.isEmpty()) {
        QDir dir(outputDir);
        if (dir.exists()) dir.removeRecursively();
    }

    QSqlQuery del(DatabaseManager::instance().db());
    del.prepare("DELETE FROM documents WHERE id=:id");
    del.bindValue(":id", docId);
    if (!del.exec()) {
        qWarning() << "[PdfConverter] delete document failed:" << del.lastError().text();
        return false;
    }
    emit documentsChanged();
    return true;
}

bool PdfConverter::openHtmlFile(const QString &htmlPath)
{
    if (htmlPath.isEmpty() || !QFileInfo::exists(htmlPath)) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath));
}

QVariantList PdfConverter::scanHtmlFiles(const QString &base, const QHash<QString, QString> &dirToTitle) const
{
    QVariantList result;
    QDirIterator it(base, {"*.html", "*.htm"}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString fullPath = it.filePath();
        const QString fileName = it.fileName();
        const QString rel = QDir(base).relativeFilePath(fullPath);
        const QString parentDir = it.fileInfo().dir().dirName();
        const QString parentPath = QDir::fromNativeSeparators(it.fileInfo().dir().absolutePath());

        QString displayName = fileName;
        if (fileName.toLower() == "index.html") {
            displayName = dirToTitle.value(parentPath, parentDir);
        }

        result.append(QVariantMap{
            {"name",        fileName},
            {"displayName", displayName},
            {"path",        fullPath},
            {"relPath",     rel},
            {"dir",         parentDir},
        });
    }
    std::sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap()["relPath"].toString() < b.toMap()["relPath"].toString();
    });
    return result;
}

QVariantList PdfConverter::scanHtmlFiles()
{
    const QString base = knowledgeBaseDir();
    if (base.isEmpty()) return QVariantList();

    QHash<QString, QString> dirToTitle;
    QSqlQuery tq(DatabaseManager::instance().db());
    tq.prepare("SELECT output_dir, title FROM documents WHERE status='done'");
    if (tq.exec()) {
        while (tq.next()) {
            const QString outDir = QDir::fromNativeSeparators(tq.value(0).toString());
            dirToTitle[outDir] = tq.value(1).toString();
        }
    }

    return scanHtmlFiles(base, dirToTitle);
}

void PdfConverter::scanHtmlFilesAsync()
{
    const QString base = knowledgeBaseDir();
    qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
             << "] scanHtmlFilesAsync scheduling base:" << base;
    QHash<QString, QString> dirToTitle;
    if (!base.isEmpty()) {
        QSqlQuery tq(DatabaseManager::instance().db());
        tq.prepare("SELECT output_dir, title FROM documents WHERE status='done'");
        if (tq.exec()) {
            while (tq.next()) {
                dirToTitle[QDir::fromNativeSeparators(tq.value(0).toString())] = tq.value(1).toString();
            }
        }
    }

    PdfConverter *self = this;
    QtConcurrent::run([self, base, dirToTitle]() {
        qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
                 << "] scanHtmlFilesAsync running base:" << base;
        QVariantList files;
        if (self && !base.isEmpty()) files = self->scanHtmlFiles(base, dirToTitle);
        qDebug() << "[PdfConverter] [thread" << QThread::currentThreadId()
                 << "] scanHtmlFilesAsync scanned count:" << files.count();
        if (self) {
            QMetaObject::invokeMethod(self,
                                      "htmlFilesScanned",
                                      Qt::QueuedConnection,
                                      Q_ARG(QVariantList, files));
        }
    });
}

bool PdfConverter::openInBrowser(int docId)
{
    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare("SELECT html_path FROM documents WHERE id=:id");
    q.bindValue(":id", docId);
    if (!q.exec() || !q.next()) return false;

    const QString path = q.value(0).toString();
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;

    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QVariantList PdfConverter::documents()
{
    QVariantList result;
    QSqlQuery q(DatabaseManager::instance().db());
    q.prepare(R"(
        SELECT id, title, source_path, output_dir, html_path,
               page_count, status, error_message, created_at
        FROM documents
        ORDER BY id DESC
    )");
    if (!q.exec()) {
        qWarning() << "[PdfConverter] load documents failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        result.append(QVariantMap{
            {"id",          q.value(0).toInt()},
            {"title",       q.value(1).toString()},
            {"sourcePath",  q.value(2).toString()},
            {"outputDir",   q.value(3).toString()},
            {"htmlPath",    q.value(4).toString()},
            {"pageCount",   q.value(5).toInt()},
            {"status",      q.value(6).toString()},
            {"errorMessage",q.value(7).toString()},
            {"createdAt",   q.value(8).toString()},
        });
    }
    return result;
}

void PdfConverter::setIsConverting(bool v)
{
    if (m_isConverting == v) return;
    m_isConverting = v;
    emit isConvertingChanged();
}

QString PdfConverter::readHtmlPage(const QString &htmlPath, int page) const
{
    QFile f(htmlPath);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("无法打开文件：") + htmlPath;

    const qint64 fileSize = f.size();
    if (fileSize <= 0)
        return QStringLiteral("文件为空：") + htmlPath;

    uchar *mapped = f.map(0, fileSize);
    if (!mapped)
        return QStringLiteral("无法映射文件：") + htmlPath;

    const QByteArray data = QByteArray::fromRawData(reinterpret_cast<const char *>(mapped), fileSize);

    const QByteArray startMarker = QStringLiteral("data-page-no=\"%1\"").arg(page).toUtf8();
    int start = data.indexOf(startMarker);
    if (start < 0) {
        const QByteArray startMarkerAlt = QStringLiteral("id=\"pf%1\"").arg(page).toUtf8();
        start = data.indexOf(startMarkerAlt);
    }
    if (start < 0) {
        f.unmap(mapped);
        return QStringLiteral("未找到第 %1 页").arg(page);
    }

    // Find the actual <div ...> start position for this page
    int divStart = data.lastIndexOf("<div", start);
    if (divStart < 0) divStart = start;

    int end = data.indexOf("data-page-no=\"", start + startMarker.size());
    if (end < 0)
        end = data.indexOf("id=\"pf", start + startMarker.size());
    if (end < 0)
        end = data.indexOf("</body>", start);
    if (end < 0)
        end = fileSize;

    QString pageHtml = QString::fromUtf8(data.constData() + divStart, end - divStart);
    f.unmap(mapped);

    static const QRegularExpression scriptRe("<script[^>]*>[\\s\\S]*?</script>",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression styleRe("<style[^>]*>[\\s\\S]*?</style>",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tagRe("<[^>]+>");
    static const QRegularExpression spaceRe("\\s{3,}");

    pageHtml.remove(scriptRe);
    pageHtml.remove(styleRe);
    pageHtml.remove(tagRe);
    pageHtml.replace("&nbsp;", " ");
    pageHtml.replace("&lt;",   "<");
    pageHtml.replace("&gt;",   ">");
    pageHtml.replace("&amp;",  "&");
    pageHtml.replace("&quot;", "\"");
    pageHtml = pageHtml.replace(spaceRe, "\n").trimmed();

    if (pageHtml.length() > 4000)
        pageHtml = pageHtml.left(4000) + "\n...[内容已截断]";

    return pageHtml;
}

QVariantMap PdfConverter::fileInfo(const QString &path) const
{
    QFileInfo fi(path);
    return QVariantMap{
        {"exists",   fi.exists()},
        {"size",     fi.size()},
        {"baseName", fi.baseName()},
        {"fileName", fi.fileName()},
        {"suffix",   fi.suffix()}
    };
}

static QString extractSingleHtmlPage(const QString &htmlPath, int page)
{
    QFileInfo srcFi(htmlPath);
    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString tmpName = QStringLiteral("stock_kb_%1_%2_p%3.html")
                                .arg(srcFi.dir().dirName())
                                .arg(srcFi.baseName())
                                .arg(page);
    const QString tmpPath = QDir(tmpDir).absoluteFilePath(tmpName);

    // Reuse already-extracted page if it exists and looks valid.
    if (QFileInfo::exists(tmpPath)) {
        QFile check(tmpPath);
        bool valid = false;
        if (check.open(QIODevice::ReadOnly) && check.size() > 0) {
            const QByteArray content = check.readAll();
            const QByteArray expectedMarker = QStringLiteral("data-page-no=\"%1\"").arg(page).toUtf8();
            valid = content.contains(expectedMarker);
        }
        check.close();
        if (valid) {
            qDebug() << "[PdfConverter] extractSingleHtmlPage reusing cached tmp:" << tmpPath;
            return QDir::fromNativeSeparators(tmpPath);
        }
        qWarning() << "[PdfConverter] extractSingleHtmlPage stale tmp removed:" << tmpPath;
        QFile::remove(tmpPath);
    }

    QFile f(htmlPath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();

    const qint64 fileSize = f.size();
    qDebug() << "[PdfConverter] extractSingleHtmlPage path:" << htmlPath
             << "page:" << page << "size:" << fileSize;

    if (fileSize <= 0)
        return QString();

    // Memory-map the file so we can search it as a contiguous byte array without
    // copying the whole thing into memory or converting it line-by-line.
    uchar *mapped = f.map(0, fileSize);
    if (!mapped) {
        qWarning() << "[PdfConverter] extractSingleHtmlPage failed to map file:" << htmlPath;
        return QString();
    }

    const QByteArray data = QByteArray::fromRawData(reinterpret_cast<const char *>(mapped), fileSize);

    auto toString = [](const QByteArray &v, int pos, int len) -> QString {
        return QString::fromUtf8(v.constData() + pos, len);
    };

    // Extract <head>...</head> once.
    const QByteArray headStartMarker = "<head";
    const QByteArray headEndMarker   = "</head>";
    QString headHtml;
    int headStart = data.indexOf(headStartMarker);
    if (headStart >= 0) {
        int headEnd = data.indexOf(headEndMarker, headStart);
        if (headEnd > headStart) {
            headEnd += headEndMarker.size();
            headHtml = toString(data, headStart, headEnd - headStart);
        }
    }

    // Find the requested page marker.
    const QByteArray startMarker = QStringLiteral("data-page-no=\"%1\"").arg(page).toUtf8();
    int markerPos = data.indexOf(startMarker);
    if (markerPos < 0) {
        const QByteArray startMarkerAlt = QStringLiteral("id=\"pf%1\"").arg(page).toUtf8();
        markerPos = data.indexOf(startMarkerAlt);
    }

    if (markerPos < 0) {
        qWarning() << "[PdfConverter] extractSingleHtmlPage page marker not found:" << page;
        f.unmap(mapped);
        return QString();
    }

    // Find the actual <div ...> start for this page.
    int divStart = data.lastIndexOf("<div", markerPos);
    if (divStart < 0) divStart = markerPos;

    // Find the end: next page marker or </body>.
    int end = data.indexOf("data-page-no=\"", markerPos + startMarker.size());
    if (end < 0)
        end = data.indexOf("id=\"pf", markerPos + startMarker.size());
    if (end < 0)
        end = data.indexOf("</body>", markerPos);
    if (end < 0)
        end = fileSize;

    QString pageHtml = toString(data, divStart, end - divStart);
    f.unmap(mapped);

    QString bodyHtml = QStringLiteral("<div id=\"page-container\">%1</div>").arg(pageHtml);
    QString out = QStringLiteral("<!DOCTYPE html><html xmlns=\"http://www.w3.org/1999/xhtml\"><head><meta charset=\"utf-8\"/>%1</head><body>%2</body></html>")
                      .arg(headHtml, bodyHtml);

    QFile outFile(tmpPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    QTextStream(&outFile) << out;
    outFile.close();

    qDebug() << "[PdfConverter] extractSingleHtmlPage wrote tmp:" << tmpPath;
    return QDir::fromNativeSeparators(tmpPath);
}

void PdfConverter::extractHtmlPageAsync(const QString &htmlPath, int page) const
{
    PdfConverter *self = const_cast<PdfConverter*>(this);
    QtConcurrent::run([self, htmlPath, page]() {
        const QString tmpPath = self ? extractSingleHtmlPage(htmlPath, page) : QString();
        if (self) {
            QMetaObject::invokeMethod(self,
                                      "htmlPageExtracted",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, htmlPath),
                                      Q_ARG(int, page),
                                      Q_ARG(QString, tmpPath));
        }
    });
}

QString PdfConverter::buildKnowledgeBasePrompt()
{
    const QVariantList files = scanHtmlFiles();
    if (files.isEmpty())
        return QString();

    static const QRegularExpression tagRe("<[^>]+>");
    static const QRegularExpression spaceRe("\\s{3,}");

    QString prompt =
        "你是一个本地知识库助手。以下是知识库中所有文档的内容摘要，"
        "请根据这些内容回答用户问题。如果问题超出知识库范围，请如实告知。\n\n"
        "========== 知识库内容 ==========\n\n";

    for (const QVariant &v : files) {
        const QVariantMap m = v.toMap();
        const QString path        = m["path"].toString();
        const QString displayName = m["displayName"].toString();

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;

        QString html = QTextStream(&f).readAll();
        f.close();

        // Strip <head>...</head> and <script>...</script> blocks
        static const QRegularExpression headRe("<head[^>]*>[\\s\\S]*?</head>",
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression scriptRe("<script[^>]*>[\\s\\S]*?</script>",
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression styleRe("<style[^>]*>[\\s\\S]*?</style>",
            QRegularExpression::CaseInsensitiveOption);
        html.remove(headRe);
        html.remove(scriptRe);
        html.remove(styleRe);

        // Strip remaining tags and decode common entities
        QString text = html;
        text.remove(tagRe);
        text.replace("&nbsp;", " ");
        text.replace("&lt;",   "<");
        text.replace("&gt;",   ">");
        text.replace("&amp;",  "&");
        text.replace("&quot;", "\"");
        text = text.replace(spaceRe, "\n").trimmed();

        // Limit each document to 4000 chars to avoid exceeding context
        if (text.length() > 4000)
            text = text.left(4000) + "\n...[内容已截断]";

        if (text.isEmpty()) continue;

        prompt += "--- 【" + displayName + "】 ---\n";
        prompt += text + "\n\n";
    }

    prompt += "========== 知识库内容结束 ==========\n";
    return prompt;
}
