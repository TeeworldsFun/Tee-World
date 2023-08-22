/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "character.h"
#include "laser.h"
#include "projectile.h"

// input count
struct CInputCount
{
	int m_Presses;
	int m_Releases;
};

CInputCount CountInput(int Prev, int Cur)
{
	CInputCount c = {0, 0};
	Prev &= INPUT_STATE_MASK;
	Cur &= INPUT_STATE_MASK;
	int i = Prev;

	while (i != Cur)
	{
		i = (i + 1) & INPUT_STATE_MASK;
		if (i & 1)
			c.m_Presses++;
		else
			c.m_Releases++;
	}

	return c;
}

MACRO_ALLOC_POOL_ID_IMPL(CCharacter, MAX_CLIENTS)

// Character, "physical" player's part
CCharacter::CCharacter(CGameWorld *pWorld)
	: CEntity(pWorld, CGameWorld::ENTTYPE_CHARACTER, vec2(0, 0), ms_PhysSize)
{
	m_Health = 0;
	m_Armor = 0;
}

void CCharacter::Reset()
{
	Destroy();
}

bool CCharacter::Spawn(CPlayer *pPlayer, vec2 Pos)
{
	m_EmoteStop = -1;
	m_LastAction = -1;
	m_LastNoAmmoSound = -1;
	m_ActiveWeapon = WEAPON_GUN;
	m_LastWeapon = WEAPON_HAMMER;
	m_QueuedWeapon = -1;

	m_pPlayer = pPlayer;
	m_Pos = Pos;

	m_Core.Reset();
	m_Core.Init(&GameServer()->m_World.m_Core, GameServer()->Collision());
	m_Core.m_Pos = m_Pos;
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = &m_Core;

	m_ReckoningTick = 0;
	mem_zero(&m_SendCore, sizeof(m_SendCore));
	mem_zero(&m_ReckoningCore, sizeof(m_ReckoningCore));
	m_Core.m_WorldID = m_pPlayer->GetPlayerWorldID();

	dbg_msg("h", "%d, %d", m_pPlayer->GetCID(), m_Core.m_WorldID);
	GameServer()->m_World.InsertEntity(this);
	m_Alive = true;
	m_NumInputs = 0;

	GameServer()->m_pController->OnCharacterSpawn(this);

	// Zomb2
	m_Move.m_JumpTimer = 0;
	m_Move.m_LastXTimer = 0;
	if (m_pPlayer->GetZomb())
	{
		if (m_pPlayer->GetZomb(6))
			IncreaseHealth(100);
		// Move
		int Rand = rand() % 2;
		if (Rand < 1)
			m_Input.m_Direction = -1;
		else
			m_Input.m_Direction = 1;
		m_Move.m_LastX = m_Pos.x;

		// Aim
		m_Aim.m_Angle = rand() % 15 * 12; // save some CPU, this can result n * 12, for n = 0 until n = 15, n = 0 -> 0�, n = 15 -> 180�
		m_Aim.m_Explode = false;
	}

	return true;
}

void CCharacter::Destroy()
{
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	m_Alive = false;
}

void CCharacter::SetWeapon(int W)
{
	if (W == m_ActiveWeapon)
		return;

	m_LastWeapon = m_ActiveWeapon;
	m_QueuedWeapon = -1;
	m_ActiveWeapon = W;
	GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SWITCH);

	if (m_ActiveWeapon < 0 || m_ActiveWeapon >= NUM_WEAPONS)
		m_ActiveWeapon = 0;
}

bool CCharacter::IsGrounded()
{
	if (GameServer()->Collision()->CheckPoint(m_Pos.x + GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;
	if (GameServer()->Collision()->CheckPoint(m_Pos.x - GetProximityRadius() / 2, m_Pos.y + GetProximityRadius() / 2 + 5))
		return true;

	return false;
}

void CCharacter::HandleNinja()
{
	if (m_ActiveWeapon != WEAPON_NINJA)
		return;

	if ((Server()->Tick() - m_Ninja.m_ActivationTick) > (g_pData->m_Weapons.m_Ninja.m_Duration * Server()->TickSpeed() / 1000) && !m_pPlayer->GetZomb(10))
	{
		// time's up, return
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = m_LastWeapon;

		SetWeapon(m_ActiveWeapon);
		return;
	}

	// force ninja Weapon
	SetWeapon(WEAPON_NINJA);

	m_Ninja.m_CurrentMoveTime--;

	if (m_Ninja.m_CurrentMoveTime == 0)
	{
		// reset velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * m_Ninja.m_OldVelAmount;
	}

	if (m_Ninja.m_CurrentMoveTime > 0)
	{
		// Set velocity
		m_Core.m_Vel = m_Ninja.m_ActivationDir * g_pData->m_Weapons.m_Ninja.m_Velocity;
		vec2 OldPos = m_Pos;
		GameServer()->Collision()->MoveBox(&m_Core.m_Pos, &m_Core.m_Vel, vec2(GetProximityRadius(), GetProximityRadius()), 0.f);

		// reset velocity so the client doesn't predict stuff
		m_Core.m_Vel = vec2(0.f, 0.f);

		// check if we Hit anything along the way
		{
			CCharacter *aEnts[MAX_CLIENTS];
			vec2 Dir = m_Pos - OldPos;
			float Radius = GetProximityRadius() * 2.0f;
			vec2 Center = OldPos + Dir * 0.5f;
			int Num = GameServer()->m_World.FindEntities(Center, Radius, (CEntity **)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

			for (int i = 0; i < Num; ++i)
			{
				if (aEnts[i] == this)
					continue;

				// make sure we haven't Hit this object before
				bool bAlreadyHit = false;
				for (int j = 0; j < m_NumObjectsHit; j++)
				{
					if (m_apHitObjects[j] == aEnts[i])
						bAlreadyHit = true;
				}
				if (bAlreadyHit)
					continue;

				// check so we are sufficiently close
				if (distance(aEnts[i]->m_Pos, m_Pos) > (GetProximityRadius() * 2.0f))
					continue;

				// Hit a player, give him damage and stuffs...
				GameServer()->CreateSound(aEnts[i]->m_Pos, SOUND_NINJA_HIT);
				// set his velocity to fast upward (for now)
				if (m_NumObjectsHit < 10)
					m_apHitObjects[m_NumObjectsHit++] = aEnts[i];

				aEnts[i]->TakeDamage(vec2(0, -10.0f), g_pData->m_Weapons.m_Ninja.m_pBase->m_Damage, m_pPlayer->GetCID(), WEAPON_NINJA);
			}
		}

		return;
	}

	return;
}

void CCharacter::DoWeaponSwitch()
{
	// make sure we can switch
	if (m_ReloadTimer != 0 || m_QueuedWeapon == -1 || m_aWeapons[WEAPON_NINJA].m_Got)
		return;

	// switch Weapon
	SetWeapon(m_QueuedWeapon);
}

void CCharacter::HandleWeaponSwitch()
{
	int WantedWeapon = m_ActiveWeapon;
	if (m_QueuedWeapon != -1)
		WantedWeapon = m_QueuedWeapon;

	// select Weapon
	int Next = CountInput(m_LatestPrevInput.m_NextWeapon, m_LatestInput.m_NextWeapon).m_Presses;
	int Prev = CountInput(m_LatestPrevInput.m_PrevWeapon, m_LatestInput.m_PrevWeapon).m_Presses;

	if (Next < 128) // make sure we only try sane stuff
	{
		while (Next) // Next Weapon selection
		{
			WantedWeapon = (WantedWeapon + 1) % NUM_WEAPONS;
			if (m_aWeapons[WantedWeapon].m_Got)
				Next--;
		}
	}

	if (Prev < 128) // make sure we only try sane stuff
	{
		while (Prev) // Prev Weapon selection
		{
			WantedWeapon = (WantedWeapon - 1) < 0 ? NUM_WEAPONS - 1 : WantedWeapon - 1;
			if (m_aWeapons[WantedWeapon].m_Got)
				Prev--;
		}
	}

	// Direct Weapon selection
	if (m_LatestInput.m_WantedWeapon)
		WantedWeapon = m_Input.m_WantedWeapon - 1;

	// check for insane values
	if (WantedWeapon >= 0 && WantedWeapon < NUM_WEAPONS && WantedWeapon != m_ActiveWeapon && m_aWeapons[WantedWeapon].m_Got)
		m_QueuedWeapon = WantedWeapon;

	DoWeaponSwitch();
}

void CCharacter::FireWeapon()
{
	if (m_ReloadTimer != 0)
		return;

	DoWeaponSwitch();
	vec2 Direction = normalize(vec2(m_LatestInput.m_TargetX, m_LatestInput.m_TargetY));

	bool FullAuto = false;
	if (m_ActiveWeapon == WEAPON_GRENADE || m_ActiveWeapon == WEAPON_SHOTGUN || m_ActiveWeapon == WEAPON_RIFLE)
		FullAuto = true;

	// check if we gonna fire
	bool WillFire = false;

	if (m_pPlayer->GetZomb() && m_Input.m_Fire == 1)
		WillFire = true;

	if (CountInput(m_LatestPrevInput.m_Fire, m_LatestInput.m_Fire).m_Presses)
		WillFire = true;

	if (FullAuto && (m_LatestInput.m_Fire & 1) && m_aWeapons[m_ActiveWeapon].m_Ammo)
		WillFire = true;

	if (!WillFire)
		return;

	// check for ammo
	if (!m_aWeapons[m_ActiveWeapon].m_Ammo)
	{
		// 125ms is a magical limit of how fast a human can click
		m_ReloadTimer = 125 * Server()->TickSpeed() / 1000;
		if (m_LastNoAmmoSound + Server()->TickSpeed() <= Server()->Tick())
		{
			GameServer()->CreateSound(m_Pos, SOUND_WEAPON_NOAMMO);
			m_LastNoAmmoSound = Server()->Tick();
		}
		return;
	}

	vec2 ProjStartPos = m_Pos + Direction * GetProximityRadius() * 0.75f;

	switch (m_ActiveWeapon)
	{
	case WEAPON_HAMMER:
	{
		// reset objects Hit
		m_NumObjectsHit = 0;
		GameServer()->CreateSound(m_Pos, SOUND_HAMMER_FIRE);

		CCharacter *apEnts[MAX_CLIENTS];
		int Hits = 0;
		int Num = GameServer()->m_World.FindEntities(ProjStartPos, GetProximityRadius() * 0.5f, (CEntity **)apEnts,
													 MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);

		for (int i = 0; i < Num; ++i)
		{
			CCharacter *pTarget = apEnts[i];

			if ((pTarget == this) || GameServer()->Collision()->IntersectLine(ProjStartPos, pTarget->m_Pos, NULL, NULL))
				continue;

			// set his velocity to fast upward (for now)
			if (length(pTarget->m_Pos - ProjStartPos) > 0.0f)
				GameServer()->CreateHammerHit(pTarget->m_Pos - normalize(pTarget->m_Pos - ProjStartPos) * GetProximityRadius() * 0.5f);
			else
				GameServer()->CreateHammerHit(ProjStartPos);

			vec2 Dir;
			if (length(pTarget->m_Pos - m_Pos) > 0.0f)
				Dir = normalize(pTarget->m_Pos - m_Pos);
			else
				Dir = vec2(0.f, -1.f);

			pTarget->TakeDamage(vec2(0.f, -1.f) + normalize(Dir + vec2(0.f, -1.1f)) * 10.0f, g_pData->m_Weapons.m_Hammer.m_pBase->m_Damage,
								m_pPlayer->GetCID(), m_ActiveWeapon);
			Hits++;
		}

		// if we Hit anything, we have to wait for the reload
		if (Hits)
			m_ReloadTimer = Server()->TickSpeed() / 3;
	}
	break;

	case WEAPON_GUN:
	{
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GUN,
											 m_pPlayer->GetCID(),
											 ProjStartPos,
											 Direction,
											 (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime),
											 1, 0, 0, -1, WEAPON_GUN);

		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
	}
	break;

	case WEAPON_SHOTGUN:
	{
		int ShotSpread = 2;

		for (int i = -ShotSpread; i <= ShotSpread; ++i)
		{
			float Spreading[] = {-0.185f, -0.070f, 0, 0.070f, 0.185f};
			float a = GetAngle(Direction);
			a += Spreading[i + 2];
			float v = 1 - (absolute(i) / (float)ShotSpread);
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
			CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_SHOTGUN,
												 m_pPlayer->GetCID(),
												 ProjStartPos,
												 vec2(cosf(a), sinf(a)) * Speed,
												 (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime),
												 1, 0, 0, -1, WEAPON_SHOTGUN);
		}

		GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
	}
	break;

	case WEAPON_GRENADE:
	{
		CProjectile *pProj = new CProjectile(GameWorld(), WEAPON_GRENADE,
											 m_pPlayer->GetCID(),
											 ProjStartPos,
											 Direction,
											 (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime),
											 1, true, 0, SOUND_GRENADE_EXPLODE, WEAPON_GRENADE);

		GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);
	}
	break;

	case WEAPON_RIFLE:
	{
		new CLaser(GameWorld(), m_Pos, Direction, GameServer()->Tuning()->m_LaserReach, m_pPlayer->GetCID());
		GameServer()->CreateSound(m_Pos, SOUND_RIFLE_FIRE);
	}
	break;

	case WEAPON_NINJA:
	{
		// reset Hit objects
		m_NumObjectsHit = 0;

		m_Ninja.m_ActivationDir = Direction;
		m_Ninja.m_CurrentMoveTime = g_pData->m_Weapons.m_Ninja.m_Movetime * Server()->TickSpeed() / 1000;
		m_Ninja.m_OldVelAmount = length(m_Core.m_Vel);

		GameServer()->CreateSound(m_Pos, SOUND_NINJA_FIRE);
	}
	break;
	}

	m_AttackTick = Server()->Tick();

	if (m_aWeapons[m_ActiveWeapon].m_Ammo > 0) // -1 == unlimited
		m_aWeapons[m_ActiveWeapon].m_Ammo--;

	if (!m_ReloadTimer)
		m_ReloadTimer = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Firedelay * Server()->TickSpeed() / 1000;
}

void CCharacter::HandleWeapons()
{
	// ninja
	HandleNinja();

	// check reload timer
	if (m_ReloadTimer)
	{
		m_ReloadTimer--;
		return;
	}

	// fire Weapon, if wanted
	FireWeapon();

	// ammo regen
	int AmmoRegenTime = g_pData->m_Weapons.m_aId[m_ActiveWeapon].m_Ammoregentime;
	if (AmmoRegenTime && !m_pPlayer->GetZomb())
	{
		// If equipped and not active, regen ammo?
		if (m_ReloadTimer <= 0)
		{
			if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart < 0)
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = Server()->Tick();

			if ((Server()->Tick() - m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart) >= AmmoRegenTime * Server()->TickSpeed() / 1000)
			{
				// Add some ammo
				m_aWeapons[m_ActiveWeapon].m_Ammo = min(m_aWeapons[m_ActiveWeapon].m_Ammo + 1, 10);
				m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
			}
		}
		else
		{
			m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart = -1;
		}
	}

	return;
}

bool CCharacter::GiveWeapon(int Weapon, int Ammo)
{
	if (m_aWeapons[Weapon].m_Ammo < g_pData->m_Weapons.m_aId[Weapon].m_Maxammo || !m_aWeapons[Weapon].m_Got)
	{
		m_aWeapons[Weapon].m_Got = true;
		m_aWeapons[Weapon].m_Ammo = min(g_pData->m_Weapons.m_aId[Weapon].m_Maxammo, Ammo);
		if (m_pPlayer->GetZomb())
			m_aWeapons[Weapon].m_Ammo = -1;
		return true;
	}
	return false;
}

void CCharacter::GiveNinja()
{
	m_Ninja.m_ActivationTick = Server()->Tick();
	m_aWeapons[WEAPON_NINJA].m_Got = true;
	m_aWeapons[WEAPON_NINJA].m_Ammo = -1;
	if (m_ActiveWeapon != WEAPON_NINJA)
		m_LastWeapon = m_ActiveWeapon;
	m_ActiveWeapon = WEAPON_NINJA;

	if (!m_pPlayer->GetZomb())
		GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
}

void CCharacter::SetEmote(int Emote, int Tick)
{
	m_EmoteType = Emote;
	m_EmoteStop = Tick;
}

void CCharacter::OnPredictedInput(CNetObj_PlayerInput *pNewInput)
{
	// check for changes
	if (mem_comp(&m_Input, pNewInput, sizeof(CNetObj_PlayerInput)) != 0)
		m_LastAction = Server()->Tick();

	// copy new input
	mem_copy(&m_Input, pNewInput, sizeof(m_Input));

	// it is not allowed to aim in the center
	if (m_Input.m_TargetX == 0 && m_Input.m_TargetY == 0)
		m_Input.m_TargetY = -1;
}

void CCharacter::OnDirectInput(CNetObj_PlayerInput *pNewInput)
{
	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
	mem_copy(&m_LatestInput, pNewInput, sizeof(m_LatestInput));
	m_NumInputs++;

	// it is not allowed to aim in the center
	if (m_LatestInput.m_TargetX == 0 && m_LatestInput.m_TargetY == 0)
		m_LatestInput.m_TargetY = -1;

	if (m_NumInputs > 1 && m_pPlayer->GetTeam() != TEAM_SPECTATORS)
	{
		HandleWeaponSwitch();
		FireWeapon();
	}

	mem_copy(&m_LatestPrevInput, &m_LatestInput, sizeof(m_LatestInput));
}

void CCharacter::ResetInput()
{
	m_Input.m_Direction = 0;
	m_Input.m_Hook = 0;
	// simulate releasing the fire button
	if ((m_Input.m_Fire & 1) != 0)
		m_Input.m_Fire++;
	m_Input.m_Fire &= INPUT_STATE_MASK;
	m_Input.m_Jump = 0;
	m_LatestPrevInput = m_LatestInput = m_Input;
}

void CCharacter::Tick()
{
	if (!m_pPlayer || !IsAlive()) // bugfix
		return;
	// Zomb2 I thought it would be better to divide this 3 things
	DoZombieMovement();
	if (!IsAlive()) // Boomer kills himself
		return;

	if (m_pPlayer->m_ForceBalanced)
	{
		char Buf[128];
		str_format(Buf, sizeof(Buf), "You were moved to %s due to team balancing", GameServer()->m_pController->GetTeamName(m_pPlayer->GetTeam()));
		GameServer()->SendBroadcast(Buf, m_pPlayer->GetCID());

		m_pPlayer->m_ForceBalanced = false;
	}

	m_Core.m_Input = m_Input;
	m_Core.Tick(true, m_pPlayer->GetNextTuningParams());

	// handle death-tiles and leaving gamelayer
	if (GameServer()->Collision()->GetCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) & CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) & CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) & CCollision::COLFLAG_DEATH ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) & CCollision::COLFLAG_DEATH ||
		GameLayerClipped(m_Pos))
	{
		Die(m_pPlayer->GetCID(), WEAPON_WORLD);
	}

	// handle Weapons
	HandleWeapons();

	if (GameServer()->Collision()->GetCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) & CCollision::COLFLAG_SOLID ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x + GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) & CCollision::COLFLAG_SOLID ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y - GetProximityRadius() / 3.f) & CCollision::COLFLAG_SOLID ||
		GameServer()->Collision()->GetCollisionAt(m_Pos.x - GetProximityRadius() / 3.f, m_Pos.y + GetProximityRadius() / 3.f) & CCollision::COLFLAG_SOLID ||
		GameLayerClipped(m_Pos))
		dbg_msg("sda", "yes");
	// Previnput
	m_PrevInput = m_Input;
	return;
}

void CCharacter::TickDefered()
{
	// advance the dummy
	{
		CWorldCore TempWorld;
		m_ReckoningCore.Init(&TempWorld, GameServer()->Collision());
		m_ReckoningCore.Tick(false, &TempWorld.m_Tuning);
		m_ReckoningCore.Move(&TempWorld.m_Tuning);
		m_ReckoningCore.Quantize();
	}

	// lastsentcore
	vec2 StartPos = m_Core.m_Pos;
	vec2 StartVel = m_Core.m_Vel;
	bool StuckBefore = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));

	m_Core.Move(m_pPlayer->GetNextTuningParams());
	bool StuckAfterMove = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Core.Quantize();
	bool StuckAfterQuant = GameServer()->Collision()->TestBox(m_Core.m_Pos, vec2(28.0f, 28.0f));
	m_Pos = m_Core.m_Pos;

	if (!StuckBefore && (StuckAfterMove || StuckAfterQuant))
	{
		// Hackish solution to get rid of strict-aliasing warning
		union
		{
			float f;
			unsigned u;
		} StartPosX, StartPosY, StartVelX, StartVelY;

		StartPosX.f = StartPos.x;
		StartPosY.f = StartPos.y;
		StartVelX.f = StartVel.x;
		StartVelY.f = StartVel.y;

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "STUCK!!! %d %d %d %f %f %f %f %x %x %x %x",
				   StuckBefore,
				   StuckAfterMove,
				   StuckAfterQuant,
				   StartPos.x, StartPos.y,
				   StartVel.x, StartVel.y,
				   StartPosX.u, StartPosY.u,
				   StartVelX.u, StartVelY.u);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
	}

	int Events = m_Core.m_TriggeredEvents;
	int Mask = CmaskAllExceptOne(m_pPlayer->GetCID());

	if (Events & COREEVENT_GROUND_JUMP)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_JUMP, Mask);

	if (Events & COREEVENT_HOOK_ATTACH_PLAYER)
		GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_PLAYER, CmaskAll());
	if (Events & COREEVENT_HOOK_ATTACH_GROUND)
		GameServer()->CreateSound(m_Pos, SOUND_HOOK_ATTACH_GROUND, Mask);
	if (Events & COREEVENT_HOOK_HIT_NOHOOK)
		GameServer()->CreateSound(m_Pos, SOUND_HOOK_NOATTACH, Mask);

	if (m_pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		m_Pos.x = m_Input.m_TargetX;
		m_Pos.y = m_Input.m_TargetY;
	}

	// update the m_SendCore if needed
	{
		CNetObj_Character Predicted;
		CNetObj_Character Current;
		mem_zero(&Predicted, sizeof(Predicted));
		mem_zero(&Current, sizeof(Current));
		m_ReckoningCore.Write(&Predicted);
		m_Core.Write(&Current);

		// only allow dead reackoning for a top of 3 seconds
		if (m_ReckoningTick + Server()->TickSpeed() * 3 < Server()->Tick() || mem_comp(&Predicted, &Current, sizeof(CNetObj_Character)) != 0)
		{
			m_ReckoningTick = Server()->Tick();
			m_SendCore = m_Core;
			m_ReckoningCore = m_Core;
		}
	}
}

void CCharacter::TickPaused()
{
	++m_AttackTick;
	++m_DamageTakenTick;
	++m_Ninja.m_ActivationTick;
	++m_ReckoningTick;
	if (m_LastAction != -1)
		++m_LastAction;
	if (m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart > -1)
		++m_aWeapons[m_ActiveWeapon].m_AmmoRegenStart;
	if (m_EmoteStop > -1)
		++m_EmoteStop;
}

bool CCharacter::IncreaseHealth(int Amount)
{
	if (m_Health >= 10 && !m_pPlayer->GetZomb(6) && !m_pPlayer->GetZomb(13))
		return false;
	m_Health = clamp(m_Health + Amount, 0, 100);
	return true;
}

bool CCharacter::IncreaseArmor(int Amount)
{
	if (m_Armor >= 10)
		return false;
	m_Armor = clamp(m_Armor + Amount, 0, 10);
	return true;
}

void CCharacter::Die(int Killer, int Weapon)
{
	// we got to wait 0.5 secs before respawning
	m_pPlayer->m_RespawnTick = Server()->Tick() + Server()->TickSpeed() / 2;
	int ModeSpecial = GameServer()->m_pController->OnCharacterDeath(this, GameServer()->m_apPlayers[Killer], Weapon);
	GameServer()->m_pBotEngine->OnCharacterDeath(m_pPlayer->GetCID(), Killer, Weapon);

	// send the kill message
	CNetMsg_Sv_KillMsg Msg;
	Msg.m_Killer = Killer;
	Msg.m_Victim = m_pPlayer->GetCID();
	Msg.m_Weapon = Weapon;
	Msg.m_ModeSpecial = ModeSpecial;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);

	// a nice sound
	GameServer()->CreateSound(m_Pos, SOUND_PLAYER_DIE);

	// this is for auto respawn after 3 secs
	m_pPlayer->m_DieTick = Server()->Tick();

	m_Alive = false;
	GameServer()->m_World.RemoveEntity(this);
	GameServer()->m_World.m_Core.m_apCharacters[m_pPlayer->GetCID()] = 0;
	GameServer()->CreateDeath(m_Pos, m_pPlayer->GetCID());

	if (m_pPlayer->m_Zomb)
		GameServer()->OnZombieKill(m_pPlayer->GetCID()); // remove the player to get a new one
}

bool CCharacter::TakeDamage(vec2 Force, int Dmg, int From, int Weapon)
{
	m_Core.m_Vel += Force;

	if (GameServer()->m_pController->IsFriendlyFire(m_pPlayer->GetCID(), From))
		return false;

	// m_pPlayer only inflicts half damage on self
	if (From == m_pPlayer->GetCID())
		Dmg = max(1, Dmg / 2);

	m_DamageTaken++;

	// create healthmod indicator
	if (Server()->Tick() < m_DamageTakenTick + 25)
	{
		// make sure that the damage indicators doesn't group together
		GameServer()->CreateDamageInd(m_Pos, m_DamageTaken * 0.25f, Dmg);
	}
	else
	{
		m_DamageTaken = 0;
		GameServer()->CreateDamageInd(m_Pos, 0, Dmg);
	}

	if (Dmg)
	{
		if (m_Armor)
		{
			if (Dmg > 1)
			{
				m_Health--;
				Dmg--;
			}

			if (Dmg > m_Armor)
			{
				Dmg -= m_Armor;
				m_Armor = 0;
			}
			else
			{
				m_Armor -= Dmg;
				Dmg = 0;
			}
		}

		m_Health -= Dmg;
	}

	m_DamageTakenTick = Server()->Tick();

	// do damage Hit sound
	if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
	{
		int64 Mask = CmaskOne(From);
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[i]->m_SpectatorID == From)
				Mask |= CmaskOne(i);
		}
		GameServer()->CreateSound(GameServer()->m_apPlayers[From]->m_ViewPos, SOUND_HIT, Mask);
	}

	// check for death
	if (m_Health <= 0)
	{
		// set attacker's face to happy (taunt!)
		if (From >= 0 && From != m_pPlayer->GetCID() && GameServer()->m_apPlayers[From])
		{
			CCharacter *pChr = GameServer()->m_apPlayers[From]->GetCharacter();
			if (pChr)
			{
				pChr->m_EmoteType = EMOTE_HAPPY;
				pChr->m_EmoteStop = Server()->Tick() + Server()->TickSpeed();
			}
		}

		// Zomb2 Swapped
		Die(From, Weapon);
		return false;
	}

	if (Dmg > 2)
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_LONG);
	else
		GameServer()->CreateSound(m_Pos, SOUND_PLAYER_PAIN_SHORT);

	m_EmoteType = EMOTE_PAIN;
	m_EmoteStop = Server()->Tick() + 500 * Server()->TickSpeed() / 1000;

	return true;
}

void CCharacter::Snap(int SnappingClient)
{
	int Id = m_pPlayer->GetCID();

	if (!Server()->Translate(Id, SnappingClient))
		return;

	if (NetworkClipped(SnappingClient))
		return;

	CNetObj_Character *pCharacter = static_cast<CNetObj_Character *>(Server()->SnapNewItem(NETOBJTYPE_CHARACTER, Id, sizeof(CNetObj_Character)));
	if (!pCharacter)
		return;

	// write down the m_Core
	if (!m_ReckoningTick || GameServer()->m_World.m_Paused)
	{
		// no dead reckoning when paused because the client doesn't know
		// how far to perform the reckoning
		pCharacter->m_Tick = 0;
		m_Core.Write(pCharacter);
	}
	else
	{
		pCharacter->m_Tick = m_ReckoningTick;
		m_SendCore.Write(pCharacter);
	}

	// set emote
	if (m_EmoteStop < Server()->Tick())
	{
		m_EmoteType = EMOTE_NORMAL;
		m_EmoteStop = -1;
	}

	if (pCharacter->m_HookedPlayer != -1)
	{
		if (!Server()->Translate(pCharacter->m_HookedPlayer, SnappingClient))
			pCharacter->m_HookedPlayer = -1;
	}

	pCharacter->m_Emote = m_EmoteType;

	pCharacter->m_AmmoCount = 0;
	pCharacter->m_Health = 0;
	pCharacter->m_Armor = 0;

	pCharacter->m_Weapon = m_ActiveWeapon;
	pCharacter->m_AttackTick = m_AttackTick;

	pCharacter->m_Direction = m_Input.m_Direction;

	if (m_pPlayer->GetCID() == SnappingClient || SnappingClient == -1 ||
		(!g_Config.m_SvStrictSpectateMode && m_pPlayer->GetCID() == GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID))
	{
		pCharacter->m_Health = m_Health;
		pCharacter->m_Armor = m_Armor;
		if (m_aWeapons[m_ActiveWeapon].m_Ammo > 0)
			pCharacter->m_AmmoCount = m_aWeapons[m_ActiveWeapon].m_Ammo;
	}

	if (pCharacter->m_Emote == EMOTE_NORMAL)
	{
		if (250 - ((Server()->Tick() - m_LastAction) % (250)) < 5)
			pCharacter->m_Emote = EMOTE_BLINK;
	}

	pCharacter->m_PlayerFlags = GetPlayer()->m_PlayerFlags;
}

void CCharacter::DoZombieMovement()
{
	if (!m_pPlayer->GetZomb())
		return;

	if (m_Move.m_LastX == m_Pos.x) // direction swap caused by a wall
		m_Move.m_LastXTimer++;
	else
		m_Move.m_LastXTimer = 0;
	m_Move.m_LastX = m_Pos.x;

	if (IsGrounded()) // reset jump
		m_Move.m_JumpTimer = 0;

	if (m_Move.m_LastXTimer > 50) // Direction change for non chaning X
	{
		m_Input.m_Direction = m_Input.m_Direction * -1;
		m_Move.m_FJump = true;
	}

	if (GameServer()->Collision()->CheckPoint(vec2(m_Pos.x + m_Input.m_Direction * 45, m_Pos.y)) || GameServer()->Collision()->CheckPoint(vec2(m_Pos.x + m_Input.m_Direction * 77, m_Pos.y)) || m_Move.m_FJump || m_Core.m_Vel == vec2(0.f, 0.f)) // set jump
		m_Move.m_JumpTimer++;

	if (m_Move.m_FJump) // Reset Collision jump
		m_Move.m_FJump = false;

	if (m_Move.m_CliffTimer) // Reset Cliff Timer
		m_Move.m_CliffTimer--;

	if (m_Move.m_JumpTimer == 1 || m_Move.m_JumpTimer == 31) // Jump + DoubleJump
		m_Input.m_Jump = 1;
	else
		m_Input.m_Jump = 0;

	if (!m_Input.m_Direction) // rare but possible
	{
		int Rand = rand() % 2;
		if (Rand < 1)
			m_Input.m_Direction = -1;
		else
			m_Input.m_Direction = 1;
	}

	if (GameServer()->Tuning()->m_PlayerCollision) // Zombies blocking other zombies
	{
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			CCharacter *pChar = GameServer()->GetPlayerChar(i);

			if (!pChar || pChar == this)
				continue;

			if (distance(m_Pos, pChar->m_Pos) <= 65.0f && m_Pos.y == pChar->m_Pos.y)
			{
				if (m_Pos.x > pChar->m_Pos.x && m_Input.m_Direction != pChar->m_Input.m_Direction)
					m_Move.m_FJump = true;
			}
		}
	}

	// Can jump over cliff?!
	if (!GameServer()->Collision()->CheckTiles(vec2(m_Pos.x + m_Input.m_Direction * 46, m_Pos.y), 15) && !m_Move.m_CliffTimer)
	{

		m_Move.m_FJump = true;
		m_Move.m_CliffTimer = 30;
		if (!GameServer()->Collision()->CheckParable(vec2(m_Pos.x, m_Pos.y), 50, m_Input.m_Direction))
		{
			m_Input.m_Direction = m_Input.m_Direction * -1;
			m_Move.m_CliffTimer = 0;
		}
	}

	CCharacter *pClosest = this;
	CCharacter *pCloseZomb = this;
	for (int i = 0; i < MAX_CLIENTS; i++) // see a player? :D Do Aim - Don't jump over the cliff if you see a player falling down
	{
		CCharacter *pChar = GameServer()->GetPlayerChar(i);

		if (!pChar || pChar == this || !pChar->IsAlive() || !pChar->GetPlayer() || (GameServer()->Collision()->IntersectTile(m_Pos, pChar->m_Pos) && m_Core.m_HookState != HOOK_GRABBED))
			continue;

		if (GameServer()->m_apPlayers[i]->GetTeam() == TEAM_RED && (pClosest == this || distance(m_Pos, pClosest->m_Pos) > distance(m_Pos, pChar->m_Pos)))
			pClosest = pChar;
		if (GameServer()->m_apPlayers[i]->GetTeam() == TEAM_BLUE && (pCloseZomb == this || distance(m_Pos, pCloseZomb->m_Pos) > distance(m_Pos, pChar->m_Pos)))
			pCloseZomb = pChar;
	}
	if (pClosest != this)
	{
		if (m_pPlayer->GetZomb(9)) // Flombie fly movement //first movement, then Aim because Aim can cause death
		{
			if (pClosest->m_Pos.y < m_Pos.y)
				m_Core.m_Vel.y -= 0.25f + GameServer()->Tuning()->m_Gravity; // must work
			else if (pClosest->m_Pos.y > m_Pos.y)
				m_Core.m_Vel.y += 0.25f;
		}

		// Do Aim
		if (pCloseZomb != this)
			DoZombieAim(pClosest->m_Pos, pClosest->GetPlayer()->GetCID(), pCloseZomb->m_Pos, pCloseZomb->GetPlayer()->GetCID()); // Only do aiming if it sees a player, (but i wanted it always...)
		else
			DoZombieAim(pClosest->m_Pos, pClosest->GetPlayer()->GetCID(), vec2(0, 0), -1); // Only do aiming if it sees a player, (but i wanted it always...)
		if (IsAlive() && pClosest && pClosest->IsAlive() && (pClosest->m_Pos.y > m_Pos.y || GameServer()->Collision()->CheckTiles(pClosest->m_Pos, 20)))
		{
			if (m_Pos.x > pClosest->m_Pos.x)
				m_Input.m_Direction = -1;
			else if (m_Pos.x == pClosest->m_Pos.x)
				m_Input.m_Direction = 0; // rare but could
			else
				m_Input.m_Direction = 1;
		}
	}
	else if (pCloseZomb != this)
		DoZombieAim(vec2(0, 0), -1, pCloseZomb->m_Pos, pCloseZomb->GetPlayer()->GetCID());
	else
		ResetAiming();
}

void CCharacter::DoZombieAim(vec2 VictimPos, int VicCID, vec2 NearZombPos, int NearZombCID)
{
	if (!m_pPlayer->GetZomb())
		return;

	// if(m_Aim.m_FireCounter)
	// m_Aim.m_FireCounter--;
	if (m_pPlayer->GetZomb(8) && distance(m_Pos, VictimPos) > 100.0f)
		VictimPos = GetGrenadeAngle(m_Pos, VictimPos, true) + m_Pos;

	if (m_pPlayer->GetZomb(5) && distance(m_Pos, VictimPos) > 100.0f)
		VictimPos = GetGrenadeAngle(m_Pos, VictimPos, false) + m_Pos;

	// Direction is exactly to the player
	m_Input.m_TargetY = 160 * (VictimPos.y - m_Pos.y) / sqrt((VictimPos.x - m_Pos.x) * (VictimPos.x - m_Pos.x) + (VictimPos.y - m_Pos.y) * (VictimPos.y - m_Pos.y));
	m_Input.m_TargetX = 160 * (VictimPos.x - m_Pos.x) / sqrt((VictimPos.x - m_Pos.x) * (VictimPos.x - m_Pos.x) + (VictimPos.y - m_Pos.y) * (VictimPos.y - m_Pos.y));
	m_LatestInput.m_TargetX = m_Input.m_TargetX;
	m_LatestInput.m_TargetY = m_Input.m_TargetY;
	// THIS IS THE FUCKING WORKING KEY, cost me 2 days
	m_LastAction = Server()->Tick();

	// ||
	if (m_pPlayer->GetZomb(2))
		m_Aim.m_FireCounter = 21;

	// Zele
	if (m_pPlayer->GetZomb(11))
	{
		if (distance(m_Pos, VictimPos) <= 500.0f && distance(m_Pos, VictimPos) > 200.0f)
		{
			if (!GameServer()->Collision()->CheckPoint(VictimPos + vec2(0, 32)))
				m_Core.m_Pos = VictimPos + vec2(0, 32);
			else
				m_Core.m_Pos = VictimPos - vec2(0, 32);
			m_Core.m_Vel.y = -0.1;
			m_Pos = m_Core.m_Pos;
		}
	}

	// Zeater
	if (m_pPlayer->GetZomb(13) && NearZombPos != m_Pos && distance(m_Pos, NearZombPos) <= 65.0f && !GameServer()->m_apPlayers[NearZombCID]->GetZomb(13))
	{
		for (int i = 0; i < 3; i++)
		{
			if (!m_pPlayer->m_SubZomb[i])
			{
				int VictimType = GameServer()->m_apPlayers[NearZombCID]->GetZomb();
				if (m_pPlayer->GetZomb(VictimType))
					return;
				if (VictimType == 6)
					IncreaseHealth(100);
				else
					IncreaseHealth(10);
				m_pPlayer->m_Score++;
				m_pPlayer->m_SubZomb[i] = VictimType;
				GameServer()->GetPlayerChar(NearZombCID)->Die(m_pPlayer->GetCID(), WEAPON_GAME);

				if (m_pPlayer->GetZomb(5)) // gun
					m_ActiveWeapon = WEAPON_GUN;
				if (m_pPlayer->GetZomb(7)) // Shotgun
					m_ActiveWeapon = WEAPON_SHOTGUN;
				if (m_pPlayer->GetZomb(8)) // Grenade
					m_ActiveWeapon = WEAPON_GRENADE;
				if (m_pPlayer->GetZomb(2)) // Rifle
					m_ActiveWeapon = WEAPON_RIFLE;
				m_aWeapons[m_ActiveWeapon].m_Ammo = -1;
				break;
			}
		}
	}

	// Can do sth.
	if (distance(m_Pos, VictimPos) <= GetTriggerDistance(m_pPlayer->GetZomb()) || (!m_pPlayer->GetZomb(4) && NearZombPos != m_Pos && distance(m_Pos, NearZombPos) <= 65.0f && GetTriggerDistance(m_pPlayer->GetZomb()) == 65.0f)) // Zamer shouldnt attak other zombies
	{
		// Zinvi
		if (m_pPlayer->GetZomb(12))
			m_IsVisible = true;

		// Zinja
		if (m_pPlayer->GetZomb(10))
		{
			if (m_ActiveWeapon == WEAPON_HAMMER)
				GameServer()->CreateSound(m_Pos, SOUND_PICKUP_NINJA);
			GiveNinja();
		}

		// Zooker
		if (m_pPlayer->GetZomb(3))
		{
			if (VicCID != -1 && distance(m_Pos, VictimPos) < 380.0f && GameServer()->GetPlayerChar(VicCID)) // Look hooklenght in tuning.h
			{
				m_Input.m_Hook = 1;
				m_LatestInput.m_Hook = 1;
				m_Core.m_HookedPlayer = VicCID;
				m_Core.m_HookState = HOOK_GRABBED;
			}
			if (m_Core.m_HookState != HOOK_IDLE && (!GameServer()->GetPlayerChar(m_Core.m_HookedPlayer) || !GameServer()->GetPlayerChar(m_Core.m_HookedPlayer)->IsAlive())) // return hook by death
			{
				m_Input.m_Hook = 0;
				m_LatestInput.m_Hook = 0;
				m_Core.m_HookState = HOOK_IDLE;
				m_Core.m_HookTick = 0;
				m_Core.m_HookedPlayer = -1;
			}
		}

		// Zamer
		if (m_pPlayer->GetZomb(4))
		{
			m_Aim.m_Explode = true;
			GameServer()->CreateExplosion(vec2(m_Pos.x + 5, m_Pos.y + 5), m_pPlayer->GetCID(), WEAPON_GAME, false);
			GameServer()->CreateExplosion(vec2(m_Pos.x - 5, m_Pos.y + 5), m_pPlayer->GetCID(), WEAPON_GAME, false);
			GameServer()->CreateExplosion(vec2(m_Pos.x - 5, m_Pos.y - 5), m_pPlayer->GetCID(), WEAPON_GAME, false);
			GameServer()->CreateExplosion(vec2(m_Pos.x + 5, m_Pos.y - 5), m_pPlayer->GetCID(), WEAPON_GAME, false);
			if (m_pPlayer)
				Die(m_pPlayer->GetCID(), WEAPON_SELF);
			return;
		}

		// Fire!
		m_Input.m_Fire = 1;
		m_LatestPrevInput.m_Fire = 1;
	}
	else if (!m_pPlayer->GetZomb(8) && !m_pPlayer->GetZomb(5))
	{
		if (m_pPlayer->GetZomb(12))
			m_IsVisible = false;
		ResetAiming();
	}

	if (VictimPos == vec2(0, 0) || VictimPos == m_Pos) // Reset all
		ResetAiming();
}

vec2 CCharacter::GetGrenadeAngle(vec2 m_StartPos, vec2 m_ToShoot, bool GrenadeBot)
{
	/*
	inline vec2 CalcPos(vec2 Pos, vec2 Velocity, float Curvature, float Speed, float Time)
	{
		vec2 n;
		Time *= Speed;
		n.x = Pos.x + Velocity.x*Time;
		n.y = Pos.y + Velocity.y*Time + Curvature/10000*(Time*Time);
		return n;
	*/
	if (m_ToShoot == vec2(0, 0))
	{
		return vec2(0, 0);
	}
	char aBuf[128];
	vec2 m_Direction;
	float Curvature = GameServer()->Tuning()->m_GunCurvature;
	if (GrenadeBot)
		Curvature = GameServer()->Tuning()->m_GrenadeCurvature;
	m_Direction.x = (m_ToShoot.x - m_StartPos.x);
	m_Direction.y = (m_ToShoot.y - m_StartPos.y - 32 * Curvature);
	str_format(aBuf, sizeof(aBuf), "AimPos %d %d", m_Direction.x, m_Direction.y);
	return m_Direction;
}

void CCharacter::ResetAiming()
{
	m_Input.m_Fire = 0;
	m_LatestPrevInput.m_Fire = 0;
	if (m_pPlayer->GetZomb(10))
	{
		m_aWeapons[WEAPON_NINJA].m_Got = false;
		m_ActiveWeapon = WEAPON_HAMMER;
	}
}

float CCharacter::GetTriggerDistance(int Type)
{
	if (Type == 5 || Type == 9) // Zunner
		return 4000.0f;
	else if (Type == 8)
		return 800.0f; // Grenade
	else if (Type == 2)
		return GameServer()->Tuning()->m_LaserReach; // Zoomer
	else if (Type == 7 || Type == 10)				 // Zotter, Zinja
		return 500.0f;
	else if (Type == 3) // Zooker
		return 380.0f;
	return 65.0f; // Rest
}