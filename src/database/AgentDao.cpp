#include "AgentDao.h"
#include "DatabaseManager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>

static QSqlDatabase db() { return DatabaseManager::instance().db(); }

// ============================================================
// Agent
// ============================================================
int AgentDao::insertAgent(const AgentData &a)
{
    QSqlQuery q(db());
    q.prepare(R"(INSERT INTO agents
        (name,provider,model_name,api_key,base_url,system_prompt,temperature,max_tokens,template_name)
        VALUES(:n,:p,:m,:k,:u,:s,:t,:x,:tn))");
    q.bindValue(":n",  a.name);
    q.bindValue(":p",  a.provider);
    q.bindValue(":m",  a.modelName);
    q.bindValue(":k",  a.apiKey);
    q.bindValue(":u",  a.baseUrl);
    q.bindValue(":s",  a.systemPrompt);
    q.bindValue(":t",  a.temperature);
    q.bindValue(":x",  a.maxTokens);
    q.bindValue(":tn", a.templateName);
    if (!q.exec()) { qWarning() << "insertAgent:" << q.lastError().text(); return -1; }
    return q.lastInsertId().toInt();
}

bool AgentDao::updateAgent(const AgentData &a)
{
    QSqlQuery q(db());
    q.prepare(R"(UPDATE agents SET
        name=:n,provider=:p,model_name=:m,api_key=:k,base_url=:u,
        system_prompt=:s,temperature=:t,max_tokens=:x,template_name=:tn,
        updated_at=datetime('now','localtime')
        WHERE id=:id)");
    q.bindValue(":n",  a.name);
    q.bindValue(":p",  a.provider);
    q.bindValue(":m",  a.modelName);
    q.bindValue(":k",  a.apiKey);
    q.bindValue(":u",  a.baseUrl);
    q.bindValue(":s",  a.systemPrompt);
    q.bindValue(":t",  a.temperature);
    q.bindValue(":x",  a.maxTokens);
    q.bindValue(":tn", a.templateName);
    q.bindValue(":id", a.id);
    if (!q.exec()) { qWarning() << "updateAgent:" << q.lastError().text(); return false; }
    return true;
}

bool AgentDao::deleteAgent(int id)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM agents WHERE id=:id");
    q.bindValue(":id", id);
    if (!q.exec()) { qWarning() << "deleteAgent:" << q.lastError().text(); return false; }
    return true;
}

static AgentData rowToAgent(QSqlQuery &q)
{
    AgentData a;
    a.id           = q.value("id").toInt();
    a.name         = q.value("name").toString();
    a.provider     = q.value("provider").toString();
    a.modelName    = q.value("model_name").toString();
    a.apiKey       = q.value("api_key").toString();
    a.baseUrl      = q.value("base_url").toString();
    a.systemPrompt = q.value("system_prompt").toString();
    a.temperature  = q.value("temperature").toDouble();
    a.maxTokens    = q.value("max_tokens").toInt();
    a.templateName = q.value("template_name").toString();
    a.createdAt    = q.value("created_at").toString();
    a.updatedAt    = q.value("updated_at").toString();
    return a;
}

AgentData AgentDao::getAgent(int id)
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM agents WHERE id=:id");
    q.bindValue(":id", id);
    q.exec();
    if (q.next()) return rowToAgent(q);
    return {};
}

QVector<AgentData> AgentDao::getAllAgents()
{
    QSqlQuery q("SELECT * FROM agents ORDER BY id DESC", db());
    QVector<AgentData> result;
    while (q.next()) result.append(rowToAgent(q));
    return result;
}

QVector<AgentData> AgentDao::searchAgents(const QString &keyword)
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM agents WHERE name LIKE :kw OR model_name LIKE :kw ORDER BY id DESC");
    q.bindValue(":kw", "%" + keyword + "%");
    q.exec();
    QVector<AgentData> result;
    while (q.next()) result.append(rowToAgent(q));
    return result;
}

// ============================================================
// Conversation
// ============================================================
int AgentDao::insertConversation(int agentId, const QString &title)
{
    QSqlQuery q(db());
    if (agentId >= 0) {
        q.prepare("INSERT INTO conversations (agent_id,title) VALUES(:a,:t)");
        q.bindValue(":a", agentId);
    } else {
        q.prepare("INSERT INTO conversations (agent_id,title) VALUES(NULL,:t)");
    }
    q.bindValue(":t", title);
    if (!q.exec()) { qWarning() << "insertConversation:" << q.lastError().text(); return -1; }
    return q.lastInsertId().toInt();
}

int AgentDao::insertGroupConversation(int groupId, const QString &title)
{
    QSqlQuery q(db());
    q.prepare("INSERT INTO conversations (agent_id,group_id,title) VALUES(NULL,:g,:t)");
    q.bindValue(":g", groupId);
    q.bindValue(":t", title);
    if (!q.exec()) { qWarning() << "insertGroupConversation:" << q.lastError().text(); return -1; }
    return q.lastInsertId().toInt();
}

bool AgentDao::deleteConversation(int convId)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM conversations WHERE id=:id");
    q.bindValue(":id", convId);
    return q.exec();
}

QVector<QPair<int,QString>> AgentDao::getConversations(int agentId)
{
    QSqlQuery q(db());
    q.prepare("SELECT id,title FROM conversations WHERE agent_id=:a ORDER BY id DESC");
    q.bindValue(":a", agentId);
    q.exec();
    QVector<QPair<int,QString>> result;
    while (q.next()) result.append({q.value(0).toInt(), q.value(1).toString()});
    return result;
}

// ============================================================
// Message
// ============================================================
int AgentDao::insertMessage(const MessageData &m)
{
    static bool s_checked = false;
    static bool s_hasModelName = false;

    if (!s_checked) {
        QSqlQuery cols(db());
        cols.exec("PRAGMA table_info(messages)");
        while (cols.next()) {
            if (cols.value(1).toString() == "model_name") {
                s_hasModelName = true;
                break;
            }
        }
        s_checked = true;
        qDebug() << "[DB] messages table has model_name:" << s_hasModelName;
    }

    QSqlQuery q(db());
    QString sql;
    if (s_hasModelName) {
        sql = "INSERT INTO messages (conversation_id,role,content,tokens_in,tokens_out,model_name) VALUES(?,?,?,?,?,?)";
    } else {
        sql = "INSERT INTO messages (conversation_id,role,content,tokens_in,tokens_out) VALUES(?,?,?,?,?)";
    }

    if (!q.prepare(sql)) {
        qWarning() << "insertMessage prepare failed:" << q.lastError().text() << "SQL:" << sql;
        return -1;
    }
    q.addBindValue(m.conversationId);
    q.addBindValue(m.role);
    q.addBindValue(m.content);
    q.addBindValue(m.tokensIn);
    q.addBindValue(m.tokensOut);
    if (s_hasModelName) q.addBindValue(m.modelName);

    if (!q.exec()) {
        qWarning() << "insertMessage exec failed:" << q.lastError().text();
        return -1;
    }
    return q.lastInsertId().toInt();
}

QVector<MessageData> AgentDao::getMessages(int conversationId)
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM messages WHERE conversation_id=? ORDER BY id ASC");
    q.addBindValue(conversationId);
    q.exec();
    QVector<MessageData> result;
    while (q.next()) {
        MessageData m;
        m.id             = q.value("id").toInt();
        m.conversationId = q.value("conversation_id").toInt();
        m.role           = q.value("role").toString();
        m.content        = q.value("content").toString();
        m.tokensIn       = q.value("tokens_in").toInt();
        m.tokensOut      = q.value("tokens_out").toInt();
        // model_name may not exist in old schema — use column index safely
        const int modelIdx = q.record().indexOf("model_name");
        m.modelName      = modelIdx >= 0 ? q.value(modelIdx).toString() : "";
        m.createdAt      = q.value("created_at").toString();
        result.append(m);
    }
    return result;
}

bool AgentDao::deleteMessagesByConversation(int conversationId)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM messages WHERE conversation_id=:c");
    q.bindValue(":c", conversationId);
    return q.exec();
}

// ============================================================
// Log
// ============================================================
bool AgentDao::insertLog(int agentId, int convId, const QString &eventType,
                         const QString &modelName, int tokIn, int tokOut,
                         int latencyMs, const QString &errMsg)
{
    QSqlQuery q(db());
    q.prepare(R"(INSERT INTO logs
        (agent_id,conversation_id,event_type,model_name,tokens_in,tokens_out,latency_ms,error_message)
        VALUES(:a,:c,:e,:m,:ti,:to,:l,:err))");
    q.bindValue(":a",   agentId);
    q.bindValue(":c",   convId);
    q.bindValue(":e",   eventType);
    q.bindValue(":m",   modelName);
    q.bindValue(":ti",  tokIn);
    q.bindValue(":to",  tokOut);
    q.bindValue(":l",   latencyMs);
    q.bindValue(":err", errMsg);
    return q.exec();
}

QVector<QVariantMap> AgentDao::getLogs(int limit)
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM logs ORDER BY id DESC LIMIT :l");
    q.bindValue(":l", limit);
    q.exec();
    QVector<QVariantMap> result;
    while (q.next()) {
        QVariantMap m;
        m["id"]           = q.value("id");
        m["agent_id"]     = q.value("agent_id");
        m["event_type"]   = q.value("event_type");
        m["model_name"]   = q.value("model_name");
        m["tokens_in"]    = q.value("tokens_in");
        m["tokens_out"]   = q.value("tokens_out");
        m["latency_ms"]   = q.value("latency_ms");
        m["error_message"]= q.value("error_message");
        m["created_at"]   = q.value("created_at");
        result.append(m);
    }
    return result;
}

// ============================================================
// Template
// ============================================================
bool AgentDao::insertTemplate(const TemplateData &t)
{
    QSqlQuery q(db());
    q.prepare(R"(INSERT INTO templates
        (name,description,system_prompt,provider,model_name,temperature,is_builtin)
        VALUES(:n,:d,:s,:p,:m,:t,:b))");
    q.bindValue(":n", t.name);
    q.bindValue(":d", t.description);
    q.bindValue(":s", t.systemPrompt);
    q.bindValue(":p", t.provider);
    q.bindValue(":m", t.modelName);
    q.bindValue(":t", t.temperature);
    q.bindValue(":b", t.isBuiltin ? 1 : 0);
    return q.exec();
}

QVector<TemplateData> AgentDao::getAllTemplates()
{
    QSqlQuery q("SELECT * FROM templates ORDER BY id ASC", db());
    QVector<TemplateData> result;
    while (q.next()) {
        TemplateData t;
        t.id           = q.value("id").toInt();
        t.name         = q.value("name").toString();
        t.description  = q.value("description").toString();
        t.systemPrompt = q.value("system_prompt").toString();
        t.provider     = q.value("provider").toString();
        t.modelName    = q.value("model_name").toString();
        t.temperature  = q.value("temperature").toDouble();
        t.isBuiltin    = q.value("is_builtin").toInt() != 0;
        result.append(t);
    }
    return result;
}

TemplateData AgentDao::getTemplateByName(const QString &name)
{
    QSqlQuery q(db());
    q.prepare("SELECT * FROM templates WHERE name=:n LIMIT 1");
    q.bindValue(":n", name);
    q.exec();
    if (q.next()) {
        TemplateData t;
        t.id           = q.value("id").toInt();
        t.name         = q.value("name").toString();
        t.description  = q.value("description").toString();
        t.systemPrompt = q.value("system_prompt").toString();
        t.provider     = q.value("provider").toString();
        t.modelName    = q.value("model_name").toString();
        t.temperature  = q.value("temperature").toDouble();
        t.isBuiltin    = q.value("is_builtin").toInt() != 0;
        return t;
    }
    return {};
}

int AgentDao::templateCount()
{
    QSqlQuery q("SELECT COUNT(*) FROM templates", db());
    if (q.next()) return q.value(0).toInt();
    return 0;
}

// ============================================================
// Group
// ============================================================
int AgentDao::insertGroup(const QString &name)
{
    QSqlQuery q(db());
    q.prepare("INSERT INTO groups (name) VALUES(:n)");
    q.bindValue(":n", name);
    if (!q.exec()) { qWarning() << "insertGroup:" << q.lastError().text(); return -1; }
    return q.lastInsertId().toInt();
}

bool AgentDao::updateGroup(int groupId, const QString &name)
{
    QSqlQuery q(db());
    q.prepare("UPDATE groups SET name=:n WHERE id=:id");
    q.bindValue(":n", name);
    q.bindValue(":id", groupId);
    return q.exec();
}

bool AgentDao::deleteGroup(int groupId)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM groups WHERE id=:id");
    q.bindValue(":id", groupId);
    return q.exec();
}

QVariantList AgentDao::getGroups()
{
    QSqlQuery q(db());
    q.exec(R"(SELECT g.id, g.name, g.created_at,
                   (SELECT COUNT(*) FROM group_agents ga WHERE ga.group_id = g.id) AS agent_count
            FROM groups g ORDER BY g.id DESC)");
    QVariantList result;
    while (q.next()) {
        QVariantMap m;
        m["id"]         = q.value(0);
        m["name"]       = q.value(1);
        m["createdAt"]  = q.value(2);
        m["agentCount"] = q.value(3);
        result.append(m);
    }
    return result;
}

QVariantList AgentDao::getGroupAgents(int groupId)
{
    QSqlQuery q(db());
    q.prepare(R"(SELECT a.id, a.name, a.provider, a.model_name
                 FROM agents a
                 JOIN group_agents ga ON ga.agent_id = a.id
                 WHERE ga.group_id = :gid
                 ORDER BY a.name)");
    q.bindValue(":gid", groupId);
    q.exec();
    QVariantList result;
    while (q.next()) {
        QVariantMap m;
        m["id"]         = q.value(0);
        m["name"]       = q.value(1);
        m["provider"]   = q.value(2);
        m["modelName"]  = q.value(3);
        result.append(m);
    }
    return result;
}

// ============================================================
// Group-Agent relation
// ============================================================
bool AgentDao::addAgentToGroup(int groupId, int agentId)
{
    QSqlQuery q(db());
    q.prepare("INSERT OR IGNORE INTO group_agents (group_id, agent_id) VALUES(:g,:a)");
    q.bindValue(":g", groupId);
    q.bindValue(":a", agentId);
    return q.exec();
}

bool AgentDao::removeAgentFromGroup(int groupId, int agentId)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM group_agents WHERE group_id=:g AND agent_id=:a");
    q.bindValue(":g", groupId);
    q.bindValue(":a", agentId);
    return q.exec();
}

bool AgentDao::setGroupAgents(int groupId, const QVector<int> &agentIds)
{
    QSqlQuery q(db());
    q.prepare("DELETE FROM group_agents WHERE group_id=:g");
    q.bindValue(":g", groupId);
    if (!q.exec()) return false;
    for (int aid : agentIds) {
        q.prepare("INSERT OR IGNORE INTO group_agents (group_id, agent_id) VALUES(:g,:a)");
        q.bindValue(":g", groupId);
        q.bindValue(":a", aid);
        if (!q.exec()) return false;
    }
    return true;
}
