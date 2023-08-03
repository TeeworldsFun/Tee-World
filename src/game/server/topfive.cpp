//Assa part
//#include <new>
#include <engine/shared/config.h>

//
#include "topfive.h"
//#include <iostream>
#include <fstream>
#include "gamecontext.h"
#include <string.h>

CTop::CTop(CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	/*mem_zero(m_TopFiveVars.m_aaiKills, sizeof(m_TopFiveVars.m_aaiKills));
	mem_zero(m_TopFiveVars.m_aaaName, sizeof(m_TopFiveVars.m_aaaName));
	mem_zero(m_TopFiveVars.m_aiWaves, sizeof(m_TopFiveVars.m_aiWaves));
	mem_zero(m_Current.m_aCurrentKills, sizeof(m_Current.m_aCurrentKills));
	mem_zero(m_Current.m_aCurrentName, sizeof(m_Current.m_aCurrentName));
	//m_Current.m_aCurrentWave = 0;*/
}

CTop::~CTop()
{
	m_pGameServer = 0;
}

bool CTop::Info(TopFiveVars &Team)
{
	char aBuf[64] = {0};
	str_format(aBuf, sizeof(aBuf), "top5/%s-%d.txt", g_Config.m_SvMap, g_Config.m_SvLives);//Mapname, Life config.
	FILE *pF = fopen(aBuf, "rb");

	if(!pF)
		return false;

	//mem_zero(aBuf, sizeof(aBuf));
	fread(&Team, sizeof(Team), 1, pF);
	fclose(pF);

	return true;
}

bool CTop::GetInfo()
{
	char aBuf[64] = {0};
	str_format(aBuf, sizeof(aBuf), "top5/%s-%d.txt", g_Config.m_SvMap, g_Config.m_SvLives);//Mapname, Life config.
	FILE *pF = fopen(aBuf, "rb");

	if(!pF)
	{
		//fclose(pF);
		return false;
	}

	//mem_zero(aBuf, sizeof(aBuf));
	fread(&m_TopFiveVars, sizeof(TopFiveVars), 1, pF);
	fclose(pF);

	return true;
}

/*void CTop::Test()
{

	TopFiveVars testA;


	for(int team = 0; team < 5; ++team)
	{

		 testA.m_aTeams[team].m_NumPlayers = 4;
		 for(int p = 0; p < testA.m_aTeams[team].m_NumPlayers; ++p)
		 {			
			str_format(testA.m_aTeams[team].m_aaName[p], sizeof(testA.m_aTeams[team].m_aaName[p]), "default name");
			testA.m_aTeams[team].m_aKills[p] = team;		
		 }
			
		
			testA.m_aTeams[team].m_TeamScore = team;
			testA.m_aTeams[team].m_Waves = 2*team;
	}
	
	FILE *pF = fopen("test.data", "wb");
	fwrite(&testA, sizeof(testA), 1, pF);
	fclose(pF);
	pF = fopen("test.data", "rb");
	fread(&testA, sizeof(testA), 1, pF);	
	fclose(pF);

	for(int team = 0; team < 5; ++team)
	{
		dbg_msg("ReadTeam", "Team %i: \n NumPlayers: %i \n Teamscore: %i \n Waves %i \n Players: \n",team,testA.m_aTeams[team].m_NumPlayers, testA.m_aTeams[team].m_TeamScore, testA.m_aTeams[team].m_Waves);
		 for(int p = 0; p < testA.m_aTeams[team].m_NumPlayers; ++p)
		 {		

			dbg_msg("ReadTeam", "Player: %s, Kills %i \n", testA.m_aTeams[team].m_aaName[p], testA.m_aTeams[team].m_aKills[p]);
		 }			
	}
		
	Sort(testA);

	for(int team = 0; team < 5; ++team)
	{
		dbg_msg("ReadTeam", "Team %i: \n NumPlayers: %i \n Teamscore: %i \n Waves %i \n Players: \n",team,testA.m_aTeams[team].m_NumPlayers, testA.m_aTeams[team].m_TeamScore, testA.m_aTeams[team].m_Waves);
		 for(int p = 0; p < testA.m_aTeams[team].m_NumPlayers; ++p)
		 {		

			dbg_msg("ReadTeam", "Player: %s, Kills %i \n", testA.m_aTeams[team].m_aaName[p], testA.m_aTeams[team].m_aKills[p]);
		 }			
	}

}*/

bool CTop::Write(TopFiveVars &Team)
{
	char aBuf[64] = {0};
	str_format(aBuf, sizeof(aBuf), "top5/%s-%d.txt", g_Config.m_SvMap, g_Config.m_SvLives);

	FILE *pF;
	pF = fopen(aBuf, "wb");
	if(!pF)
	{
		if(!Create(Team))
			return false;
		return true;
	}

	if(!Info(Team))
		return false;

	Sort(Team);

	fwrite(&Team, sizeof(Team), 1, pF);
	fclose(pF);
	return true;
}

bool CTop::Create(TopFiveVars &Team)
{
	char aBuf[64] = {0};

	str_format(aBuf, sizeof(aBuf), "top5/%s-%d.txt", g_Config.m_SvMap, g_Config.m_SvLives);
	FILE *pF = fopen(aBuf, "wb");
	if(pF) // File exists -> username exists
	{
		fclose(pF);
		return false;
	}
	FILE *pFi = fopen(aBuf, "wb");
	//mem_zero(aBuf, sizeof(aBuf));

	fwrite(&Team, sizeof(Team), 1, pFi);
	fclose(pFi);

	return true;
}

void CTop::Sort(TopFiveVars &Top5)//Very very complicated, but i had an idea
{
	
	bool update = true;
	for(int a = 4; a > 0 && update; --a)
	{
		update = false;	
		for(int i = 0; i < a; ++i)
		{
			if(!(Top5.m_aTeams[i] < Top5.m_aTeams[i+1]))
			{
				update = true;			
				TopFiveVars::Team temp = Top5.m_aTeams[i];
				Top5.m_aTeams[i] = Top5.m_aTeams[i+1];
				Top5.m_aTeams[i+1] = temp;
			}
		}
	}

	

	/*for(int i = 0; i < 5; i++)//5 elements
	{
		if(m_TopFiveVars.m_aaiKills[i][1] < m_Current.m_aCurrentKills[1])//found a place in the i's row in the Top5
		{
			//re-sort the rest, begin from the last
			for(int k = 5; k > i; k--)
			{
				if(k == i)//the sort from the last is now at its changin point (example: new win is at place 3, the counter counts 5 ... 4... 3, and now must get the new data at 3)
				{
					//Write current in the file
					for(int c = 0; c < 5; i++)
					{
						m_TopFiveVars.m_aaiKills[i][c] = m_Current.m_aCurrentKills[c];
						if(c < 4)
							m_TopFiveVars.m_aaaName[i][c][16] = m_Current.m_aCurrentName[c][16];
					}
					m_TopFiveVars.m_aiWaves[i] = m_Current.m_aCurrentWave;
					//
					//Reset the Current(?)
					return;
				}
				for(int x = 0; x < 5; x++)
					m_TopFiveVars.m_aaiKills[k][x] = m_TopFiveVars.m_aaiKills[k-1][x];//set all the kills

				for(int l = 0; l < 4; l++)
					m_TopFiveVars.m_aaaName[k][l][16] = m_TopFiveVars.m_aaaName[k-1][l][16];//Set all the names
				
				m_TopFiveVars.m_aiWaves[k] = m_TopFiveVars.m_aiWaves[k-1];
			}
		}
	}*/
}

void CTop::SortArray(int team)//Very very complicated, but i had an idea
{
	
	bool update = true;
	for(int a = 3; a > 0 && update; --a)
	{
		update = false;	
		for(int i = 0; i < a; ++i)
		{
			if(m_TopFiveVars.m_aTeams[team].m_aKills[i]  < m_TopFiveVars.m_aTeams[team].m_aKills[i+1])
			{
				update = true;	
				char tempchar[16];
				
				int temp = m_TopFiveVars.m_aTeams[team].m_aKills[i+1];
				str_format(tempchar, sizeof(tempchar), m_TopFiveVars.m_aTeams[team].m_aaName[i+1]); 
				
				m_TopFiveVars.m_aTeams[team].m_aKills[i+1] = m_TopFiveVars.m_aTeams[team].m_aKills[i];
				str_format(m_TopFiveVars.m_aTeams[team].m_aaName[i+1], sizeof(m_TopFiveVars.m_aTeams[team].m_aaName[i+1]), m_TopFiveVars.m_aTeams[team].m_aaName[i]); 

				m_TopFiveVars.m_aTeams[team].m_aKills[i] = temp;
				str_format(m_TopFiveVars.m_aTeams[team].m_aaName[i], sizeof(m_TopFiveVars.m_aTeams[team].m_aaName[i]), tempchar); 
			}
		}
	}
}
