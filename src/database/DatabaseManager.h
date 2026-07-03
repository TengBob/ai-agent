#pragma once
#include <QSqlDatabase>
#include <QString>

class DatabaseManager
{
public:
    static DatabaseManager &instance();

    bool initialize();
    QSqlDatabase db() const { return m_db; }

private:
    DatabaseManager() = default;
    bool createTables();

    QSqlDatabase m_db;
};
