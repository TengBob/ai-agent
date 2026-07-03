#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

// Describes one tool that the agent can call
struct ToolDef {
    QString     name;
    QString     description;
    QJsonObject parameters;   // JSON Schema object
};

class ToolRegistry
{
public:
    static ToolRegistry &instance();

    void registerTool(const ToolDef &def);
    QJsonArray toOpenAITools() const;          // [{type:"function", function:{...}}]
    QStringList toolNames() const;

private:
    ToolRegistry();
    QVector<ToolDef> m_tools;
};
