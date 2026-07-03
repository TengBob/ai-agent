#pragma once
#include <QString>

struct TemplateData {
    int     id           = 0;
    QString name;
    QString description;
    QString systemPrompt;
    QString provider;
    QString modelName;
    double  temperature  = 0.7;
    bool    isBuiltin    = true;
};
