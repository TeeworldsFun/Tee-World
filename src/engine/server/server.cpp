/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/system.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/engine.h>
#include <engine/map.h>
#include <engine/masterserver.h>
#include <engine/server.h>
#include <engine/storage.h>

#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/econ.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/http.h>
#include <engine/shared/json.h>
#include <engine/shared/netban.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/snapshot.h>

#include <mastersrv/mastersrv.h>

#include "register.h"
#include "server.h"

#include <teeuniverses/components/localization.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>

#if defined(CONF_FAMILY_WINDOWS)
#define _WIN32_WINNT 0x0501
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(CONF_FAMILY_UNIX)
#include <cstring> //fixes memset error
#endif

#include "multi_worlds.h"

static const char *StrLtrim(const char *pStr)
{
	while (*pStr && *pStr >= 0 && *pStr <= 32)
		pStr++;
	return pStr;
}

static void StrRtrim(char *pStr)
{
	int i = str_length(pStr);
	while (i >= 0)
	{
		if (pStr[i] < 0 || pStr[i] > 32)
			break;
		pStr[i] = 0;
		i--;
	}
}

CSnapIDPool::CSnapIDPool()
{
	Reset();
}

void CSnapIDPool::Reset()
{
	for (int i = 0; i < MAX_IDS; i++)
	{
		m_aIDs[i].m_Next = i + 1;
		m_aIDs[i].m_State = 0;
	}

	m_aIDs[MAX_IDS - 1].m_Next = -1;
	m_FirstFree = 0;
	m_FirstTimed = -1;
	m_LastTimed = -1;
	m_Usage = 0;
	m_InUsage = 0;
}

void CSnapIDPool::RemoveFirstTimeout()
{
	int NextTimed = m_aIDs[m_FirstTimed].m_Next;

	// add it to the free list
	m_aIDs[m_FirstTimed].m_Next = m_FirstFree;
	m_aIDs[m_FirstTimed].m_State = 0;
	m_FirstFree = m_FirstTimed;

	// remove it from the timed list
	m_FirstTimed = NextTimed;
	if (m_FirstTimed == -1)
		m_LastTimed = -1;

	m_Usage--;
}

int CSnapIDPool::NewID()
{
	int64 Now = time_get();

	// process timed ids
	while (m_FirstTimed != -1 && m_aIDs[m_FirstTimed].m_Timeout < Now)
		RemoveFirstTimeout();

	int ID = m_FirstFree;
	dbg_assert(ID != -1, "id error");
	if (ID == -1)
		return ID;
	m_FirstFree = m_aIDs[m_FirstFree].m_Next;
	m_aIDs[ID].m_State = 1;
	m_Usage++;
	m_InUsage++;
	return ID;
}

void CSnapIDPool::TimeoutIDs()
{
	// process timed ids
	while (m_FirstTimed != -1)
		RemoveFirstTimeout();
}

void CSnapIDPool::FreeID(int ID)
{
	if (ID < 0)
		return;
	dbg_assert(m_aIDs[ID].m_State == 1, "id is not alloced");

	m_InUsage--;
	m_aIDs[ID].m_State = 2;
	m_aIDs[ID].m_Timeout = time_get() + time_freq() * 5;
	m_aIDs[ID].m_Next = -1;

	if (m_LastTimed != -1)
	{
		m_aIDs[m_LastTimed].m_Next = ID;
		m_LastTimed = ID;
	}
	else
	{
		m_FirstTimed = ID;
		m_LastTimed = ID;
	}
}

void CServerBan::InitServerBan(IConsole *pConsole, IStorage *pStorage, CServer *pServer)
{
	CNetBan::Init(pConsole, pStorage);

	m_pServer = pServer;

	// overwrites base command, todo: improve this
	Console()->Register("ban", "s?ir", CFGFLAG_SERVER | CFGFLAG_STORE, ConBanExt, this, "Ban player with ip/client id for x minutes for any reason");
}

template <class T>
int CServerBan::BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason)
{
	// validate address
	if (Server()->m_RconClientID >= 0 && Server()->m_RconClientID < MAX_CLIENTS &&
		Server()->m_aClients[Server()->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		if (NetMatch(pData, Server()->m_NetServer.ClientAddr(Server()->m_RconClientID)))
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (you can't ban yourself)");
			return -1;
		}

		for (int i = 0; i < MAX_CLIENTS; ++i)
		{
			if (i == Server()->m_RconClientID || Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if (Server()->m_aClients[i].m_Authed >= Server()->m_RconAuthLevel && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}
	else if (Server()->m_RconClientID == IServer::RCON_CID_VOTE)
	{
		for (int i = 0; i < MAX_CLIENTS; ++i)
		{
			if (Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
				continue;

			if (Server()->m_aClients[i].m_Authed != CServer::AUTHED_NO && NetMatch(pData, Server()->m_NetServer.ClientAddr(i)))
			{
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (command denied)");
				return -1;
			}
		}
	}

	int Result = Ban(pBanPool, pData, Seconds, pReason);
	if (Result != 0)
		return Result;

	// drop banned clients
	typename T::CDataType Data = *pData;
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (Server()->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY)
			continue;

		if (NetMatch(&Data, Server()->m_NetServer.ClientAddr(i)))
		{
			CNetHash NetHash(&Data);
			char aBuf[256];
			MakeBanInfo(pBanPool->Find(&Data, &NetHash), aBuf, sizeof(aBuf), MSGTYPE_PLAYER);
			Server()->m_NetServer.Drop(i, aBuf);
		}
	}

	return Result;
}

int CServerBan::BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason)
{
	return BanExt(&m_BanAddrPool, pAddr, Seconds, pReason);
}

int CServerBan::BanRange(const CNetRange *pRange, int Seconds, const char *pReason)
{
	if (pRange->IsValid())
		return BanExt(&m_BanRangePool, pRange, Seconds, pReason);

	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban failed (invalid range)");
	return -1;
}

void CServerBan::ConBanExt(IConsole::IResult *pResult, void *pUser)
{
	CServerBan *pThis = static_cast<CServerBan *>(pUser);

	const char *pStr = pResult->GetString(0);
	int Minutes = pResult->NumArguments() > 1 ? clamp(pResult->GetInteger(1), 0, 44640) : 30;
	const char *pReason = pResult->NumArguments() > 2 ? pResult->GetString(2) : "No reason given";

	if (StrAllnum(pStr))
	{
		int ClientID = str_toint(pStr);
		if (ClientID < 0 || ClientID >= MAX_CLIENTS || pThis->Server()->m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "net_ban", "ban error (invalid client id)");
		else
			pThis->BanAddr(pThis->Server()->m_NetServer.ClientAddr(ClientID), Minutes * 60, pReason);
	}
	else
		ConBan(pResult, pUser);
}

void CServer::CClient::Reset()
{
	// reset input
	for (int i = 0; i < 200; i++)
		m_aInputs[i].m_GameTick = -1;
	m_CurrentInput = 0;
	mem_zero(&m_LatestInput, sizeof(m_LatestInput));

	m_Snapshots.PurgeAll();
	m_LastAckedSnapshot = -1;
	m_LastInputTick = -1;
	m_SnapRate = CClient::SNAPRATE_INIT;
	m_Score = 0;
	str_copy(m_aLanguage, "en", sizeof(m_aLanguage));
}

const char *CServer::GetClientLanguage(int ClientID)
{
	return m_aClients[ClientID].m_aLanguage;
}

void CServer::SetClientLanguage(int ClientID, const char *pLanguage)
{
	str_copy(m_aClients[ClientID].m_aLanguage, pLanguage, sizeof(m_aClients[ClientID].m_aLanguage));
}

CServer::CServer() : m_DemoRecorder(&m_SnapshotDelta)
{
	m_TickSpeed = SERVER_TICK_SPEED;

	m_CurrentGameTick = 0;
	m_RunServer = 1;

	m_MapReload = 0;
	m_HeavyReload = false;

	m_RconClientID = IServer::RCON_CID_SERV;
	m_RconAuthLevel = AUTHED_ADMIN;

	m_ServerInfoFirstRequest = 0;
	m_ServerInfoNumRequests = 0;
	m_ServerInfoHighLoad = false;
	m_ServerInfoNeedsUpdate = false;

	m_pRegister = nullptr;
	m_pMultiWorlds = new CMultiWorlds;

	Init();
}

CServer::~CServer()
{
	delete m_pRegister;
	delete m_pMultiWorlds;
}

IGameServer *CServer::GameServer(int WorldID)
{
	if (!MultiWorlds()->IsValid(WorldID))
		return MultiWorlds()->GetWorld(MAIN_WORLD_ID)->m_pGameServer;
	return MultiWorlds()->GetWorld(WorldID)->m_pGameServer;
}

IGameServer *CServer::GameServerPlayer(int ClientID)
{
	return GameServer(GetClientWorldID(ClientID));
}

int CServer::GetWorldsSize() const
{
	return MultiWorlds()->GetSizeInitilized();
}

void CServer::ChangeWorld(int ClientID, int NewWorldID)
{
	if (ClientID < 0 || ClientID >= MAX_PLAYERS || NewWorldID == m_aClients[ClientID].m_WorldID || !MultiWorlds()->IsValid(NewWorldID) || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_OldWorldID = m_aClients[ClientID].m_WorldID;
	GameServer(m_aClients[ClientID].m_OldWorldID)->PrepareClientChangeWorld(ClientID);

	m_aClients[ClientID].m_WorldID = NewWorldID;
	GameServer(m_aClients[ClientID].m_WorldID)->PrepareClientChangeWorld(ClientID);

	int *pIdMap = GetIdMap(ClientID);
	memset(pIdMap, -1, sizeof(int) * VANILLA_MAX_CLIENTS);
	pIdMap[0] = ClientID;

	m_aClients[ClientID].Reset();
	m_aClients[ClientID].m_IsChangesWorld = true;
	m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
	SendMap(ClientID);
}

int CServer::GetClientWorldID(int ClientID)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return MAIN_WORLD_ID;

	return m_aClients[ClientID].m_WorldID;
}

int CServer::TrySetClientName(int ClientID, const char *pName, bool IsBot)
{
	char aTrimmedName[64];

	// trim the name
	str_copy(aTrimmedName, StrLtrim(pName), sizeof(aTrimmedName));
	StrRtrim(aTrimmedName);

	// check for empty names
	if (!aTrimmedName[0])
		return -1;

	// check if new and old name are the same
	if (m_aClients[ClientID].m_aName[0] && str_comp(m_aClients[ClientID].m_aName, aTrimmedName) == 0)
		return 0;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "'%s' -> '%s'", pName, aTrimmedName);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
	pName = aTrimmedName;

	if (IsBot)
	{
		str_copy(m_aClients[ClientID].m_aName, pName, MAX_NAME_LENGTH);
		return 0;
	}
	// make sure that two clients doesn't have the same name
	for (int i = 0; i < MAX_CLIENTS; i++)
		if (i != ClientID && m_aClients[i].m_State >= CClient::STATE_READY && ClientID < MAX_PLAYERS)
		{
			if (str_comp(pName, m_aClients[i].m_aName) == 0)
				return -1;
		}

	// set the client name
	str_copy(m_aClients[ClientID].m_aName, pName, MAX_NAME_LENGTH);
	return 0;
}

void CServer::SetClientName(int ClientID, const char *pName, bool IsBot)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	if (!pName)
		return;

	char aCleanName[MAX_NAME_LENGTH];
	str_copy(aCleanName, pName, sizeof(aCleanName));

	if (TrySetClientName(ClientID, aCleanName, IsBot))
	{
		// auto rename
		for (int i = 1;; i++)
		{
			char aNameTry[MAX_NAME_LENGTH];
			str_format(aNameTry, sizeof(aCleanName), "(%d)%s", i, aCleanName);
			if (TrySetClientName(ClientID, aNameTry, IsBot) == 0)
				break;
		}
	}
}

void CServer::SetClientClan(int ClientID, const char *pClan)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY || !pClan)
		return;

	str_copy(m_aClients[ClientID].m_aClan, pClan, MAX_CLAN_LENGTH);
}

void CServer::SetClientCountry(int ClientID, int Country)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;

	m_aClients[ClientID].m_Country = Country;
}

void CServer::SetClientScore(int ClientID, int Score)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State < CClient::STATE_READY)
		return;
	m_aClients[ClientID].m_Score = Score;
}

void CServer::Kick(int ClientID, const char *pReason)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "invalid client id to kick");
		return;
	}
	else if (m_RconClientID == ClientID)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "you can't kick yourself");
		return;
	}
	else if (m_aClients[ClientID].m_Authed > m_RconAuthLevel)
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "kick command denied");
		return;
	}

	m_NetServer.Drop(ClientID, pReason);
}

/*int CServer::Tick()
{
	return m_CurrentGameTick;
}*/

int64 CServer::TickStartTime(int Tick)
{
	return m_GameStartTime + (time_freq() * Tick) / SERVER_TICK_SPEED;
}

/*int CServer::TickSpeed()
{
	return SERVER_TICK_SPEED;
}*/

int CServer::Init()
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aClients[i].m_State = CClient::STATE_EMPTY;
		m_aClients[i].m_aName[0] = 0;
		m_aClients[i].m_aClan[0] = 0;
		m_aClients[i].m_CustClt = 0;
		m_aClients[i].m_Country = -1;
		m_aClients[i].m_Snapshots.Init();
		m_aClients[i].m_IsBot = false;
	}

	m_CurrentGameTick = 0;

	_StoreMultiworldIdentifiableStaticData::Init((IServer *)this);
	return 0;
}

void CServer::SetRconCID(int ClientID)
{
	m_RconClientID = ClientID;
}

bool CServer::IsAuthed(int ClientID)
{
	return m_aClients[ClientID].m_Authed;
}

int CServer::GetClientInfo(int ClientID, CClientInfo *pInfo)
{
	dbg_assert(ClientID >= 0 && ClientID < MAX_CLIENTS, "client_id is not valid");
	dbg_assert(pInfo != 0, "info can not be null");

	if (m_aClients[ClientID].m_State == CClient::STATE_INGAME)
	{
		pInfo->m_pName = m_aClients[ClientID].m_aName;
		pInfo->m_Latency = m_aClients[ClientID].m_Latency;
		pInfo->m_CustClt = m_aClients[ClientID].m_CustClt;
		pInfo->m_Authed = m_aClients[ClientID].m_Authed;
		return 1;
	}
	return 0;
}

void CServer::GetClientAddr(int ClientID, char *pAddrStr, int Size)
{
	if (ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CClient::STATE_INGAME)
		net_addr_str(m_NetServer.ClientAddr(ClientID), pAddrStr, Size, false);
}

const char *CServer::ClientName(int ClientID)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "(invalid)";
	if (m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aName;
	else
		return "(connecting)";
}

const char *CServer::ClientClan(int ClientID)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return "";
	if (m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_aClan;
	else
		return "";
}

int CServer::ClientCountry(int ClientID)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS || m_aClients[ClientID].m_State == CServer::CClient::STATE_EMPTY)
		return -1;
	if (m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME)
		return m_aClients[ClientID].m_Country;
	else
		return -1;
}

bool CServer::ClientIngame(int ClientID)
{
	return ClientID >= 0 && ClientID < MAX_CLIENTS && m_aClients[ClientID].m_State == CServer::CClient::STATE_INGAME;
}

int CServer::MaxClients() const
{
	return MAX_CLIENTS;
}

static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer, bool Sixup)
{
	int MsgId = pMsg->m_MsgID;
	Packer.Reset();

	if (Sixup && !pMsg->m_NoTranslate)
	{
		if (pMsg->m_System)
		{
			if (MsgId >= OFFSET_UUID)
				;
			else if (MsgId >= NETMSG_MAP_CHANGE && MsgId <= NETMSG_MAP_DATA)
				;
			else if (MsgId >= NETMSG_CON_READY && MsgId <= NETMSG_INPUTTIMING)
				MsgId += 1;
			else if (MsgId == NETMSG_RCON_LINE)
				MsgId = 13;
			else if (MsgId >= NETMSG_AUTH_CHALLANGE && MsgId <= NETMSG_AUTH_RESULT)
				MsgId += 4;
			else if (MsgId >= NETMSG_PING && MsgId <= NETMSG_ERROR)
				MsgId += 4;
			else if (MsgId >= NETMSG_RCON_CMD_ADD && MsgId <= NETMSG_RCON_CMD_REM)
				MsgId -= 11;
			else
			{
				dbg_msg("net", "DROP send sys %d", MsgId);
				return true;
			}
		}
		else
		{

			if (MsgId < 0)
				return true;
		}
	}

	if (MsgId < OFFSET_UUID)
	{
		Packer.AddInt((MsgId << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt((0 << 1) | (pMsg->m_System ? 1 : 0)); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(MsgId, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return false;
}

int CServer::SendMsg(CMsgPacker *pMsg, int Flags, int ClientID, int64 Mask, int WorldID)
{
	if (!pMsg)
		return -1;

	if (ClientID != -1 && (ClientID < 0 || ClientID >= MAX_PLAYERS || m_aClients[ClientID].m_State == CClient::STATE_EMPTY || m_aClients[ClientID].m_Quitting))
		return 0;

	CNetChunk Packet;
	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientID = ClientID;
	Packet.m_pData = pMsg->Data();
	Packet.m_DataSize = pMsg->Size();

	if (Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if (Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if (!(Flags & MSGFLAG_NOSEND))
	{
		if (ClientID == -1)
		{
			CPacker Pack;
			if (RepackMsg(pMsg, Pack, false))
				return -1;

			for (int i = 0; i < MAX_PLAYERS; i++)
			{
				if (m_aClients[i].m_State == CClient::STATE_INGAME && !m_aClients[i].m_Quitting)
				{
					// skip what is not included in the mask
					if (Mask != -1 && (Mask & (int64)1 << i) == 0)
						continue;

					const CPacker *pPack = &Pack;
					Packet.m_pData = pPack->Data();
					Packet.m_DataSize = pPack->Size();
					if (WorldID != -1)
					{
						if (m_aClients[i].m_WorldID == WorldID)
						{
							Packet.m_ClientID = i;
							m_NetServer.Send(&Packet);
						}
						continue;
					}

					Packet.m_ClientID = i;
					m_NetServer.Send(&Packet);
				}
			}
		}
		else
		{
			CPacker Pack;
			if (RepackMsg(pMsg, Pack, false))
				return -1;

			Packet.m_ClientID = ClientID;
			Packet.m_pData = Pack.Data();
			Packet.m_DataSize = Pack.Size();

			if (!(Flags & MSGFLAG_NOSEND))
				m_NetServer.Send(&Packet);
		}
	}
	return 0;
}

void CServer::DoSnapshot(int WorldID)
{
	GameServer(WorldID)->OnPreSnap();
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		// client must be ingame to recive snapshots
		if (m_aClients[i].m_WorldID != WorldID || m_aClients[i].m_State != CClient::STATE_INGAME)
			continue;

		// this client is trying to recover, don't spam snapshots
		if (m_aClients[i].m_SnapRate == CClient::SNAPRATE_RECOVER && (Tick() % 50) != 0)
			continue;

		// this client is trying to recover, don't spam snapshots
		if (m_aClients[i].m_SnapRate == CClient::SNAPRATE_INIT && (Tick() % 10) != 0)
			continue;

		{
			m_SnapshotBuilder.Init();

			GameServer(WorldID)->OnSnap(i);

			// finish snapshot
			char aData[CSnapshot::MAX_SIZE];
			CSnapshot *pData = (CSnapshot *)aData; // Fix compiler warning for strict-aliasing
			int SnapshotSize = m_SnapshotBuilder.Finish(pData);

			int Crc = pData->Crc();

			// remove old snapshots
			// keep 3 seconds worth of snapshots
			m_aClients[i].m_Snapshots.PurgeUntil(m_CurrentGameTick - SERVER_TICK_SPEED * 3);

			// save the snapshot
			m_aClients[i].m_Snapshots.Add(m_CurrentGameTick, time_get(), SnapshotSize, pData, 0, nullptr);

			// find snapshot that we can perform delta against
			static CSnapshot s_EmptySnap;
			s_EmptySnap.Clear();

			int DeltaTick = -1;
			CSnapshot *pDeltashot = &s_EmptySnap;
			{
				int DeltashotSize = m_aClients[i].m_Snapshots.Get(m_aClients[i].m_LastAckedSnapshot, 0, &pDeltashot, 0);
				if (DeltashotSize >= 0)
					DeltaTick = m_aClients[i].m_LastAckedSnapshot;
				else
				{
					// no acked package found, force client to recover rate
					if (m_aClients[i].m_SnapRate == CClient::SNAPRATE_FULL)
						m_aClients[i].m_SnapRate = CClient::SNAPRATE_RECOVER;
				}
			}

			// create delta
			char aDeltaData[CSnapshot::MAX_SIZE];
			if (int DeltaSize = m_SnapshotDelta.CreateDelta(pDeltashot, pData, aDeltaData))
			{
				// compress it
				const int MaxSize = MAX_SNAPSHOT_PACKSIZE;

				char aCompData[CSnapshot::MAX_SIZE];
				SnapshotSize = CVariableInt::Compress(aDeltaData, DeltaSize, aCompData, sizeof(aCompData));
				int NumPackets = (SnapshotSize + MaxSize - 1) / MaxSize;

				for (int n = 0, Left = SnapshotSize; Left > 0; n++)
				{
					int Chunk = Left < MaxSize ? Left : MaxSize;
					Left -= Chunk;

					if (NumPackets == 1)
					{
						CMsgPacker Msg(NETMSG_SNAPSINGLE, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i, -1, WorldID);
					}
					else
					{
						CMsgPacker Msg(NETMSG_SNAP, true);
						Msg.AddInt(m_CurrentGameTick);
						Msg.AddInt(m_CurrentGameTick - DeltaTick);
						Msg.AddInt(NumPackets);
						Msg.AddInt(n);
						Msg.AddInt(Crc);
						Msg.AddInt(Chunk);
						Msg.AddRaw(&aCompData[n * MaxSize], Chunk);
						SendMsg(&Msg, MSGFLAG_FLUSH, i, -1, WorldID);
					}
				}
			}
			else
			{
				CMsgPacker Msg(NETMSG_SNAPEMPTY, true);
				Msg.AddInt(m_CurrentGameTick);
				Msg.AddInt(m_CurrentGameTick - DeltaTick);
				SendMsg(&Msg, MSGFLAG_FLUSH, i, -1, WorldID);
			}
		}
	}
	GameServer(WorldID)->OnPostSnap();
}

int CServer::ClientRejoinCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_DDNetVersion = VERSION_NONE;
	pThis->m_aClients[ClientID].m_GotDDNetVersionPacket = false;
	pThis->m_aClients[ClientID].m_DDNetVersionSettled = false;

	pThis->m_aClients[ClientID].Reset();

	pThis->SendMap(ClientID);

	return 0;
}

int CServer::NewClientNoAuthCallback(int ClientID, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	pThis->m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_OldWorldID = MAIN_WORLD_ID;
	pThis->m_aClients[ClientID].m_WorldID = MAIN_WORLD_ID;
	pThis->m_aClients[ClientID].m_CustClt = 0;
	pThis->m_aClients[ClientID].Reset();

	pThis->SendMap(ClientID);
	pThis->SendCapabilities(ClientID);

	return 0;
}

int CServer::NewClientCallback(int ClientID, void *pUser, bool Sixup)
{
	CServer *pThis = (CServer *)pUser;

	if (pThis->m_aClients[ClientID].m_IsBot)
		pThis->GameServer()->DeleteBot(ClientID);

	pThis->m_aClients[ClientID].m_State = CClient::STATE_AUTH;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_CustClt = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_OldWorldID = MAIN_WORLD_ID;
	pThis->m_aClients[ClientID].m_WorldID = MAIN_WORLD_ID;
	pThis->m_aClients[ClientID].m_IsChangesWorld = false;
	pThis->m_aClients[ClientID].m_IsBot = false;
	memset(&pThis->m_aClients[ClientID].m_Addr, 0, sizeof(NETADDR));
	pThis->m_aClients[ClientID].Reset();
	return 0;
}

bool CServer::IsClientChangesWorld(int ClientID)
{
	if (ClientID < 0 || ClientID >= MAX_CLIENTS)
		return false;

	return m_aClients[ClientID].m_IsChangesWorld && m_aClients[ClientID].m_State >= CClient::STATE_CONNECTING && m_aClients[ClientID].m_State < CClient::STATE_INGAME;
}

const char *CServer::GetWorldName(int WorldID)
{
	if (!MultiWorlds()->IsValid(WorldID))
		return "invalid";
	return MultiWorlds()->GetWorld(WorldID)->m_aName;
}

int CServer::DelClientCallback(int ClientID, const char *pReason, void *pUser)
{
	CServer *pThis = (CServer *)pUser;

	char aAddrStr[NETADDR_MAXSTRSIZE];
	net_addr_str(pThis->m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "client dropped. cid=%d addr=%s reason='%s'", ClientID, aAddrStr, pReason);
	pThis->Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

	// notify the mod about the drop
	if (pThis->m_aClients[ClientID].m_State >= CClient::STATE_READY || pThis->IsClientChangesWorld(ClientID))
	{
		pThis->m_aClients[ClientID].m_Quitting = true;

		for (int i = 0; i < pThis->MultiWorlds()->GetSizeInitilized(); i++)
		{
			IGameServer *pGameServer = pThis->MultiWorlds()->GetWorld(i)->m_pGameServer;
			pGameServer->OnClientDrop(ClientID, pReason);
		}

		pThis->GameServer(MAIN_WORLD_ID)->ClearClientData(ClientID);
		pThis->ExpireServerInfo();
	}

	pThis->m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	pThis->m_aClients[ClientID].m_aName[0] = 0;
	pThis->m_aClients[ClientID].m_aClan[0] = 0;
	pThis->m_aClients[ClientID].m_Country = -1;
	pThis->m_aClients[ClientID].m_Authed = AUTHED_NO;
	pThis->m_aClients[ClientID].m_AuthTries = 0;
	pThis->m_aClients[ClientID].m_pRconCmdToSend = 0;
	pThis->m_aClients[ClientID].m_IsBot = false;
	pThis->m_aClients[ClientID].m_OldWorldID = MAIN_WORLD_ID;
	pThis->m_aClients[ClientID].m_WorldID = MAIN_WORLD_ID;
	pThis->m_aClients[ClientID].m_IsChangesWorld = false;
	pThis->m_aClients[ClientID].m_Quitting = false;
	pThis->m_aClients[ClientID].m_Snapshots.PurgeAll();

	pThis->ExpireServerInfo();
	return 0;
}

void CServer::SendMapData(int ClientID, int Chunk)
{
	const int WorldID = m_aClients[ClientID].m_WorldID;
	unsigned int CurrentMapSize = MultiWorlds()->GetWorld(WorldID)->m_pLoadedMap->GetCurrentMapSize();
	unsigned char *pCurrentMapData = MultiWorlds()->GetWorld(WorldID)->m_pLoadedMap->GetCurrentMapData();
	const unsigned Crc = MultiWorlds()->GetWorld(WorldID)->m_pLoadedMap->Crc();

	unsigned int ChunkSize = 1024 - 128;
	const unsigned int Offset = Chunk * ChunkSize;
	int Last = 0;

	// drop faulty map data requests
	if (Chunk < 0 || Offset > CurrentMapSize)
		return;

	if ((Offset + ChunkSize) >= CurrentMapSize)
	{
		ChunkSize = CurrentMapSize - Offset;
		Last = 1;
	}

	CMsgPacker Msg(NETMSG_MAP_DATA, true);
	Msg.AddInt(Last);
	Msg.AddInt(Crc);
	Msg.AddInt(Chunk);
	Msg.AddInt(ChunkSize);
	Msg.AddRaw(&pCurrentMapData[Offset], ChunkSize);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);

	if (g_Config.m_Debug)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "sending chunk %d with size %d", Chunk, ChunkSize);
		Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
	}
}

void CServer::SendRconType(int ClientID, bool UsernameReq)
{
	CMsgPacker Msg(NETMSG_RCONTYPE, true);
	Msg.AddInt(UsernameReq);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendCapabilities(int ClientID)
{
	CMsgPacker Msg(NETMSG_CAPABILITIES, true);
	Msg.AddInt(SERVERCAP_CURVERSION); // version
	Msg.AddInt(SERVERCAPFLAG_DDNET |
			   SERVERCAPFLAG_ANYPLAYERFLAG |
			   SERVERCAPFLAG_PINGEX |
			   SERVERCAPFLAG_SYNCWEAPONINPUT); // flags
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendMap(int ClientID)
{
	const int WorldID = m_aClients[ClientID].m_WorldID;
	const char *pWorldName = MultiWorlds()->GetWorld(WorldID)->m_aName;
	IEngineMap *pMap = MultiWorlds()->GetWorld(WorldID)->m_pLoadedMap;
	const unsigned Crc = pMap->Crc();

	{
		SHA256_DIGEST Sha256 = pMap->Sha256();
		CMsgPacker Msg(NETMSG_MAP_DETAILS, true);
		Msg.AddString("SOSOSO", 0);
		Msg.AddRaw(&Sha256.data, sizeof(Sha256.data));
		Msg.AddInt(Crc);
		Msg.AddInt(pMap->GetCurrentMapSize());
		Msg.AddString("", 0); // HTTPS map download URL
		SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
	}

	{

		CMsgPacker Msg(NETMSG_MAP_CHANGE, true);
		Msg.AddString(pWorldName, 0);
		Msg.AddInt(Crc);
		Msg.AddInt(pMap->GetCurrentMapSize());
		SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
	}

	m_aClients[ClientID].m_NextMapChunk = 0;
}

void CServer::SendConnectionReady(int ClientID)
{
	CMsgPacker Msg(NETMSG_CON_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH, ClientID);
}

void CServer::SendRconLine(int ClientID, const char *pLine)
{
	CMsgPacker Msg(NETMSG_RCON_LINE, true);
	Msg.AddString(pLine, 512);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconLineAuthed(const char *pLine, void *pUser)
{
	CServer *pThis = (CServer *)pUser;
	static volatile int ReentryGuard = 0;
	int i;

	if (ReentryGuard)
		return;
	ReentryGuard++;

	for (i = 0; i < MAX_CLIENTS; i++)
	{
		if (pThis->m_aClients[i].m_State != CClient::STATE_EMPTY && pThis->m_aClients[i].m_Authed >= pThis->m_RconAuthLevel)
			pThis->SendRconLine(i, pLine);
	}

	ReentryGuard--;
}

void CServer::SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_ADD, true);
	Msg.AddString(pCommandInfo->m_pName, IConsole::TEMPCMD_NAME_LENGTH);
	Msg.AddString(pCommandInfo->m_pHelp, IConsole::TEMPCMD_HELP_LENGTH);
	Msg.AddString(pCommandInfo->m_pParams, IConsole::TEMPCMD_PARAMS_LENGTH);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID)
{
	CMsgPacker Msg(NETMSG_RCON_CMD_REM, true);
	Msg.AddString(pCommandInfo->m_pName, 256);
	SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CServer::UpdateClientRconCommands()
{
	int ClientID = Tick() % MAX_CLIENTS;

	if (m_aClients[ClientID].m_State != CClient::STATE_EMPTY && m_aClients[ClientID].m_Authed)
	{
		int ConsoleAccessLevel = m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD;
		for (int i = 0; i < MAX_RCONCMD_SEND && m_aClients[ClientID].m_pRconCmdToSend; ++i)
		{
			SendRconCmdAdd(m_aClients[ClientID].m_pRconCmdToSend, ClientID);
			m_aClients[ClientID].m_pRconCmdToSend = m_aClients[ClientID].m_pRconCmdToSend->NextCommandInfo(ConsoleAccessLevel, CFGFLAG_SERVER);
		}
	}
}

void CServer::ProcessClientPacket(CNetChunk *pPacket)
{
	int ClientID = pPacket->m_ClientID;
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int MsgID;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageID(&MsgID, &Sys, &Uuid, &Unpacker, &Packer);
	if (Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}

	if (Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(&Packer, MSGFLAG_VITAL, ClientID);
	}

	if (Sys)
	{
		// system message
		if (MsgID == NETMSG_CLIENTVER)
		{
			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				CUuid *pConnectionID = (CUuid *)Unpacker.GetRaw(sizeof(*pConnectionID));
				int DDNetVersion = Unpacker.GetInt();
				const char *pDDNetVersionStr = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if (Unpacker.Error() || !str_utf8_check(pDDNetVersionStr) || DDNetVersion < 0)
				{
					return;
				}
				m_aClients[ClientID].m_ConnectionID = *pConnectionID;
				m_aClients[ClientID].m_DDNetVersion = DDNetVersion;
				str_copy(m_aClients[ClientID].m_aDDNetVersionStr, pDDNetVersionStr, sizeof(m_aClients[ClientID].m_aDDNetVersionStr));
				m_aClients[ClientID].m_DDNetVersionSettled = true;
				m_aClients[ClientID].m_GotDDNetVersionPacket = true;
				m_aClients[ClientID].m_State = CClient::STATE_AUTH;
			}
		}
		else if (MsgID == NETMSG_INFO)
		{
			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_AUTH)
			{
				const char *pPassword = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if (g_Config.m_Password[0] != 0 && str_comp(g_Config.m_Password, pPassword) != 0)
				{
					// wrong password
					m_NetServer.Drop(ClientID, "Wrong password");
					return;
				}

				m_aClients[ClientID].m_Version = Unpacker.GetInt();
				m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
				GameServer(MAIN_WORLD_ID)->ClearClientData(ClientID);
				SendCapabilities(ClientID);
				SendMap(ClientID);
			}
		}
		else if (MsgID == NETMSG_REQUEST_MAP_DATA)
		{
			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) == 0 || m_aClients[ClientID].m_State < CClient::STATE_CONNECTING)
				return;

			const int Chunk = Unpacker.GetInt();
			if (Chunk != m_aClients[ClientID].m_NextMapChunk || !g_Config.m_SvFastDownload)
			{
				SendMapData(ClientID, Chunk);
				return;
			}

			if (Chunk == 0)
			{
				for (int i = 0; i < g_Config.m_SvMapWindow; i++)
				{
					SendMapData(ClientID, i);
				}
			}

			SendMapData(ClientID, g_Config.m_SvMapWindow + m_aClients[ClientID].m_NextMapChunk);
			m_aClients[ClientID].m_NextMapChunk++;
		}
		else if (MsgID == NETMSG_READY)
		{
			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_CONNECTING)
			{
				if (!m_aClients[ClientID].m_IsChangesWorld)
				{
					char aAddrStr[NETADDR_MAXSTRSIZE];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "player is ready. ClientID=%d addr=%s", ClientID, aAddrStr);
					Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);

					const int WorldID = m_aClients[ClientID].m_WorldID;
					for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
					{
						IGameServer *pGameServer = MultiWorlds()->GetWorld(i)->m_pGameServer;
						pGameServer->PrepareClientChangeWorld(ClientID);
					}
					GameServer(WorldID)->OnClientConnected(ClientID);
				}

				m_aClients[ClientID].m_State = CClient::STATE_READY;
				SendConnectionReady(ClientID);
				SendCapabilities(ClientID);
				ExpireServerInfo();
			}
		}
		else if (MsgID == NETMSG_ENTERGAME)
		{
			const int WorldID = m_aClients[ClientID].m_WorldID;

			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State == CClient::STATE_READY && GameServer(WorldID)->IsClientReady(ClientID))
			{
				if (!m_aClients[ClientID].m_IsChangesWorld)
				{
					char aAddrStr[NETADDR_MAXSTRSIZE];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "player has entered the game. ClientID=%d addr=%s", ClientID, aAddrStr);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
				}

				m_aClients[ClientID].m_State = CClient::STATE_INGAME;
				GameServer(WorldID)->OnClientEnter(ClientID);
				ExpireServerInfo();
			}
		}
		else if (MsgID == NETMSG_INPUT)
		{
			CClient::CInput *pInput;
			int64 TagTime;

			m_aClients[ClientID].m_LastAckedSnapshot = Unpacker.GetInt();
			int IntendedTick = Unpacker.GetInt();
			int Size = Unpacker.GetInt();

			// check for errors
			if (Unpacker.Error() || Size / 4 > MAX_INPUT_SIZE)
				return;

			if (m_aClients[ClientID].m_LastAckedSnapshot > 0)
				m_aClients[ClientID].m_SnapRate = CClient::SNAPRATE_FULL;

			if (m_aClients[ClientID].m_Snapshots.Get(m_aClients[ClientID].m_LastAckedSnapshot, &TagTime, 0, 0) >= 0)
				m_aClients[ClientID].m_Latency = (int)(((time_get() - TagTime) * 1000) / time_freq());

			// add message to report the input timing
			// skip packets that are old
			if (IntendedTick > m_aClients[ClientID].m_LastInputTick)
			{
				int TimeLeft = ((TickStartTime(IntendedTick) - time_get()) * 1000) / time_freq();

				CMsgPacker Msgp(NETMSG_INPUTTIMING, true);
				Msgp.AddInt(IntendedTick);
				Msgp.AddInt(TimeLeft);
				SendMsg(&Msgp, 0, ClientID, -1, m_aClients[ClientID].m_WorldID);
			}

			m_aClients[ClientID].m_LastInputTick = IntendedTick;

			pInput = &m_aClients[ClientID].m_aInputs[m_aClients[ClientID].m_CurrentInput];

			if (IntendedTick <= Tick())
				IntendedTick = Tick() + 1;

			pInput->m_GameTick = IntendedTick;

			for (int i = 0; i < Size / 4; i++)
				pInput->m_aData[i] = Unpacker.GetInt();

			mem_copy(m_aClients[ClientID].m_LatestInput.m_aData, pInput->m_aData, MAX_INPUT_SIZE * sizeof(int));

			m_aClients[ClientID].m_CurrentInput++;
			m_aClients[ClientID].m_CurrentInput %= 200;

			// call the mod with the fresh input data
			if (m_aClients[ClientID].m_State == CClient::STATE_INGAME)
			{
				const int WorldID = m_aClients[ClientID].m_WorldID;
				GameServer(WorldID)->OnClientDirectInput(ClientID, m_aClients[ClientID].m_LatestInput.m_aData);
			}
		}
		else if (MsgID == NETMSG_RCON_CMD)
		{
			const char *pCmd = Unpacker.GetString();
			if (!str_utf8_check(pCmd))
			{
				return;
			}

			if (Unpacker.Error() == 0 && !str_comp(pCmd, "crashmeplx"))
			{
				int Version = m_aClients[ClientID].m_DDNetVersion;
				if (GameServer()->PlayerExists(ClientID) && Version < VERSION_DDNET_OLD)
				{
					m_aClients[ClientID].m_DDNetVersion = VERSION_DDNET_OLD;
				}
			}
			else if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0 && m_aClients[ClientID].m_Authed)
			{
				m_RconClientID = ClientID;
				m_RconAuthLevel = m_aClients[ClientID].m_Authed;
				Console()->SetAccessLevel(m_aClients[ClientID].m_Authed == AUTHED_ADMIN ? IConsole::ACCESS_LEVEL_ADMIN : IConsole::ACCESS_LEVEL_MOD);
				Console()->ExecuteLineFlag(pCmd, -1, CFGFLAG_SERVER);
				Console()->SetAccessLevel(IConsole::ACCESS_LEVEL_ADMIN);
				m_RconClientID = RCON_CID_SERV;
				m_RconAuthLevel = AUTHED_ADMIN;

				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "ClientID=%d rcon='%s'", ClientID, pCmd);
				Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBuf);
			}
		}
		else if (MsgID == NETMSG_RCON_AUTH)
		{
			const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC); // login name, now used
			const char *pPw = Unpacker.GetString(CUnpacker::SANITIZE_CC);
			if (!str_utf8_check(pPw) || !str_utf8_check(pName))
				return;

			if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Unpacker.Error() == 0)
			{
				if (g_Config.m_SvRconModPassword[0] && str_comp(pPw, g_Config.m_SvRconModPassword) == 0)
					m_aClients[ClientID].m_Authed = AUTHED_MOD;
				else if (g_Config.m_SvRconPassword[0] && str_comp(pPw, g_Config.m_SvRconPassword) == 0)
					m_aClients[ClientID].m_Authed = AUTHED_ADMIN;
				else if (g_Config.m_SvRconMaxTries)
				{
					m_aClients[ClientID].m_AuthTries++;
					char aBuf[128];
					str_format(aBuf, sizeof(aBuf), "Wrong password %d/%d.", m_aClients[ClientID].m_AuthTries, g_Config.m_SvRconMaxTries);
					SendRconLine(ClientID, aBuf);
					if (m_aClients[ClientID].m_AuthTries >= g_Config.m_SvRconMaxTries)
					{
						if (!g_Config.m_SvRconBantime)
							m_NetServer.Drop(ClientID, "Too many remote console authentication tries");
					}
				}
				else
				{
					SendRconLine(ClientID, "Wrong password.");
				}

				// authed
				if (m_aClients[ClientID].m_Authed != AUTHED_NO)
				{
					CMsgPacker Msgp(NETMSG_RCON_AUTH_STATUS, true);
					Msgp.AddInt(1); // authed
					Msgp.AddInt(1); // cmdlist
					SendMsg(&Msgp, MSGFLAG_VITAL, ClientID);

					char aBuf[256];
					char aAddrStr[NETADDR_MAXSTRSIZE];
					net_addr_str(m_NetServer.ClientAddr(ClientID), aAddrStr, sizeof(aAddrStr), true);
					if (m_aClients[ClientID].m_Authed == AUTHED_MOD)
					{
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_MOD, CFGFLAG_SERVER);
						SendRconLine(ClientID, "Moderator authentication successful. Limited remote console access granted.");
						str_format(aBuf, sizeof(aBuf), "ClientID=%d addr=%s authed (moderator)", ClientID, aAddrStr);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
					}
					else if (m_aClients[ClientID].m_Authed == AUTHED_ADMIN)
					{
						m_aClients[ClientID].m_pRconCmdToSend = Console()->FirstCommandInfo(IConsole::ACCESS_LEVEL_ADMIN, CFGFLAG_SERVER);
						SendRconLine(ClientID, "Admin authentication successful. Full remote console access granted.");
						str_format(aBuf, sizeof(aBuf), "ClientID=%d addr=%s authed (admin)", ClientID, aAddrStr);
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
					}
				}
			}
		}
		else if (MsgID == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY, true);
			SendMsg(&Msg, 0, ClientID, -1, m_aClients[ClientID].m_WorldID);
		}
		else if (MsgID == NETMSG_PINGEX)
		{
			CUuid *pID = (CUuid *)Unpacker.GetRaw(sizeof(*pID));
			if (Unpacker.Error())
			{
				return;
			}
			CMsgPacker Msgp(NETMSG_PONGEX, true);
			Msgp.AddRaw(pID, sizeof(*pID));
			SendMsg(&Msgp, MSGFLAG_FLUSH, ClientID, m_aClients[ClientID].m_WorldID);
		}
		else
		{
			if (g_Config.m_Debug)
			{
				char aHex[] = "0123456789ABCDEF";
				char aBuf[512];

				for (int b = 0; b < pPacket->m_DataSize && b < 32; b++)
				{
					aBuf[b * 3] = aHex[((const unsigned char *)pPacket->m_pData)[b] >> 4];
					aBuf[b * 3 + 1] = aHex[((const unsigned char *)pPacket->m_pData)[b] & 0xf];
					aBuf[b * 3 + 2] = ' ';
					aBuf[b * 3 + 3] = 0;
				}

				char aBufMsg[256];
				str_format(aBufMsg, sizeof(aBufMsg), "strange message ClientID=%d msg=%d data_size=%d", ClientID, MsgID, pPacket->m_DataSize);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBufMsg);
				Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
			}
		}
	}
	else
	{
		// game message
		if ((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && m_aClients[ClientID].m_State >= CClient::STATE_READY)
		{
			const int WorldID = m_aClients[ClientID].m_WorldID;
			GameServer(WorldID)->OnMessage(MsgID, &Unpacker, ClientID);
		}
	}
}

bool CServer::RateLimitServerInfoConnless()
{
	bool SendClients = true;
	if (g_Config.m_SvServerInfoPerSecond)
	{
		SendClients = m_ServerInfoNumRequests <= g_Config.m_SvServerInfoPerSecond;
		const int64_t Now = Tick();

		if (Now <= m_ServerInfoFirstRequest + TickSpeed())
		{
			m_ServerInfoNumRequests++;
		}
		else
		{
			m_ServerInfoNumRequests = 1;
			m_ServerInfoFirstRequest = Now;
		}
	}

	return SendClients;
}

void CServer::SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type)
{
	SendServerInfo(pAddr, Token, Type, RateLimitServerInfoConnless());
}

static inline int GetCacheIndex(int Type, bool SendClient)
{
	if (Type == SERVERINFO_INGAME)
		Type = SERVERINFO_VANILLA;
	else if (Type == SERVERINFO_EXTENDED_MORE)
		Type = SERVERINFO_EXTENDED;

	return Type * 2 + SendClient;
}

CServer::CCache::CCache()
{
	m_Cache.clear();
}

CServer::CCache::~CCache()
{
	Clear();
}

CServer::CCache::CCacheChunk::CCacheChunk(const void *pData, int Size)
{
	m_vData.assign((const uint8_t *)pData, (const uint8_t *)pData + Size);
}

void CServer::CCache::AddChunk(const void *pData, int Size)
{
	m_Cache.emplace_back(pData, Size);
}

void CServer::CCache::Clear()
{
	m_Cache.clear();
}

void CServer::CacheServerInfo(CCache *pCache, int Type, bool SendClients)
{
	pCache->Clear();

	// One chance to improve the protocol!
	CPacker p;
	char aBuf[128];

	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if (GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	p.Reset();

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x)                            \
	do                                           \
	{                                            \
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0);                  \
	} while (0)

	p.AddString(GameServer()->Version(), 32);
	if (Type != SERVERINFO_VANILLA)
	{
		p.AddString(g_Config.m_SvName, 256);
	}
	else
	{
		if (m_NetServer.MaxClients() <= VANILLA_MAX_CLIENTS)
		{
			p.AddString(g_Config.m_SvName, 64);
		}
		else
		{
			str_format(aBuf, sizeof(aBuf), "%s [%d/%d]", g_Config.m_SvName, ClientCount, m_NetServer.MaxClients());
			p.AddString(aBuf, 64);
		}
	}
	p.AddString(g_Config.m_SvMap, 32);

	if (Type == SERVERINFO_EXTENDED)
	{
		ADD_INT(p, MultiWorlds()->GetWorld(MAIN_WORLD_ID)->m_pLoadedMap->Crc());
		ADD_INT(p, MultiWorlds()->GetWorld(MAIN_WORLD_ID)->m_pLoadedMap->GetCurrentMapSize());
	}

	// gametype
	p.AddString("Tee:World", 16);

	// flags
	ADD_INT(p, g_Config.m_Password[0] ? SERVER_FLAG_PASSWORD : 0);

	int MaxClients = m_NetServer.MaxClients();
	// How many clients the used serverinfo protocol supports, has to be tracked
	// separately to make sure we don't subtract the reserved slots from it
	int MaxClientsProtocol = MAX_PLAYERS;
	if (Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
	{
		if (ClientCount >= VANILLA_MAX_CLIENTS)
		{
			if (ClientCount < MaxClients)
				ClientCount = VANILLA_MAX_CLIENTS - 1;
			else
				ClientCount = VANILLA_MAX_CLIENTS;
		}
		MaxClientsProtocol = VANILLA_MAX_CLIENTS;
		if (PlayerCount > ClientCount)
			PlayerCount = ClientCount;
	}

	ADD_INT(p, PlayerCount);		// num players
	ADD_INT(p, MaxClientsProtocol); // max players
	ADD_INT(p, ClientCount);		// num clients
	ADD_INT(p, MaxClientsProtocol); // max clients

	if (Type == SERVERINFO_EXTENDED)
		p.AddString("", 0); // extra info, reserved

	const void *pPrefix = p.Data();
	int PrefixSize = p.Size();

	CPacker q;
	int ChunksStored = 0;
	int PlayersStored = 0;

#define SAVE(size)                        \
	do                                    \
	{                                     \
		pCache->AddChunk(q.Data(), size); \
		ChunksStored++;                   \
	} while (0)

#define RESET()                        \
	do                                 \
	{                                  \
		q.Reset();                     \
		q.AddRaw(pPrefix, PrefixSize); \
	} while (0)

	RESET();

	if (Type == SERVERINFO_64_LEGACY)
		q.AddInt(PlayersStored); // offset

	if (!SendClients)
	{
		SAVE(q.Size());
		return;
	}

	if (Type == SERVERINFO_EXTENDED)
	{
		pPrefix = "";
		PrefixSize = 0;
	}

	int Remaining;
	switch (Type)
	{
	case SERVERINFO_EXTENDED:
		Remaining = -1;
		break;
	case SERVERINFO_64_LEGACY:
		Remaining = 24;
		break;
	case SERVERINFO_VANILLA:
		Remaining = VANILLA_MAX_CLIENTS;
		break;
	case SERVERINFO_INGAME:
		Remaining = VANILLA_MAX_CLIENTS;
		break;
	default:
		dbg_assert(0, "caught earlier, unreachable");
		return;
	}

	// Use the following strategy for sending:
	// For vanilla, send the first 16 players.
	// For legacy 64p, send 24 players per packet.
	// For extended, send as much players as possible.

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if (Remaining == 0)
			{
				if (Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
					break;

				// Otherwise we're SERVERINFO_64_LEGACY.
				SAVE(q.Size());
				RESET();
				q.AddInt(PlayersStored); // offset
				Remaining = 24;
			}

			if (Remaining > 0)
			{
				Remaining--;
			}

			int PreviousSize = q.Size();
			q.AddString(ClientName(i), MAX_NAME_LENGTH); // client name
			q.AddString(ClientClan(i), MAX_CLAN_LENGTH); // client clan

			ADD_INT(q, m_aClients[i].m_Country);				 // client country
			ADD_INT(q, m_aClients[i].m_Score);					 // client score
			ADD_INT(q, GameServer()->IsClientPlayer(i) ? 1 : 0); // is player?
			if (Type == SERVERINFO_EXTENDED)
				q.AddString("", 0); // extra info, reserved

			if (Type == SERVERINFO_EXTENDED)
			{
				if (q.Size() >= NET_MAX_PAYLOAD - 18) // 8 bytes for type, 10 bytes for the largest token
				{
					// Retry current player.
					i--;
					SAVE(PreviousSize);
					RESET();
					ADD_INT(q, ChunksStored);
					q.AddString("", 0); // extra info, reserved
					continue;
				}
			}
			PlayersStored++;
		}
	}

	SAVE(q.Size());
#undef SAVE
#undef RESET
#undef ADD_RAW
#undef ADD_INT
}

void CServer::SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients)
{
	CPacker p;
	char aBuf[128];
	p.Reset();

	CCache *pCache = &m_aServerInfoCache[GetCacheIndex(Type, SendClients)];

#define ADD_RAW(p, x) (p).AddRaw(x, sizeof(x))
#define ADD_INT(p, x)                            \
	do                                           \
	{                                            \
		str_format(aBuf, sizeof(aBuf), "%d", x); \
		(p).AddString(aBuf, 0);                  \
	} while (0)

	CNetChunk Packet;
	Packet.m_ClientID = -1;
	Packet.m_Address = *pAddr;
	Packet.m_Flags = NETSENDFLAG_CONNLESS;

	for (const auto &Chunk : pCache->m_Cache)
	{
		p.Reset();
		if (Type == SERVERINFO_EXTENDED)
		{
			if (&Chunk == &pCache->m_Cache.front())
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED, sizeof(SERVERBROWSE_INFO_EXTENDED));
			else
				p.AddRaw(SERVERBROWSE_INFO_EXTENDED_MORE, sizeof(SERVERBROWSE_INFO_EXTENDED_MORE));
			ADD_INT(p, Token);
		}
		else if (Type == SERVERINFO_64_LEGACY)
		{
			ADD_RAW(p, SERVERBROWSE_INFO_64_LEGACY);
			ADD_INT(p, Token);
		}
		else if (Type == SERVERINFO_VANILLA || Type == SERVERINFO_INGAME)
		{
			ADD_RAW(p, SERVERBROWSE_INFO);
			ADD_INT(p, Token);
		}
		else
		{
			dbg_assert(false, "unknown serverinfo type");
		}

		p.AddRaw(Chunk.m_vData.data(), Chunk.m_vData.size());
		Packet.m_pData = p.Data();
		Packet.m_DataSize = p.Size();
		m_NetServer.Send(&Packet);
	}
}

void CServer::ExpireServerInfo()
{
	m_ServerInfoNeedsUpdate = true;
}

void CServer::UpdateRegisterServerInfo()
{
	// count the players
	int PlayerCount = 0, ClientCount = 0;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			if (GameServer()->IsClientPlayer(i))
				PlayerCount++;

			ClientCount++;
		}
	}

	int MaxPlayers = max(m_NetServer.MaxClients() - g_Config.m_SvSpectatorSlots, PlayerCount);
	int MaxClients = max(m_NetServer.MaxClients(), ClientCount);
	char aName[256];
	char aGameType[32];
	char aMapName[64];
	char aVersion[64];
	char aMapSha256[SHA256_MAXSTRSIZE];

	IEngineMap *pMap = MultiWorlds()->GetWorld(MAIN_WORLD_ID)->m_pLoadedMap;

	sha256_str(pMap->Sha256(), aMapSha256, sizeof(aMapSha256));

	char aInfo[16384];
	str_format(aInfo, sizeof(aInfo),
			   "{"
			   "\"max_clients\":%d,"
			   "\"max_players\":%d,"
			   "\"passworded\":%s,"
			   "\"game_type\":\"%s\","
			   "\"name\":\"%s\","
			   "\"map\":{"
			   "\"name\":\"%s\","
			   "\"sha256\":\"%s\","
			   "\"size\":%d"
			   "},"
			   "\"version\":\"%s\","
			   "\"clients\":[",
			   MaxClients,
			   MaxPlayers,
			   JsonBool(g_Config.m_Password[0]),
			   EscapeJson(aGameType, sizeof(aGameType), GameServer()->GameType()),
			   EscapeJson(aName, sizeof(aName), g_Config.m_SvName),
			   EscapeJson(aMapName, sizeof(aMapName), MultiWorlds()->GetWorld(MAIN_WORLD_ID)->m_aName),
			   aMapSha256,
			   pMap->GetCurrentMapSize(),
			   EscapeJson(aVersion, sizeof(aVersion), GameServer()->Version()));

	bool FirstPlayer = true;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			char aCName[32];
			char aCClan[32];

			char aClientInfo[256];
			str_format(aClientInfo, sizeof(aClientInfo),
					   "%s{"
					   "\"name\":\"%s\","
					   "\"clan\":\"%s\","
					   "\"country\":%d,"
					   "\"score\":%d,"
					   "\"is_player\":%s"
					   "}",
					   !FirstPlayer ? "," : "",
					   EscapeJson(aCName, sizeof(aCName), ClientName(i)),
					   EscapeJson(aCClan, sizeof(aCClan), ClientClan(i)),
					   m_aClients[i].m_Country,
					   m_aClients[i].m_Score,
					   JsonBool(GameServer()->IsClientPlayer(i)));
			str_append(aInfo, aClientInfo, sizeof(aInfo));
			FirstPlayer = false;
		}
	}

	str_append(aInfo, "]}", sizeof(aInfo));

	m_pRegister->OnNewInfo(aInfo);
}

void CServer::UpdateServerInfo(bool Resend)
{
	if (!m_pRegister)
		return;

	UpdateRegisterServerInfo();

	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 2; j++)
			CacheServerInfo(&m_aServerInfoCache[i * 2 + j], i, j);

	if (Resend)
	{
		for (int i = 0; i < MaxClients(); ++i)
		{
			if (m_aClients[i].m_State != CClient::STATE_EMPTY && !m_aClients[i].m_IsBot)
			{
				SendServerInfo(m_NetServer.ClientAddr(i), -1, SERVERINFO_INGAME, false);
			}
		}
	}

	m_ServerInfoNeedsUpdate = false;
}

void CServer::PumpNetwork(bool PacketWaiting)
{
	CNetChunk Packet;
	SECURITY_TOKEN ResponseToken;

	m_NetServer.Update();

	if (PacketWaiting)
	{
		// process packets
		while (m_NetServer.Recv(&Packet, &ResponseToken))
		{
			if (Packet.m_ClientID == -1)
			{
				if (ResponseToken == NET_SECURITY_TOKEN_UNKNOWN && m_pRegister->OnPacket(&Packet))
					continue;

				{
					int ExtraToken = 0;
					int Type = -1;
					if (Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO) + 1 &&
						mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO, sizeof(SERVERBROWSE_GETINFO)) == 0)
					{
						if (Packet.m_Flags & NETSENDFLAG_EXTENDED)
						{
							Type = SERVERINFO_EXTENDED;
							ExtraToken = (Packet.m_aExtraData[0] << 8) | Packet.m_aExtraData[1];
						}
						else
							Type = SERVERINFO_VANILLA;
					}
					else if (Packet.m_DataSize >= (int)sizeof(SERVERBROWSE_GETINFO_64_LEGACY) + 1 &&
							 mem_comp(Packet.m_pData, SERVERBROWSE_GETINFO_64_LEGACY, sizeof(SERVERBROWSE_GETINFO_64_LEGACY)) == 0)
					{
						Type = SERVERINFO_64_LEGACY;
					}
					if (Type != -1)
					{
						int Token = ((unsigned char *)Packet.m_pData)[sizeof(SERVERBROWSE_GETINFO)];
						Token |= ExtraToken << 8;
						SendServerInfoConnless(&Packet.m_Address, Token, Type);
					}
				}
			}
			else
			{
				int GameFlags = 0;
				if (Packet.m_Flags & NET_CHUNKFLAG_VITAL)
				{
					GameFlags |= MSGFLAG_VITAL;
				}

				ProcessClientPacket(&Packet);
			}
		}
	}
	{
		unsigned char aBuffer[NET_MAX_PAYLOAD];
		int Flags;
		mem_zero(&Packet, sizeof(Packet));
		Packet.m_pData = aBuffer;
	}

	m_ServerBan.Update();
	m_Econ.Update();
}

char *CServer::GetMapName()
{
	// get the name of the map without his path
	char *pMapShortName = &g_Config.m_SvMap[0];
	for (int i = 0; i < str_length(g_Config.m_SvMap) - 1; i++)
	{
		if (g_Config.m_SvMap[i] == '/' || g_Config.m_SvMap[i] == '\\')
			pMapShortName = &g_Config.m_SvMap[i + 1];
	}
	return pMapShortName;
}

bool CServer::LoadMap(int ID)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "maps/%s", MultiWorlds()->GetWorld(ID)->m_aPath);

	IEngineMap *pMap = MultiWorlds()->GetWorld(ID)->m_pLoadedMap;
	if (!pMap->Load(aBuf))
		return false;

	// reinit snapshot ids
	m_IDPool.TimeoutIDs();

	// get the sha256 and crc of the map
	char aSha256[SHA256_MAXSTRSIZE];
	sha256_str(pMap->Sha256(), aSha256, sizeof(aSha256));
	char aBufMsg[256];
	str_format(aBufMsg, sizeof(aBufMsg), "%s sha256 is %s", aBuf, aSha256);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);
	str_format(aBufMsg, sizeof(aBufMsg), "%s crc is %08x", aBuf, pMap->Crc());
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "server", aBufMsg);

	// load complete map into memory for download
	{
		IOHANDLE File = Storage()->OpenFile(aBuf, IOFLAG_READ, IStorage::TYPE_ALL);
		pMap->SetCurrentMapSize((int)io_length(File));
		pMap->SetCurrentMapData((unsigned char *)mem_alloc(pMap->GetCurrentMapSize(), 1));
		io_read(File, pMap->GetCurrentMapData(), pMap->GetCurrentMapSize());
		io_close(File);
	}
	return true;
}

int CServer::Run()
{
	//
	m_PrintCBIndex = Console()->RegisterPrintCallback(g_Config.m_ConsoleOutputLevel, SendRconLineAuthed, this);
	m_MapChunksPerRequest = g_Config.m_SvMapDownloadSpeed;
	m_DataChunksPerRequest = g_Config.m_SvMapDownloadSpeed;

	// loading maps to memory
	char aBuf[256];
	for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
	{
		if (!LoadMap(i))
		{
			str_format(aBuf, sizeof(aBuf), "maps/%s the map is not loaded...", MultiWorlds()->GetWorld(i)->m_aPath);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return -1;
		}
	}

	// start server
	NETADDR BindAddr;
	if (g_Config.m_Bindaddr[0] && net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
	{
		// sweet!
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}
	else
	{
		mem_zero(&BindAddr, sizeof(BindAddr));
		BindAddr.type = NETTYPE_ALL;
		BindAddr.port = g_Config.m_SvPort;
	}

	int Port = g_Config.m_SvPort;
	for (BindAddr.port = Port != 0 ? Port : 8303; !m_NetServer.Open(BindAddr, &m_ServerBan, g_Config.m_SvMaxClients, g_Config.m_SvMaxClientsPerIP); BindAddr.port++)
	{
		if (Port != 0 || BindAddr.port >= 8310)
		{
			dbg_msg("server", "couldn't open socket. port %d might already be in use", BindAddr.port);
			return -1;
		}
	}

	IEngine *pEngine = Kernel()->RequestInterface<IEngine>();
	m_pRegister = CreateRegister(m_pConsole, pEngine, g_Config.m_SvPort, m_NetServer.GetGlobalToken());

	m_NetServer.SetCallbacks(NewClientCallback, NewClientNoAuthCallback, ClientRejoinCallback, DelClientCallback, this);

	m_Econ.Init(Console(), &m_ServerBan);

	str_format(aBuf, sizeof(aBuf), "server name is '%s'", g_Config.m_SvName);
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	if (!MultiWorlds()->GetSizeInitilized())
	{
		dbg_msg("server", "the worlds were not found or were not initialized");
		return -1;
	}
	for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
		MultiWorlds()->GetWorld(i)->m_pGameServer->OnInit(i);
	str_format(aBuf, sizeof(aBuf), "version %s", GameServer()->NetVersion());
	Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// process pending commands
	m_pConsole->StoreCommands(false);
	m_pRegister->OnConfigChange();

	// start game
	{
		bool NonActive = false;
		bool PacketWaiting = false;

		m_GameStartTime = time_get();

		UpdateServerInfo();
		while (m_RunServer)
		{
			bool ExistsPlayers = false;
			bool ShouldSnap = false;
			if (NonActive)
				PumpNetwork(PacketWaiting);

			set_new_tick();

			int64_t t = time_get();
			int NewTicks = 0;

			while (t > TickStartTime(m_CurrentGameTick + 1))
			{
				m_CurrentGameTick++;
				if ((m_CurrentGameTick % 2) == 0)
					ShouldSnap = true;

				NewTicks++;

				// apply new input
				for (int c = 0; c < MAX_PLAYERS; c++)
				{
					if (m_aClients[c].m_State == CClient::STATE_EMPTY)
						continue;

					ExistsPlayers = true;
					for (auto &m_aInput : m_aClients[c].m_aInputs)
					{
						if (m_aInput.m_GameTick == Tick())
						{
							if (m_aClients[c].m_State == CClient::STATE_INGAME)
							{
								const int WorldID = m_aClients[c].m_WorldID;
								GameServer(WorldID)->OnClientPredictedInput(c, m_aInput.m_aData);
							}
							break;
						}
					}
				}
			}

			MultiWorlds()->GetWorld(MAIN_WORLD_ID)->m_pGameServer->OnTickMainWorld();
			for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
			{
				IGameServer *pGameServer = MultiWorlds()->GetWorld(i)->m_pGameServer;
				pGameServer->OnTick();
			}

			if (NewTicks)
			{
				// heavy reset server ((24 * 60) * 60) * SERVER_TICK_SPEED = 4320000 tick in day i think
				if (false)
				{
					m_CurrentGameTick = 0;
					m_GameStartTime = time_get();
					m_ServerInfoFirstRequest = 0;

					if (!MultiWorlds()->LoadWorlds(this, Kernel(), Storage(), Console()))
					{
						str_format(aBuf, sizeof(aBuf), "interfaces for heavy reload could not be updated...");
						Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
						return -1;
					}

					for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
					{
						// load map data
						if (!LoadMap(i))
						{
							str_format(aBuf, sizeof(aBuf), "maps/%s the map is not loaded...", MultiWorlds()->GetWorld(i)->m_aPath);
							Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
							return -1;
						}
					}

					if (m_HeavyReload)
					{
						// reload players
						for (int ClientID = 0; ClientID < MAX_PLAYERS; ClientID++)
						{
							if (m_aClients[ClientID].m_State <= CClient::STATE_AUTH)
								continue;

							m_aClients[ClientID].Reset();
							m_aClients[ClientID].m_State = CClient::STATE_CONNECTING;
							m_aClients[ClientID].m_IsChangesWorld = false;
							SendMap(ClientID);
						}
						m_HeavyReload = false;
					}
					else
					{
						Init();
					}

					// reinit gamecontext
					for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
					{
						IGameServer *pGameServer = MultiWorlds()->GetWorld(i)->m_pGameServer;
						pGameServer->OnInit(i);
					}

					UpdateServerInfo(true);
					Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "A server was heavy reload.");
				}
				else
				{
					// snap game
					if (g_Config.m_SvHighBandwidth || ShouldSnap)
					{
						for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
							DoSnapshot(i);
					}
					UpdateClientRconCommands();
				}
			}

			// master server stuff
			m_pRegister->Update();

			if (m_ServerInfoNeedsUpdate)
				UpdateServerInfo();

			if (!NonActive)
				PumpNetwork(PacketWaiting);

			NonActive = true;

			for (auto &Client : m_aClients)
			{
				if (Client.m_State != CClient::STATE_EMPTY)
				{
					NonActive = false;
					break;
				}
			}
			m_ReloadedWhenEmpty = false;

			set_new_tick();
			t = time_get();
			int x = (TickStartTime(m_CurrentGameTick + 1) - t) * 1000000 / time_freq() + 1;

			PacketWaiting = x > 0 ? net_socket_read_wait(m_NetServer.Socket(), x) : true;
			dbg_msg("sa", "%d", x);
		}
	}
	// disconnect all clients on shutdown
	for (int i = 0; i < MAX_CLIENTS; ++i)
	{
		if (m_aClients[i].m_State != CClient::STATE_EMPTY)
			m_NetServer.Drop(i, "Server shutdown");

		m_Econ.Shutdown();
	}

	m_pRegister->OnShutdown();
	return 0;
}

void CServer::ConKick(IConsole::IResult *pResult, void *pUser)
{
	if (pResult->NumArguments() > 1)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "Kicked (%s)", pResult->GetString(1));
		((CServer *)pUser)->Kick(pResult->GetInteger(0), aBuf);
	}
	else
		((CServer *)pUser)->Kick(pResult->GetInteger(0), "Kicked by console");
}

void CServer::ConStatus(IConsole::IResult *pResult, void *pUser)
{
	char aBuf[1024];
	char aAddrStr[NETADDR_MAXSTRSIZE];
	CServer *pThis = static_cast<CServer *>(pUser);

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (pThis->m_aClients[i].m_State != CClient::STATE_EMPTY)
		{
			net_addr_str(pThis->m_NetServer.ClientAddr(i), aAddrStr, sizeof(aAddrStr), true);
			if (pThis->m_aClients[i].m_State == CClient::STATE_INGAME)
			{
				const char *pAuthStr = pThis->m_aClients[i].m_Authed == CServer::AUTHED_ADMIN ? "(Admin)" : pThis->m_aClients[i].m_Authed == CServer::AUTHED_MOD ? "(Mod)"
																																								 : "";
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s name='%s' score=%d secure=%s %s", i, aAddrStr,
						   pThis->m_aClients[i].m_aName, pThis->m_aClients[i].m_Score, pThis->m_NetServer.HasSecurityToken(i) ? "yes" : "no", pAuthStr);
			}
			else
				str_format(aBuf, sizeof(aBuf), "id=%d addr=%s connecting", i, aAddrStr);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		}
	}
}

void CServer::ConShutdown(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_RunServer = 0;
}

void CServer::DemoRecorder_HandleAutoStart()
{
	return; // I HATE THIS SHIT, it record 40GB demo file while I test server...
}

bool CServer::DemoRecorder_IsRecording()
{
	return m_DemoRecorder.IsRecording();
}

void CServer::ConRecord(IConsole::IResult *pResult, void *pUser)
{
	return; // Both.
}

void CServer::ConStopRecord(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_DemoRecorder.Stop();
}

void CServer::ConMapReload(IConsole::IResult *pResult, void *pUser)
{
	((CServer *)pUser)->m_HeavyReload = true;
}

void CServer::ConLogout(IConsole::IResult *pResult, void *pUser)
{
	CServer *pServer = (CServer *)pUser;

	if (pServer->m_RconClientID >= 0 && pServer->m_RconClientID < MAX_CLIENTS &&
		pServer->m_aClients[pServer->m_RconClientID].m_State != CServer::CClient::STATE_EMPTY)
	{
		CMsgPacker Msg(NETMSG_RCON_AUTH_STATUS, true);
		Msg.AddInt(0); // authed
		Msg.AddInt(0); // cmdlist
		pServer->SendMsg(&Msg, MSGFLAG_VITAL, pServer->m_RconClientID);

		pServer->m_aClients[pServer->m_RconClientID].m_Authed = AUTHED_NO;
		pServer->m_aClients[pServer->m_RconClientID].m_AuthTries = 0;
		pServer->m_aClients[pServer->m_RconClientID].m_pRconCmdToSend = 0;
		pServer->SendRconLine(pServer->m_RconClientID, "Logout successful.");
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "ClientID=%d logged out", pServer->m_RconClientID);
		pServer->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
	}
}

void CServer::ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if (pResult->NumArguments())
		((CServer *)pUserData)->UpdateServerInfo();
}

void CServer::ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if (pResult->NumArguments())
		((CServer *)pUserData)->m_NetServer.SetMaxClientsPerIP(pResult->GetInteger(0));
}

void CServer::ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	if (pResult->NumArguments() == 2)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		const IConsole::CCommandInfo *pInfo = pThis->Console()->GetCommandInfo(pResult->GetString(0), CFGFLAG_SERVER, false);
		int OldAccessLevel = 0;
		if (pInfo)
			OldAccessLevel = pInfo->GetAccessLevel();
		pfnCallback(pResult, pCallbackUserData);
		if (pInfo && OldAccessLevel != pInfo->GetAccessLevel())
		{
			for (int i = 0; i < MAX_CLIENTS; ++i)
			{
				if (pThis->m_aClients[i].m_State == CServer::CClient::STATE_EMPTY || pThis->m_aClients[i].m_Authed != CServer::AUTHED_MOD ||
					(pThis->m_aClients[i].m_pRconCmdToSend && str_comp(pResult->GetString(0), pThis->m_aClients[i].m_pRconCmdToSend->m_pName) >= 0))
					continue;

				if (OldAccessLevel == IConsole::ACCESS_LEVEL_ADMIN)
					pThis->SendRconCmdAdd(pInfo, i);
				else
					pThis->SendRconCmdRem(pInfo, i);
			}
		}
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CServer::ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if (pResult->NumArguments() == 1)
	{
		CServer *pThis = static_cast<CServer *>(pUserData);
		pThis->Console()->SetPrintOutputLevel(pThis->m_PrintCBIndex, pResult->GetInteger(0));
	}
}

void CServer::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	HttpInit(m_pStorage);

	// register console commands
	Console()->Register("kick", "i?r", CFGFLAG_SERVER, ConKick, this, "Kick player with specified id for any reason");
	Console()->Register("status", "", CFGFLAG_SERVER, ConStatus, this, "List players");
	Console()->Register("shutdown", "", CFGFLAG_SERVER, ConShutdown, this, "Shut down");
	Console()->Register("logout", "", CFGFLAG_SERVER, ConLogout, this, "Logout of rcon");

	Console()->Register("record", "?s", CFGFLAG_SERVER | CFGFLAG_STORE, ConRecord, this, "Record to a file");
	Console()->Register("stoprecord", "", CFGFLAG_SERVER, ConStopRecord, this, "Stop recording");

	Console()->Register("reload", "", CFGFLAG_SERVER, ConMapReload, this, "Reload the map");

	Console()->Chain("sv_name", ConchainSpecialInfoupdate, this);
	Console()->Chain("password", ConchainSpecialInfoupdate, this);

	Console()->Chain("sv_max_clients_per_ip", ConchainMaxclientsperipUpdate, this);
	Console()->Chain("mod_command", ConchainModCommandUpdate, this);
	Console()->Chain("console_output_level", ConchainConsoleOutputLevelUpdate, this);
	// register console commands in sub parts
	m_ServerBan.InitServerBan(Console(), Storage(), this);
	for (int i = 0; i < MultiWorlds()->GetSizeInitilized(); i++)
		MultiWorlds()->GetWorld(i)->m_pGameServer->OnConsoleInit();
}

int CServer::SnapNewID()
{
	return m_IDPool.NewID();
}

void CServer::SnapFreeID(int ID)
{
	m_IDPool.FreeID(ID);
}

void *CServer::SnapNewItem(int Type, int ID, int Size)
{
	dbg_assert(ID >= 0 && ID <= 0xffff, "incorrect id");
	return ID < 0 ? 0 : m_SnapshotBuilder.NewItem(Type, ID, Size);
}

void CServer::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

static CServer *CreateServer() { return new CServer(); }

int main(int argc, const char **argv) // ignore_convention
{
#if defined(CONF_FAMILY_WINDOWS)
	for (int i = 1; i < argc; i++) // ignore_convention
	{
		if (str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			ShowWindow(GetConsoleWindow(), SW_HIDE);
			break;
		}
	}
#endif
	bool UseDefaultConfig = false;
	for (int i = 1; i < argc; i++) // ignore_convention
	{
		if (str_comp("-d", argv[i]) == 0 || str_comp("--default", argv[i]) == 0) // ignore_convention
		{
			UseDefaultConfig = true;
			break;
		}
	}

	if (secure_random_init() != 0)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}

	CServer *pServer = CreateServer();
	IKernel *pKernel = IKernel::Create();

	// create the components
	IEngine *pEngine = CreateEngine("Teeworlds", 2);

	IGameServer *pGameServer = CreateGameServer();
	IConsole *pConsole = CreateConsole(CFGFLAG_SERVER | CFGFLAG_ECON);
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_SERVER, argc, argv); // ignore_convention
	IConfig *pConfig = CreateConfig();

	pServer->m_pLocalization = new CLocalization(pStorage);
	pServer->m_pLocalization->InitConfig(0, NULL);
	if (!pServer->m_pLocalization->Init())
	{
		dbg_msg("localization", "could not initialize localization");
		return -1;
	}

	bool RegisterFail = false;

	RegisterFail = RegisterFail || !pKernel->RegisterInterface(pServer); // register as both
	RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
	RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
	RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
	RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfig);
	RegisterFail = RegisterFail || !pServer->MultiWorlds()->LoadWorlds(pServer, pKernel, pStorage, pConsole);

	if (RegisterFail)
		return -1;

	pEngine->Init();
	pConfig->Init();

	// register all console commands
	pServer->RegisterCommands();

	// execute autoexec file
	pConsole->ExecuteFile("autoexec.cfg");

	// parse the command line arguments
	if (argc > 1)									  // ignore_convention
		pConsole->ParseArguments(argc - 1, &argv[1]); // ignore_convention

	// restore empty config strings to their defaults
	pConfig->RestoreStrings();

	// run the server
	dbg_msg("server", "starting...");
	pServer->Run();

	// free
	delete pServer->m_pLocalization;
	delete pServer;
	delete pKernel;
	delete pConsole;
	delete pStorage;
	delete pConfig;
	return 0;
}

int CServer::NewBot(int ClientID)
{
	if (m_aClients[ClientID].m_State > CClient::STATE_EMPTY && !m_aClients[ClientID].m_IsBot)
		return 1;
	m_aClients[ClientID].m_State = CClient::STATE_INGAME;
	m_aClients[ClientID].m_IsBot = true;
	return 0;
}

int CServer::DelBot(int ClientID)
{
	if (!m_aClients[ClientID].m_IsBot)
		return 1;
	m_aClients[ClientID].m_State = CClient::STATE_EMPTY;
	m_aClients[ClientID].m_aName[0] = 0;
	m_aClients[ClientID].m_aClan[0] = 0;
	m_aClients[ClientID].m_Country = -1;
	m_aClients[ClientID].m_Authed = AUTHED_NO;
	m_aClients[ClientID].m_AuthTries = 0;
	m_aClients[ClientID].m_pRconCmdToSend = 0;
	m_aClients[ClientID].m_IsBot = false;
	m_aClients[ClientID].m_Snapshots.PurgeAll();
	return 0;
}

int *CServer::GetIdMap(int ClientID)
{
	return (int *)(IdMap + VANILLA_MAX_CLIENTS * ClientID);
}

void CServer::SetCustClt(int ClientID)
{
	m_aClients[ClientID].m_CustClt = 1;
}