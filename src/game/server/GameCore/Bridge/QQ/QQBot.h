#ifndef GAME_SERVER_GAMECORE_BRIDGE_QQBOT_H
#define GAME_SERVER_GAMECORE_BRIDGE_QQBOT_H

#include <engine/shared/http.h>
#include <engine/shared/json.h>

class CQQBot
{
public:
    CQQBot();
    void Write();
    void Read();
};

#endif