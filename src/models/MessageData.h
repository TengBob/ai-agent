#pragma once
#include <QString>

struct MessageData {
    int     id             = 0;
    int     conversationId = 0;
    QString role;           // "user" | "assistant" | "system"
    QString content;
    int     tokensIn       = 0;
    int     tokensOut      = 0;
    QString modelName;
    QString createdAt;
};
