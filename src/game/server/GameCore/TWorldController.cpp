/* Kurosio */
#include <game/server/gamecontext.h>

#include "TWorldController.h"
#include "Components/Account/Account.h"

TWorldController::TWorldController(CGameContext *pGameServer) : m_pGameServer(pGameServer)
{
    m_Components.add(m_pAcc = new CAccount);

    for(auto& pComponent : m_Components.m_paComponents)
	{
		pComponent->m_Job = this;
		pComponent->m_GameServer = pGameServer;
		pComponent->m_pServer = pGameServer->Server();

		pComponent->OnInit();
    }
}

TWorldController::~TWorldController()
{
	m_Components.free();
}

void TWorldController::OnTick()
{
	for(auto& pComponent : m_Components.m_paComponents)
		pComponent->OnTick();
}

bool TWorldController::OnMessage(int MsgID, void* pRawMsg, int ClientID)
{
	if(GS()->Server()->ClientIngame(ClientID) && GS()->GetPlayer(ClientID))
	{
		for(auto& pComponent : m_Components.m_paComponents)
		{
			if(pComponent->OnMessage(MsgID, pRawMsg, ClientID))
				return true;
		}
	}

	return false;
}

void TWorldController::OnInitAccount(int ClientID)
{
	CPlayer *pPlayer = GS()->m_apPlayers[ClientID];
	if(!pPlayer || !pPlayer->m_Authed)
		return;

	for(auto& pComponent : m_Components.m_paComponents)
		pComponent->OnInitAccount(pPlayer);
}

/* Kurosio */