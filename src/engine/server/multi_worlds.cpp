#include "multi_worlds.h"

#include <engine/console.h>
#include <engine/map.h>
#include <engine/server.h>
#include <engine/storage.h>
#include <engine/external/json-parser/json.h>

bool CMultiWorlds::Add(int WorldID, IKernel* pKernel, IServer *pServer)
{
	dbg_assert(WorldID < ENGINE_MAX_WORLDS, "exceeded pool of allocated memory for worlds");

	CWorldGameServer& pNewWorld = m_Worlds[WorldID];
	pNewWorld.m_pGameServer = CreateGameServer();

	bool RegisterFail = false;
	if(m_NextIsReloading) // reregister
	{
		RegisterFail = RegisterFail || !pKernel->ReregisterInterface(pNewWorld.m_pLoadedMap, WorldID);
		RegisterFail = RegisterFail || !pKernel->ReregisterInterface(static_cast<IMap*>(pNewWorld.m_pLoadedMap), WorldID);
		RegisterFail = RegisterFail || !pKernel->ReregisterInterface(pNewWorld.m_pGameServer, WorldID);
	}
	else // register
	{
		pNewWorld.m_pLoadedMap = CreateEngineMap();
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pNewWorld.m_pLoadedMap, WorldID);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap*>(pNewWorld.m_pLoadedMap), WorldID);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pNewWorld.m_pGameServer, WorldID);
	}

	m_WasInitilized++;
	return RegisterFail;
}

bool CMultiWorlds::LoadWorlds(IServer* pServer, IKernel* pKernel, IStorage* pStorage, IConsole* pConsole)
{
	// clear old worlds
	Clear(false);

	// read file data into buffer
	char aFileBuf[512];
	str_format(aFileBuf, sizeof(aFileBuf), "maps/worlds.json");
	const IOHANDLE File = pStorage->OpenFile(aFileBuf, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
	{
		pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "maps/worlds.json", "Probably deleted or error when the file is invalid.");
		return false;
	}
	
	const int FileSize = (int)io_length(File);
	char* pFileData = (char*)malloc(FileSize);
	io_read(File, pFileData, FileSize);
	io_close(File);

	// parse json data
	json_settings JsonSettings;
	mem_zero(&JsonSettings, sizeof(JsonSettings));
	char aError[256];
	json_value* pJsonData = json_parse_ex(&JsonSettings, pFileData, aError);
	free(pFileData);
	if(pJsonData == nullptr)
	{
		pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "worlds.json", aError);
		return false;
	}
	
	// extract data
	const json_value& rStart = (*pJsonData)["worlds"];
	if(rStart.type == json_array)
	{
		for(unsigned i = 0; i < rStart.u.array.length; ++i)
		{
			const char* pWorldName = rStart[i]["name"];
			const char* pPath = rStart[i]["path"];

			// here set worlds name
			Add(i, pKernel, pServer);
			str_copy(m_Worlds[i].m_aName, pWorldName, sizeof(m_Worlds[i].m_aName));
			str_copy(m_Worlds[i].m_aPath, pPath, sizeof(m_Worlds[i].m_aPath));
		}
	}

	// clean up
	m_NextIsReloading = true;
	json_value_free(pJsonData);
	return true;
}

void CMultiWorlds::Clear(bool Shutdown)
{
	for(int i = 0; i < m_WasInitilized; i++)
	{
		if(m_Worlds[i].m_pLoadedMap)
		{
			m_Worlds[i].m_pLoadedMap->Unload();
			if(Shutdown)
			{
				delete m_Worlds[i].m_pLoadedMap;
				m_Worlds[i].m_pLoadedMap = nullptr;
			}
		}
		
		delete m_Worlds[i].m_pGameServer;
		m_Worlds[i].m_pGameServer = nullptr;
	}
	m_WasInitilized = 0;
}
