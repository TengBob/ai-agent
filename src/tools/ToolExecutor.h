#pragma once
#include <QObject>
#include <QJsonObject>
#include <QString>

class PdfConverter;

// Executes tool calls requested by the LLM and returns the result string
class ToolExecutor : public QObject
{
    Q_OBJECT
public:
    explicit ToolExecutor(QObject *parent = nullptr);

    // Execute a tool by name with given JSON arguments; returns result string
    QString execute(const QString &toolName, const QJsonObject &args);

    // Working directory for file operations and commands
    void setWorkDir(const QString &dir) { m_workDir = dir; }
    QString workDir() const { return m_workDir; }

    // Tavily API key for web search
    void setTavilyKey(const QString &key) { m_tavilyKey = key; }

    // Global PdfConverter instance used by the convert_pdf_to_html tool
    static void setPdfConverter(PdfConverter *converter);

signals:
    void toolStarted(const QString &toolName, const QString &argsJson);
    void toolFinished(const QString &toolName, const QString &result);

private:
    QString webSearch(const QJsonObject &args);
    QString readFile(const QJsonObject &args);
    QString writeFile(const QJsonObject &args);
    QString runCommand(const QJsonObject &args);
    QString httpRequest(const QJsonObject &args);
    QString convertPdfToHtml(const QJsonObject &args);
    QString readHtmlPage(const QJsonObject &args);

    QString m_workDir;
    QString m_tavilyKey;
    static PdfConverter *s_pdfConverter;
};
