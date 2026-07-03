#pragma once
#include <QString>

struct AgentData {
    int     id           = 0;
    QString name;
    QString provider;       // "openai" | "deepseek" | "ollama"
    QString modelName;
    QString apiKey;
    QString baseUrl;
    QString systemPrompt;
    double  temperature  = 0.7;
    int     maxTokens    = 4096;
    QString templateName;
    QString createdAt;
    QString updatedAt;
};
