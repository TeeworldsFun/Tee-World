#ifndef GAME_SERVER_TWGC_CONTROLLER_H
#define GAME_SERVER_TWGC_CONTROLLER_H

#include <list>

#include "TWorldComponent.h"

class TWorldController
{
    /* Kurosio */
    class CStack
    {
    public:
        void add(class TWorldComponent *pComponent)
        {
            m_paComponents.push_back(pComponent);
        }

        void free()
        {
            for (auto *pComponent : m_paComponents)
                delete pComponent;
            m_paComponents.clear();
        }

        std::list<class TWorldComponent *> m_paComponents;
    };
    CStack m_Components;
    /* Kurosio */

    class CAccount *m_pAcc;

public:
    TWorldController(CGameContext *pGameServer);
    ~TWorldController();

    CGameContext *m_pGameServer;
	CGameContext *GS() const { return m_pGameServer; }

    CAccount *Account() const { return m_pAcc; }

    // global systems
	void OnTick();
	bool OnMessage(int MsgID, void* pRawMsg, int ClientID);
	bool OnPlayerHandleTile(CCharacter *pChr, int IndexCollision);
	bool OnPlayerHandleMainMenu(int ClientID, int Menulist);
	void OnInitAccount(int ClientID);
	bool OnParsingVoteCommands(CPlayer *pPlayer, const char *CMD, int VoteID, int VoteID2, int Get, const char *GetText);
	void ResetClientData(int ClientID);
};

#endif
