#ifndef GAME_SERVER_TOPFIVE_H
#define GAME_SERVER_TOPFIVE_H

#include <base/math.h>
#include <engine/shared/config.h>
#include <base/system.h>

class CTop
{
private:
	class CGameContext *m_pGameServer;

public:

	CTop( CGameContext *pGameServer);
	~CTop();
	CGameContext *GameServer() const { return m_pGameServer; }

	struct TopFiveVars
	{
		struct Team
		{
			int m_NumPlayers;
			char m_aaName[4][16];
			int m_aKills[4];
			int m_TeamScore;
			int m_Waves;
			bool operator<(Team const& other)
			{
				return this->m_TeamScore > other.m_TeamScore;
			}
		} m_aTeams[5];	
	} m_TopFiveVars;//fwrite(m_Struct, sizeof(m_Struct), 1, pFile);

	bool Info(TopFiveVars &Team);
	//bool GetInfo(int K[5][5], int W[5], char N[5][4][16]);
	bool Write(TopFiveVars &Team);
	bool Create(TopFiveVars &Team);
	void Sort(TopFiveVars &Top5);
	void SortArray(int team);

	bool GetInfo();
	//void Test();
};
#endif
