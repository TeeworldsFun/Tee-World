/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include "Z.h"

CGameControllerZ::CGameControllerZ(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
	m_pGameName = "[T:W]-Z";
}

void CGameControllerZ::Tick()
{
	// this is the main part of the gamemode, this function is run every tick

	IGameController::Tick();
}
