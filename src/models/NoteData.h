#pragma once
#include <QString>

struct NoteData {
    int     id            = -1;
    QString title;
    QString content;
    QString sourceFile;
    int     agentId       = -1;
    QString createdAt;
};
