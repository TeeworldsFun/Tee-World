/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H
#include "kernel.h"
#include "message.h"

#include <game/generated/protocol.h>
#include <engine/shared/protocol.h>

#include <base/math.h>

class IServer : public IInterface
{
	MACRO_INTERFACE("server", 0)
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	class CLocalization* m_pLocalization;
	enum
	{
		AUTHED_NO=0,
		AUTHED_MOD,
		AUTHED_ADMIN,
	};
public:
	virtual class IGameServer* GameServer(int WorldID = 0) = 0;
	virtual class IGameServer* GameServerPlayer(int ClientID) = 0;
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
		int m_Authed;
		bool m_CustClt;
	};

	inline class CLocalization* Localization() { return m_pLocalization; }

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int MaxClients() const = 0;
	virtual const char *ClientName(int ClientID) = 0;
	virtual const char *ClientClan(int ClientID) = 0;
	virtual int ClientCountry(int ClientID) = 0;
	virtual bool ClientIngame(int ClientID) = 0;
	virtual int GetClientInfo(int ClientID, CClientInfo *pInfo) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID, int64 Mask = -1, int WorldID = -1) = 0;

	bool Translate(int& Target, int Client)
	{
		if(Target < MAX_PLAYERS || Client >= MAX_PLAYERS)
			return true;

		int* pMap = GetIdMap(Client);
		bool Found = false;
		for(int i = MAX_PLAYERS; i < VANILLA_MAX_CLIENTS; i++)
		{
			if(Target == pMap[i])
			{
				Target = i;
				Found = true;
				break;
			}
		}
		return Found;
	}

	bool ReverseTranslate(int& Target, int Client)
	{
		if(Target < MAX_PLAYERS || Client >= MAX_PLAYERS)
			return true;

		Target = clamp(Target, 0, VANILLA_MAX_CLIENTS - 1);
		int* pMap = GetIdMap(Client);
		if(pMap[Target] == -1)
			return false;
		Target = pMap[Target];
		return true;
	}

	template<class T>
	int SendPackMsg(const T* pMsg, int Flags, int ClientID, int64 Mask = -1, int WorldID = -1)
	{
		int Result = 0;

		if(ClientID == -1)
		{
			for(int i = 0; i < MAX_PLAYERS; i++)
			{
				if(ClientIngame(i))
					Result = SendPackMsgTranslate(pMsg, Flags, i, Mask, WorldID);
			}
		}
		else
		{
			Result = SendPackMsgTranslate(pMsg, Flags, ClientID, Mask, WorldID);
		}

		return Result;
	}

	template<class T>
	int SendPackMsgOne(const T* pMsg, int Flags, int ClientID, int64 Mask, int WorldID)
	{
		dbg_assert(ClientID != -1, "SendPackMsgOne called with -1");
		CMsgPacker Packer(pMsg->MsgID(), false);
		if(pMsg->Pack(&Packer))
			return -1;

		return SendMsg(&Packer, Flags, ClientID, Mask, WorldID);
	}

	template<class T>
	int SendPackMsgTranslate(const T* pMsg, int Flags, int ClientID, int64 Mask, int WorldID)
	{
		return SendPackMsgOne(pMsg, Flags, ClientID, Mask, WorldID);
	}
	
	int SendPackMsgTranslate(const CNetMsg_Sv_Emoticon* pMsg, int Flags, int ClientID, int64 Mask, int WorldID)
	{
		CNetMsg_Sv_Emoticon MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));

		return Translate(MsgCopy.m_ClientID, ClientID) && SendPackMsgOne(&MsgCopy, Flags, ClientID, Mask, WorldID);
	}

	int SendPackMsgTranslate(const CNetMsg_Sv_Chat* pMsg, int Flags, int ClientID, int64 Mask, int WorldID)
	{
		CNetMsg_Sv_Chat MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));

		char aBuf[1000];
		if(MsgCopy.m_ClientID >= 0 && !Translate(MsgCopy.m_ClientID, ClientID))
		{
			str_format(aBuf, sizeof(aBuf), "%s: %s", ClientName(MsgCopy.m_ClientID), MsgCopy.m_pMessage);
			MsgCopy.m_pMessage = aBuf;
			MsgCopy.m_ClientID = VANILLA_MAX_CLIENTS - 1;
		}

		return SendPackMsgOne(&MsgCopy, Flags, ClientID, Mask, WorldID);
	}

	// translate KillMsg
	int SendPackMsgTranslate(const CNetMsg_Sv_KillMsg* pMsg, int Flags, int ClientID, int64 Mask, int WorldID)
	{
		CNetMsg_Sv_KillMsg MsgCopy;
		mem_copy(&MsgCopy, pMsg, sizeof(MsgCopy));
		if(!Translate(MsgCopy.m_Victim, ClientID))
			return 0;
		if(!Translate(MsgCopy.m_Killer, ClientID))
			MsgCopy.m_Killer = MsgCopy.m_Victim;

		return SendPackMsgOne(&MsgCopy, Flags, ClientID, Mask, WorldID);
	}

	virtual void SetClientName(int ClientID, char const *pName, bool IsBot = false) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;
	virtual void SetClientScore(int ClientID, int Score) = 0;

	virtual int NewBot(int ClientID) = 0;
	virtual int DelBot(int ClientID) = 0;

	virtual int SnapNewID() = 0;
	virtual void SnapFreeID(int ID) = 0;
	virtual void *SnapNewItem(int Type, int ID, int Size) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	enum
	{
		RCON_CID_SERV=-1,
		RCON_CID_VOTE=-2,
	};
	virtual void SetRconCID(int ClientID) = 0;
	virtual bool IsAuthed(int ClientID) = 0;
	virtual void Kick(int ClientID, const char *pReason) = 0;

	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual bool DemoRecorder_IsRecording() = 0;

	virtual void ExpireServerInfo() = 0;

	virtual const char* GetClientLanguage(int ClientID) = 0;
	virtual void SetClientLanguage(int ClientID, const char* pLanguage) = 0;
	virtual int* GetIdMap(int ClientID) = 0;
	virtual void SetCustClt(int ClientID) = 0;

	virtual bool IsClientChangesWorld(int ClientID) = 0;
	virtual void ChangeWorld(int ClientID, int NewWorldID) = 0;
	virtual int GetClientWorldID(int ClientID) = 0;
	virtual const char* GetWorldName(int WorldID) = 0;
	virtual int GetWorldsSize() const = 0;
};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver", 0)
protected:
public:
	virtual void OnInit(int WorldID) = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnTickMainWorld() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientID) = 0;
	virtual void OnPostSnap() = 0;

	virtual void PrepareClientChangeWorld(int ClientID) = 0;

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) = 0;
	virtual void ClearClientData(int ClientID) = 0;

	virtual void OnClientConnected(int ClientID) = 0;
	virtual void OnClientEnter(int ClientID) = 0;
	virtual void OnClientDrop(int ClientID, const char *pReason) = 0;
	virtual void OnClientDirectInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientID, void *pInput) = 0;

	virtual bool IsClientReady(int ClientID) = 0;
	virtual bool IsClientPlayer(int ClientID) = 0;

	virtual void DeleteBot(int i) = 0;
	virtual bool ReplacePlayerByBot(int ClientID) = 0;

	virtual const char *GameType() = 0;
	virtual const char *Version() = 0;
	virtual const char *NetVersion() = 0;

	virtual void OnSetAuthed(int ClientID, int Level) = 0;
	virtual bool PlayerExists(int ClientID) const = 0;
};

extern IGameServer *CreateGameServer();
#endif