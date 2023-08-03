#ifndef GAME_SERVER_GAMECORE_ACCOUNT_H
#define GAME_SERVER_GAMECORE_ACCOUNT_H

#include <game/server/gamecontext.h>

#include <game/server/GameCore/Database/DB.h>

class CAccount
{
private:
    CGameContext *m_pGameServer;
    CDB *m_pDB;

public:
    CAccount(CGameContext *pGameServer);
    ~CAccount() {};

    bool Register(int ClientID, const char *Username, const char *Password);
    bool Login(int ClientID, const char *Username, const char *Password);

private:
    CGameContext *GameServer() const { return m_pGameServer; }
    CDB *DB() const { return m_pDB; }
};

#endif