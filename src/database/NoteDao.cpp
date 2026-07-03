#include "NoteDao.h"
#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

static QSqlDatabase db() { return DatabaseManager::instance().db(); }

NoteDao::NoteDao() {}

int NoteDao::insertNote(const NoteData &note)
{
    QSqlQuery q(db());
    q.prepare(R"(INSERT INTO notes
        (title, content, source_file, agent_id)
        VALUES(:t, :c, :s, :a))");
    q.bindValue(":t", note.title);
    q.bindValue(":c", note.content);
    q.bindValue(":s", note.sourceFile);
    q.bindValue(":a", note.agentId >= 0 ? note.agentId : QVariant(QVariant::Int));
    if (!q.exec()) {
        qWarning() << "insertNote:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool NoteDao::deleteNote(int id)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM notes WHERE id=:id");
    q.bindValue(":id", id);
    return q.exec();
}

QList<NoteData> NoteDao::getNotes(int limit) const
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM notes ORDER BY id DESC LIMIT :l");
    q.bindValue(":l", limit);
    q.exec();
    QList<NoteData> result;
    while (q.next()) {
        NoteData n;
        n.id         = q.value("id").toInt();
        n.title      = q.value("title").toString();
        n.content    = q.value("content").toString();
        n.sourceFile = q.value("source_file").toString();
        n.agentId    = q.value("agent_id").toInt();
        n.createdAt  = q.value("created_at").toString();
        result.append(n);
    }
    return result;
}

QList<NoteData> NoteDao::getNotesByAgent(int agentId, int limit) const
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM notes WHERE agent_id=:a ORDER BY id DESC LIMIT :l");
    q.bindValue(":a", agentId);
    q.bindValue(":l", limit);
    q.exec();
    QList<NoteData> result;
    while (q.next()) {
        NoteData n;
        n.id         = q.value("id").toInt();
        n.title      = q.value("title").toString();
        n.content    = q.value("content").toString();
        n.sourceFile = q.value("source_file").toString();
        n.agentId    = q.value("agent_id").toInt();
        n.createdAt  = q.value("created_at").toString();
        result.append(n);
    }
    return result;
}
