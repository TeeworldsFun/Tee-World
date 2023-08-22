/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "entity.h"
#include "gamecontext.h"

CEntity::CEntity(CGameWorld *pGameWorld, int ObjType, vec2 Pos, int ProximityRadius)
{
	m_pGameWorld = pGameWorld;

	m_pPrevTypeEntity = nullptr;
	m_pNextTypeEntity = nullptr;

	m_ID = Server()->SnapNewID();
	m_ObjType = ObjType;

	m_ProximityRadius = ProximityRadius;
	m_MarkedForDestroy = false;

	m_Pos = Pos;
	m_PosTo = Pos;
}

CEntity::~CEntity()
{
	GameWorld()->RemoveEntity(this);
	Server()->SnapFreeID(m_ID);
}

int CEntity::NetworkClipped(int SnappingClient) const
{
	return NetworkClipped(SnappingClient, m_Pos);
}

int CEntity::NetworkClipped(int SnappingClient, vec2 CheckPos) const
{
	if(SnappingClient == -1)
		return 0;

	const float dx = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.x-CheckPos.x;
	const float dy = GameServer()->m_apPlayers[SnappingClient]->m_ViewPos.y-CheckPos.y;

	if(absolute(dx) > 1000.0f || absolute(dy) > 800.0f)
		return 1;

	if(distance(GameServer()->m_apPlayers[SnappingClient]->m_ViewPos, CheckPos) > 1100.0f)
		return 1;
	return 0;
}

bool CEntity::GameLayerClipped(vec2 CheckPos) const
{
	const int rx = round_to_int(CheckPos.x) / 32;
	const int ry = round_to_int(CheckPos.y) / 32;
	return (rx < -200 || rx >= GameServer()->Collision()->GetWidth()+200)
			|| (ry < -200 || ry >= GameServer()->Collision()->GetHeight()+200);
}
