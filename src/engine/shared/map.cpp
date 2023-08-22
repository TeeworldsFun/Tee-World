/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/map.h>
#include <engine/storage.h>
#include <game/mapitems.h>
#include "datafile.h"

class CMap : public IEngineMap
{
	int m_CurrentMapSize;
	unsigned char* m_pCurrentMapData;

	CDataFileReader m_DataFile;
public:
	CMap() : m_CurrentMapSize(0), m_pCurrentMapData(0x0) {}
	~CMap()
	{
		mem_free(m_pCurrentMapData);
		m_pCurrentMapData = 0x0;
	}

	virtual void *GetData(int Index) { return m_DataFile.GetData(Index); }
	virtual void *GetDataSwapped(int Index) { return m_DataFile.GetDataSwapped(Index); }
	virtual void UnloadData(int Index) { m_DataFile.UnloadData(Index); }
	virtual void *GetItem(int Index, int *pType, int *pID) { return m_DataFile.GetItem(Index, pType, pID); }
	virtual void GetType(int Type, int *pStart, int *pNum) { m_DataFile.GetType(Type, pStart, pNum); }
	virtual void *FindItem(int Type, int ID) { return m_DataFile.FindItem(Type, ID); }
	virtual int NumItems() { return m_DataFile.NumItems(); }

	virtual void SetCurrentMapSize(int Size) { m_CurrentMapSize = Size; }
	virtual int GetCurrentMapSize() { return m_CurrentMapSize; }

	virtual void SetCurrentMapData(unsigned char* CurrentMapData) { m_pCurrentMapData = CurrentMapData; }
	virtual unsigned char* GetCurrentMapData() { return m_pCurrentMapData; }

	virtual void Unload()
	{
		m_DataFile.Close();

		m_CurrentMapSize = 0;
		mem_free(m_pCurrentMapData);
		m_pCurrentMapData = 0x0;
	}

	virtual bool Load(const char *pMapName)
	{
		IStorage* pStorage = Kernel()->RequestInterface<IStorage>();
		if (!pStorage)
			return false;
		return m_DataFile.Open(pStorage, pMapName, IStorage::TYPE_ALL);
	}

	virtual bool IsLoaded()
	{
		return m_DataFile.IsOpen();
	}

	virtual SHA256_DIGEST Sha256()
	{
		return m_DataFile.Sha256();
	}

	virtual unsigned Crc()
	{
		return m_DataFile.Crc();
	}
};

extern IEngineMap *CreateEngineMap() { return new CMap; }
