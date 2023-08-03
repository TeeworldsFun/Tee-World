#ifndef GAME_SERVER_GAMECORE_DATABASE_DB_H
#define GAME_SERVER_GAMECORE_DATABASE_DB_H

#include <mysql_connection.h>

#include <cppconn/driver.h>
#include <cppconn/exception.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

class CDB
{
public:
    sql::Driver *m_Driver;
    sql::Connection *m_Connection;
    sql::Statement *m_Statement;
    sql::ResultSet *m_Results;

public:
    CDB();
    ~CDB() {};

    bool Connect();
    void Disconnect();

    bool *Execute(const char *Sql);
    sql::ResultSet *ExecuteQuery(const char *Sql);

    void FreeData(sql::ResultSet *Result);

    LOCK SQL_Lock = 0;
};

#endif