/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_SERVER_H
#define ENGINE_SERVER_SERVER_H

#include <base/hash.h>

#include <engine/server.h>
#include <engine/shared/uuid_manager.h>


#include <list>
#include <memory>
#include <vector>


class CSnapIDPool
{
	enum
	{
		MAX_IDS = 32*1024,
	};

	class CID
	{
	public:
		short m_Next;
		short m_State; // 0 = free, 1 = alloced, 2 = timed
		int m_Timeout;
	};

	CID m_aIDs[MAX_IDS];

	int m_FirstFree;
	int m_FirstTimed;
	int m_LastTimed;
	int m_Usage;
	int m_InUsage;

public:

	CSnapIDPool();

	void Reset();
	void RemoveFirstTimeout();
	int NewID();
	void TimeoutIDs();
	void FreeID(int ID);
};


class CServerBan : public CNetBan
{
	class CServer *m_pServer;

	template<class T> int BanExt(T *pBanPool, const typename T::CDataType *pData, int Seconds, const char *pReason);

public:
	class CServer *Server() const { return m_pServer; }

	void InitServerBan(class IConsole *pConsole, class IStorage *pStorage, class CServer* pServer);

	virtual int BanAddr(const NETADDR *pAddr, int Seconds, const char *pReason);
	virtual int BanRange(const CNetRange *pRange, int Seconds, const char *pReason);

	static void ConBanExt(class IConsole::IResult *pResult, void *pUser);
};


class CServer : public IServer
{
	class IGameServer *m_pGameServer;
	class IConsole *m_pConsole;
	class IStorage *m_pStorage;
	class IRegister *m_pRegister;
	class CMultiWorlds* m_pMultiWorlds;
public:
	class IGameServer* GameServer(int WorldID = 0) override;
	class IGameServer* GameServerPlayer(int ClientID) override;
	class IConsole *Console() { return m_pConsole; }
	class IStorage *Storage() { return m_pStorage; }
	class CMultiWorlds* MultiWorlds() const { return m_pMultiWorlds; }

	enum
	{
		AUTHED_NO=0,
		AUTHED_MOD,
		AUTHED_ADMIN,

		MAX_RCONCMD_SEND=16,
	};

	class CClient
	{
	public:

		enum
		{
			STATE_EMPTY = 0,
			STATE_AUTH,
			STATE_CONNECTING,
			STATE_READY,
			STATE_INGAME,

			SNAPRATE_INIT=0,
			SNAPRATE_FULL,
			SNAPRATE_RECOVER
		};

		class CInput
		{
		public:
			int m_aData[MAX_INPUT_SIZE];
			int m_GameTick; // the tick that was chosen for the input
		};

		// connection state info
		int m_State;
		int m_Latency;
		int m_SnapRate;

		int m_LastAckedSnapshot;
		int m_LastInputTick;
		CSnapshotStorage m_Snapshots;


		CInput m_LatestInput;
		CInput m_aInputs[200]; // TODO: handle input better
		int m_CurrentInput;

		char m_aName[MAX_NAME_LENGTH];
		char m_aClan[MAX_CLAN_LENGTH];
		int m_Country;
		int m_Score;
		int m_Authed;
		int m_AuthTries;
		int m_Version;

		bool m_IsBot;

		int m_WorldID;
		int m_OldWorldID;
		bool m_IsChangesWorld;

		int m_NextMapChunk;
		bool m_Quitting;
		const IConsole::CCommandInfo *m_pRconCmdToSend;

		void Reset();

		char m_aLanguage[16];
		bool m_CustClt;

		// DDRace
		NETADDR m_Addr;
		bool m_GotDDNetVersionPacket;
		bool m_DDNetVersionSettled;
		int m_DDNetVersion;
		char m_aDDNetVersionStr[64];
		CUuid m_ConnectionID;
	};

	CClient m_aClients[MAX_CLIENTS];
	int IdMap[MAX_CLIENTS * VANILLA_MAX_CLIENTS];

	CSnapshotDelta m_SnapshotDelta;
	CSnapshotBuilder m_SnapshotBuilder;
	CSnapIDPool m_IDPool;
	CNetServer m_NetServer;
	CEcon m_Econ;
	CServerBan m_ServerBan;

	IEngineMap *m_pMap;

	int64 m_GameStartTime;
	//int m_CurrentGameTick;
	int m_RunServer;
	int m_MapReload;
	int m_RconClientID;
	int m_RconAuthLevel;
	int m_PrintCBIndex;
	bool m_ReloadedWhenEmpty;
	bool m_HeavyReload;

	int64 m_Lastheartbeat;
	//static NETADDR4 master_server;

	bool m_ServerInfoHighLoad;
	int64 m_ServerInfoFirstRequest;
	int m_ServerInfoNumRequests;

	// map
	enum
	{
		MAP_CHUNK_SIZE=NET_MAX_PAYLOAD-NET_MAX_CHUNKHEADERSIZE-4, // msg type
	};
	int m_MapChunksPerRequest;
	int m_DataChunksPerRequest;

	CDemoRecorder m_DemoRecorder;

	CServer();
	~CServer();

	int TrySetClientName(int ClientID, const char *pName, bool IsBot = false);

	virtual void SetClientName(int ClientID, const char *pName, bool IsBot = false);
	virtual void SetClientClan(int ClientID, char const *pClan);
	virtual void SetClientCountry(int ClientID, int Country);
	virtual void SetClientScore(int ClientID, int Score);

	void Kick(int ClientID, const char *pReason);

	void DemoRecorder_HandleAutoStart();
	bool DemoRecorder_IsRecording();

	//int Tick()
	int64 TickStartTime(int Tick);
	//int TickSpeed()

	int Init();

	void SetRconCID(int ClientID);
	bool IsAuthed(int ClientID);
	int GetClientInfo(int ClientID, CClientInfo *pInfo);
	void GetClientAddr(int ClientID, char *pAddrStr, int Size);
	const char *ClientName(int ClientID);
	const char *ClientClan(int ClientID);
	int ClientCountry(int ClientID);
	bool ClientIngame(int ClientID);
	int MaxClients() const;

	int SendMsg(CMsgPacker* pMsg, int Flags, int ClientID, int64 Mask = -1, int WorldID = -1) override;

	void DoSnapshot(int WorldID);

	int NewBot(int ClientID);
	int DelBot(int ClientID);

	static int ClientRejoinCallback(int ClientID, void *pUser);
	static int NewClientCallback(int ClientID, void *pUser, bool Sixup);
	static int NewClientNoAuthCallback(int ClientID, void *pUser);
	static int DelClientCallback(int ClientID, const char *pReason, void *pUser);

	void SendRconType(int ClientID, bool UsernameReq);
	void SendCapabilities(int ClientID);
	void SendMapData(int ClientID, int Chunk);
	void SendMap(int ClientID);
	void SendConnectionReady(int ClientID);
	void SendRconLine(int ClientID, const char *pLine);
	static void SendRconLineAuthed(const char *pLine, void *pUser);

	void SendRconCmdAdd(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void SendRconCmdRem(const IConsole::CCommandInfo *pCommandInfo, int ClientID);
	void UpdateClientRconCommands();

	void ProcessClientPacket(CNetChunk *pPacket);

	class CCache
	{
	public:
		class CCacheChunk
		{
		public:
			CCacheChunk(const void *pData, int Size);
			CCacheChunk(const CCacheChunk &) = delete;

			std::vector<uint8_t> m_vData;
		};

		std::list<CCacheChunk> m_Cache;

		CCache();
		~CCache();

		void AddChunk(const void *pData, int Size);
		void Clear();
	};
	CCache m_aServerInfoCache[3 * 2];
	CCache m_aSixupServerInfoCache[2];
	bool m_ServerInfoNeedsUpdate;

	void ExpireServerInfo() override;
	void CacheServerInfo(CCache *pCache, int Type, bool SendClients);
	void SendServerInfo(const NETADDR *pAddr, int Token, int Type, bool SendClients);
	bool RateLimitServerInfoConnless();
	void SendServerInfoConnless(const NETADDR *pAddr, int Token, int Type);
	void UpdateRegisterServerInfo();
	void UpdateServerInfo(bool Resend = false);

	void PumpNetwork(bool PacketWaiting);

	char *GetMapName();
	bool LoadMap(int ID);

	int Run();

	static void ConKick(IConsole::IResult *pResult, void *pUser);
	static void ConStatus(IConsole::IResult *pResult, void *pUser);
	static void ConShutdown(IConsole::IResult *pResult, void *pUser);
	static void ConRecord(IConsole::IResult *pResult, void *pUser);
	static void ConStopRecord(IConsole::IResult *pResult, void *pUser);
	static void ConMapReload(IConsole::IResult *pResult, void *pUser);
	static void ConLogout(IConsole::IResult *pResult, void *pUser);
	static void ConchainSpecialInfoupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainMaxclientsperipUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainModCommandUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void ConchainConsoleOutputLevelUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);

	void RegisterCommands();


	virtual int SnapNewID();
	virtual void SnapFreeID(int ID);
	virtual void *SnapNewItem(int Type, int ID, int Size);
	void SnapSetStaticsize(int ItemType, int Size);

public:
	virtual const char* GetClientLanguage(int ClientID);
	virtual void SetClientLanguage(int ClientID, const char* pLanguage);
	virtual int* GetIdMap(int ClientID);
	virtual void SetCustClt(int ClientID);

	bool IsClientChangesWorld(int ClientID) override;
	void ChangeWorld(int ClientID, int NewWorldID) override;
	int GetClientWorldID(int ClientID) override;

	const char* GetWorldName(int WorldID) override;
	int GetWorldsSize() const override;

};

class _StoreMultiworldIdentifiableStaticData
{
	inline static class IServer* m_pServer {};

public:
	class IServer* Server() const { return m_pServer; }
	static void Init(IServer* pServer) { m_pServer = pServer; }
};

template < typename T >
class MultiworldIdentifiableStaticData : public _StoreMultiworldIdentifiableStaticData
{
protected:
	inline static T m_pData {};

public:
	static T& Data() { return m_pData; }
};

#endif