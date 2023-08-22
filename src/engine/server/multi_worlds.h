#ifndef ENGINE_SERVER_MULTIWORLDS_CONTEXT_H
#define ENGINE_SERVER_MULTIWORLDS_CONTEXT_H

#include <engine/shared/protocol.h>

class CMultiWorlds
{
	bool Add(int WorldID, class IKernel* pKernel, class IServer* pServer);
	void Clear(bool Shutdown = true);

public:
	struct CWorldGameServer
	{
		char m_aName[64];
		char m_aPath[512];
		class IGameServer* m_pGameServer;
		class IEngineMap* m_pLoadedMap;
	};

	CMultiWorlds()
	{
		m_WasInitilized = 0;
		m_NextIsReloading = false;
		mem_zero(m_Worlds, sizeof(m_Worlds));
	}
	~CMultiWorlds()
	{
		Clear(true);
	}
	
	CWorldGameServer* GetWorld(int WorldID) { return &m_Worlds[WorldID]; };
	bool IsValid(int WorldID) const { return WorldID >= 0 && WorldID < ENGINE_MAX_WORLDS && m_Worlds[WorldID].m_pGameServer; }
	int GetSizeInitilized() const { return m_WasInitilized; }
	bool LoadWorlds(class IServer* pServer, class IKernel* pKernel, class IStorage* pStorage, class IConsole* pConsole);

private:
	int m_WasInitilized;
	bool m_NextIsReloading;
	CWorldGameServer m_Worlds[ENGINE_MAX_WORLDS];
};


#endif