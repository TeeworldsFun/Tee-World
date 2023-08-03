#include "Account.h"

CAccount::CAccount(CGameContext *pGameServer)
{
    m_pGameServer = pGameServer;
}

bool CAccount::Register(int ClientID, const char *Username, const char *Password)
{
    lock_wait(GameServer()->DB()->SQL_Lock);
    char aBuf[512];
    str_format(aBuf, sizeof(aBuf), "SELECT * from tw_Accounts WHERE Username = '%s';", Username);
    sql::ResultSet *Result;
    if (GameServer()->DB()->Connect())
    {
        try
        {
            Result = GameServer()->DB()->ExecuteQuery(aBuf);
            if (Result->next())
            {
                GameServer()->SendChatTarget(ClientID, _("This username is already in use."));
                GameServer()->DB()->FreeData(Result);
                lock_unlock(GameServer()->DB()->SQL_Lock);
                return false;
            }
            else
            {
                str_format(aBuf, sizeof(aBuf), "INSERT INTO tw_Accounts(Username, Password) VALUES ('%s', '%s');", Username, Password);
                GameServer()->DB()->Execute(aBuf);
                GameServer()->SendChatTarget(ClientID, _(""));
            }
        }
        catch (sql::SQLException &e)
        {
            dbg_msg("SQLError", "Error when Creating Account (%d) : %s", e.getErrorCode(), e.what());
        }
    }
    GameServer()->DB()->FreeData(Result);

    lock_unlock(GameServer()->DB()->SQL_Lock);
    return true;
}

bool CAccount::Login(int ClientID, const char *Username, const char *Password)
{
    lock_wait(GameServer()->DB()->SQL_Lock);
    char aBuf[512];
    str_format(aBuf, sizeof(aBuf), "SELECT * from tw_Accounts WHERE Username = '%s';", Username);
    sql::ResultSet *Result;
    if (GameServer()->DB()->Connect())
    {
        try
        {
            Result = GameServer()->DB()->ExecuteQuery(aBuf);
            if (Result->next())
            {
                GameServer()->SendChatTarget(ClientID, _("login"));
            }
            else
            {
                GameServer()->SendChatTarget(ClientID, _("This username is invaild."));
                GameServer()->DB()->FreeData(Result);
                lock_unlock(GameServer()->DB()->SQL_Lock);
                return false;
            }
        }
        catch (sql::SQLException &e)
        {
            dbg_msg("SQLError", "Error when Creating Account (%d) : %s", e.getErrorCode(), e.what());
        }
    }
    GameServer()->DB()->FreeData(Result);

    lock_unlock(GameServer()->DB()->SQL_Lock);
    return true;
}