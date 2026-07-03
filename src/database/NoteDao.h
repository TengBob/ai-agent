#pragma once
#include <QList>
#include "models/NoteData.h"

class NoteDao
{
public:
    NoteDao();

    int  insertNote(const NoteData &note);
    bool deleteNote(int id);
    QList<NoteData> getNotes(int limit = 100) const;
    QList<NoteData> getNotesByAgent(int agentId, int limit = 100) const;
};
