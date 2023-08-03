/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include "def.h"

CGameControllerDEF::CGameControllerDEF(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	// Exchange this to a string that identifies your game mode.
	// DM, TDM and CTF are reserved for teeworlds original modes.
	m_pGameType = "zDef";

	m_GameFlags = GAMEFLAG_TEAMS;
}

void CGameControllerDEF::Tick()
{
	// this is the main part of the gamemode, this function is run every tick

	IGameController::Tick();
}
