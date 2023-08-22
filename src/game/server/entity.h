/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITY_H
#define GAME_SERVER_ENTITY_H

#include "alloc.h"
#include "gameworld.h"

/*
	Class: Entity
		Basic entity class.
*/
class CEntity
{
	MACRO_ALLOC_HEAP()

private:
	/* Friend classes */
	friend class CGameWorld; // for entity list handling

	/* Identity */
	class CGameWorld *m_pGameWorld;

	CEntity *m_pPrevTypeEntity;
	CEntity *m_pNextTypeEntity;

	int m_ID;
	int m_ObjType;

	/*
		Variable: m_ProximityRadius
			Contains the physical size of the entity.
	*/
	float m_ProximityRadius;

	/* State */
	bool m_MarkedForDestroy;

protected:
	/* State */

	/*
		Variable: m_Pos, m_PosTo
			Contains the current posititon of the entity.
	*/
	vec2 m_Pos;
	vec2 m_PosTo;

	/* Getters */
	int GetID() const					{ return m_ID; }

public:
	/* Constructor */
	CEntity(CGameWorld *pGameWorld, int Objtype, vec2 Pos, int ProximityRadius=0);

	/* Destructor */
	virtual ~CEntity();

	/* Objects */
	class CGameWorld *GameWorld() const { return m_pGameWorld; }
	class CGameContext *GameServer() const { return m_pGameWorld->GameServer(); }
	class IServer *Server() const { return m_pGameWorld->Server(); }

	/* Getters */
	CEntity *TypeNext() const { return m_pNextTypeEntity; }
	CEntity *TypePrev() const { return m_pPrevTypeEntity; }
	const vec2 &GetPos() const			{ return m_Pos; }
	const vec2 &GetPosTo() const			{ return m_PosTo; }
	float GetProximityRadius() const	{ return m_ProximityRadius; }
	bool IsMarkedForDestroy() const		{ return m_MarkedForDestroy; }

	/* Setters */
	void MarkForDestroy()				{ m_MarkedForDestroy = true; }

	/* Other functions */

	/*
		Function: Destroy
			Destroys the entity.
	*/
	virtual void Destroy() { delete this; }

	/*
		Function: Reset
			Called when the game resets the map. Puts the entity
			back to its starting state or perhaps destroys it.
	*/
	virtual void Reset() {}

	/*
		Function: Tick
			Called to progress the entity to the next tick. Updates
			and moves the entity to its new state and position.
	*/
	virtual void Tick() {}

	/*
		Function: TickDefered
			Called after all entities Tick() function has been called.
	*/
	virtual void TickDefered() {}

	/*
		Function: TickPaused
			Called when the game is paused, to freeze the state and position of the entity.
	*/
	virtual void TickPaused() {}

	/*
		Function: snap
			Called when a new snapshot is being generated for a specific
			client.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.
	*/
	virtual void Snap(int SnappingClient) {}

	/*
		Function: PostSnap
			Called after all entities Snap(int SnappingClient) function has been called.
	*/
	virtual void PostSnap() {}


	/*
		Function: networkclipped(int snapping_client)
			Performs a series of test to see if a client can see the
			entity.

		Arguments:
			SnappingClient - ID of the client which snapshot is
				being generated. Could be -1 to create a complete
				snapshot of everything in the game for demo
				recording.

		Returns:
			Non-zero if the entity doesn't have to be in the snapshot.
	*/
	int NetworkClipped(int SnappingClient) const;
	int NetworkClipped(int SnappingClient, vec2 CheckPos) const;

	bool GameLayerClipped(vec2 CheckPos) const;
};

/*
	Class: CFlashingTick
		CEntityComponent.
*/
class CFlashingTick
{
	int *m_LifeSpan;
	int m_Timer;
	bool m_Flashing;

public:
	CFlashingTick() = default;

	void InitFlashing(int* EntityLifeSpan) { m_LifeSpan = EntityLifeSpan; }
	bool IsFlashing() const { return m_Flashing; }

	void OnTick()
	{
		if((*m_LifeSpan) < 150)
		{
			m_Timer--;
			if(m_Timer > 5)
				m_Flashing = true;
			else
			{
				m_Flashing = false;
				if(m_Timer <= 0)
					m_Timer = 0;
			}
		}
	}
};

#endif
