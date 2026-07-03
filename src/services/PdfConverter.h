#pragma once
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QVariantList>

class PdfConverter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isConverting READ isConverting NOTIFY isConvertingChanged)
    Q_PROPERTY(QString knowledgeBaseDir READ knowledgeBaseDir WRITE setKnowledgeBaseDir NOTIFY knowledgeBaseDirChanged)
    Q_PROPERTY(QString pdf2HtmlEXPath READ pdf2HtmlEXPath WRITE setPdf2HtmlEXPath NOTIFY pdf2HtmlEXPathChanged)
    Q_PROPERTY(QString detectLog READ detectLog NOTIFY detectLogChanged)

public:
    explicit PdfConverter(QObject *parent = nullptr);

    bool isConverting() const { return m_isConverting; }
    QString knowledgeBaseDir() const;
    QString pdf2HtmlEXPath() const;
    QString detectLog() const { return m_detectLog; }

    Q_INVOKABLE bool setKnowledgeBaseDir(const QString &dir);
    Q_INVOKABLE bool setPdf2HtmlEXPath(const QString &path);
    Q_INVOKABLE bool checkPdf2HtmlEX() const;
    bool checkPdf2HtmlEX(const QString &path) const;
    Q_INVOKABLE void checkPdf2HtmlEXAsync() const;
    Q_INVOKABLE bool findPdf2HtmlEX();
    Q_INVOKABLE QString downloadUrl() const;
    Q_INVOKABLE void openDownloadPage() const;

    Q_INVOKABLE int convertPdf(const QString &pdfPath);
    Q_INVOKABLE bool reconvertPdf(int docId);
    Q_INVOKABLE bool deleteDocument(int docId);
    Q_INVOKABLE bool openInBrowser(int docId);
    Q_INVOKABLE bool openHtmlFile(const QString &htmlPath);
    Q_INVOKABLE QVariantMap fileInfo(const QString &path) const;
    Q_INVOKABLE QVariantList documents();
    Q_INVOKABLE QVariantList scanHtmlFiles();
    Q_INVOKABLE void scanHtmlFilesAsync();
    Q_INVOKABLE QString buildKnowledgeBasePrompt();
    Q_INVOKABLE QString readHtmlPage(const QString &htmlPath, int page) const;
    Q_INVOKABLE void extractHtmlPageAsync(const QString &htmlPath, int page) const;

signals:
    void isConvertingChanged();
    void knowledgeBaseDirChanged();
    void pdf2HtmlEXPathChanged();
    void detectLogChanged();
    void conversionProgress(int docId, int percent, QString message);
    void conversionFinished(int docId, bool success, QString error);
    void documentsChanged();
    void htmlPageExtracted(const QString &htmlPath, int page, const QString &tmpPath);
    void htmlFilesScanned(const QVariantList &files);
    void pdf2HtmlEXAvailabilityChanged(bool available);

private slots:
    void onProcessReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    void setIsConverting(bool v);
    void appendDetectLog(const QString &line);
    bool ensureKnowledgeBaseDir();
    QVariantList scanHtmlFiles(const QString &base, const QHash<QString, QString> &dirToTitle) const;
    int insertDocument(const QString &title, const QString &sourcePath);
    bool updateDocumentStatus(int docId, const QString &status, const QString &errorMsg = QString());
    bool updateDocumentOutput(int docId, const QString &outputDir, const QString &htmlPath, int pageCount);

    QProcess *m_process = nullptr;
    int m_currentDocId = -1;
    QString m_currentPdfPath;
    QString m_currentOutputDir;
    bool m_isConverting = false;
    QString m_detectLog;
    QString m_processOutput;
    QSettings m_settings;
};
