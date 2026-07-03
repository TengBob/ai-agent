#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager inst;
    return inst;
}

bool DatabaseManager::initialize()
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/agents.db";

    m_db = QSqlDatabase::addDatabase("QSQLITE", "main_connection");
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        qWarning() << "DatabaseManager: cannot open DB:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery q(m_db);
    q.exec("PRAGMA foreign_keys = OFF");
    return createTables();
}

bool DatabaseManager::createTables()
{
    QSqlQuery q(m_db);

    const QStringList ddl = {
        R"(CREATE TABLE IF NOT EXISTS agents (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            provider TEXT NOT NULL,
            model_name TEXT NOT NULL,
            api_key TEXT NOT NULL,
            base_url TEXT DEFAULT '',
            system_prompt TEXT DEFAULT '',
            temperature REAL DEFAULT 0.7,
            max_tokens INTEGER DEFAULT 4096,
            template_name TEXT DEFAULT '',
            created_at TEXT DEFAULT (datetime('now','localtime')),
            updated_at TEXT DEFAULT (datetime('now','localtime'))
        ))",
        R"(CREATE TABLE IF NOT EXISTS conversations (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id INTEGER,
            group_id INTEGER DEFAULT -1,
            title TEXT DEFAULT '新对话',
            created_at TEXT DEFAULT (datetime('now','localtime')),
            updated_at TEXT DEFAULT (datetime('now','localtime'))
        ))",
        R"(CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            conversation_id INTEGER NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            tokens_in INTEGER DEFAULT 0,
            tokens_out INTEGER DEFAULT 0,
            model_name TEXT DEFAULT '',
            created_at TEXT DEFAULT (datetime('now','localtime')),
            FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
        ))",
        R"(CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id INTEGER,
            conversation_id INTEGER,
            event_type TEXT NOT NULL,
            model_name TEXT DEFAULT '',
            tokens_in INTEGER DEFAULT 0,
            tokens_out INTEGER DEFAULT 0,
            latency_ms INTEGER DEFAULT 0,
            error_message TEXT DEFAULT '',
            created_at TEXT DEFAULT (datetime('now','localtime'))
        ))",
        R"(CREATE TABLE IF NOT EXISTS templates (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            description TEXT DEFAULT '',
            system_prompt TEXT NOT NULL,
            provider TEXT DEFAULT '',
            model_name TEXT DEFAULT '',
            temperature REAL DEFAULT 0.7,
            is_builtin INTEGER DEFAULT 1
        ))",
        R"(CREATE TABLE IF NOT EXISTS groups (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        ))",
        R"(CREATE TABLE IF NOT EXISTS group_agents (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            group_id INTEGER NOT NULL,
            agent_id INTEGER NOT NULL,
            FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE,
            FOREIGN KEY (agent_id) REFERENCES agents(id) ON DELETE CASCADE,
            UNIQUE(group_id, agent_id)
        ))",
        R"(CREATE TABLE IF NOT EXISTS documents (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            source_path TEXT DEFAULT '',
            output_dir TEXT DEFAULT '',
            html_path TEXT DEFAULT '',
            page_count INTEGER DEFAULT 0,
            status TEXT DEFAULT 'pending',
            error_message TEXT DEFAULT '',
            created_at TEXT DEFAULT (datetime('now','localtime'))
        ))",
        R"(CREATE TABLE IF NOT EXISTS notes (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT DEFAULT '',
            content TEXT NOT NULL,
            source_file TEXT DEFAULT '',
            agent_id INTEGER,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        ))",
    };

    // migration: recreate conversations table to allow nullable agent_id + group_id
    {
        // Clean up any leftover from a previous failed migration
        q.exec("DROP TABLE IF EXISTS conversations_old");

        QSqlQuery cols(m_db);
        cols.exec("PRAGMA table_info(conversations)");
        bool hasGroupId = false;
        bool agentIdNotNull = false;
        while (cols.next()) {
            const QString cname = cols.value(1).toString();
            const QString cnotnull = cols.value(3).toString();
            if (cname == "group_id") hasGroupId = true;
            if (cname == "agent_id" && cnotnull == "1") agentIdNotNull = true;
        }

        qDebug() << "[DB migration] conversations: hasGroupId=" << hasGroupId
                 << "agentIdNotNull=" << agentIdNotNull;

        if (agentIdNotNull || !hasGroupId) {
            // Disable FK during migration — renaming conversations would update
            // messages FK to point to conversations_old, breaking it when dropped
            q.exec("PRAGMA foreign_keys = OFF");
            qDebug() << "[DB migration] running conversations table migration...";

            q.exec("ALTER TABLE conversations RENAME TO conversations_old");
            q.exec(R"(CREATE TABLE conversations (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                agent_id INTEGER,
                group_id INTEGER DEFAULT -1,
                title TEXT DEFAULT '新对话',
                created_at TEXT DEFAULT (datetime('now','localtime')),
                updated_at TEXT DEFAULT (datetime('now','localtime'))
            ))");
            q.exec("INSERT INTO conversations SELECT id, agent_id, -1, title, created_at, updated_at FROM conversations_old");
            q.exec("DROP TABLE conversations_old");

            q.exec("PRAGMA foreign_keys = OFF");
            qDebug() << "[DB migration] conversations table migration done";
        }
    }

    // migration: add model_name column to messages if missing
    {
        QSqlQuery cols(m_db);
        cols.exec("PRAGMA table_info(messages)");
        bool hasModelName = false;
        while (cols.next()) {
            if (cols.value(1).toString() == "model_name") hasModelName = true;
        }
        if (!hasModelName) {
            q.exec("ALTER TABLE messages ADD COLUMN model_name TEXT DEFAULT ''");
        }
    }

    // migration: fix messages table FK if broken (references conversations_old from a previous migration)
    {
        QSqlQuery chk(m_db);
        chk.exec("SELECT sql FROM sqlite_master WHERE type='table' AND name='messages'");
        if (chk.next()) {
            const QString sqlText = chk.value(0).toString();
            if (sqlText.contains("conversations_old", Qt::CaseInsensitive)) {
                qDebug() << "[DB migration] messages table FK is broken, recreating...";
                q.exec("ALTER TABLE messages RENAME TO messages_old");
                q.exec(R"(CREATE TABLE messages (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    conversation_id INTEGER NOT NULL,
                    role TEXT NOT NULL,
                    content TEXT NOT NULL,
                    tokens_in INTEGER DEFAULT 0,
                    tokens_out INTEGER DEFAULT 0,
                    model_name TEXT DEFAULT '',
                    created_at TEXT DEFAULT (datetime('now','localtime')),
                    FOREIGN KEY (conversation_id) REFERENCES conversations(id) ON DELETE CASCADE
                ))");
                q.exec("INSERT INTO messages (id, conversation_id, role, content, tokens_in, tokens_out, model_name, created_at) "
                       "SELECT id, conversation_id, role, content, tokens_in, tokens_out, model_name, created_at FROM messages_old");
                q.exec("DROP TABLE messages_old");
                qDebug() << "[DB migration] messages table recreated with correct FK";
            }
        }
    }

    for (const QString &sql : ddl) {
        if (!q.exec(sql)) {
            qWarning() << "createTables error:" << q.lastError().text();
            return false;
        }
    }

    // Debug: show conversation group_id status
    {
        QSqlQuery dq(m_db);
        dq.exec("SELECT id, agent_id, group_id, title FROM conversations ORDER BY id DESC LIMIT 10");
        while (dq.next()) {
            qDebug() << "[DB] conv id:" << dq.value(0).toInt()
                     << "agent_id:" << dq.value(1).toString()
                     << "group_id:" << dq.value(2).toInt()
                     << "title:" << dq.value(3).toString();
        }
    }

    return true;
}
