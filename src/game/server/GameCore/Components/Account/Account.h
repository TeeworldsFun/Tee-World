#ifndef GAME_SERVER_GAMECORE_ACCOUNT_H
#define GAME_SERVER_GAMECORE_ACCOUNT_H

#include <game/server/GameCore/TWorldComponent.h>
#include <game/server/gamecontext.h>

#include <game/server/GameCore/Database/DB.h>

class CAccount : public TWorldComponent
{
private:
    CDB *m_pDB;

public:
    ~CAccount() {};

    virtual void OnInitWorld();
    bool Register(int ClientID, const char *Username, const char *Password);
    bool Login(int ClientID, const char *Username, const char *Password);

private:
    CDB *DB() const { return m_pDB; }
};

#endif