#include <game/server/gamecontext.h>

#include "TDController.h"
#include "Components/Account/Account.h"

TDController::TDController(CGameContext *pGameServer) : m_pGameServer(pGameServer)
{
    m_Components.add(m_pAcc = new CAccount);
}