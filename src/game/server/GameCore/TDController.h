#ifndef GAME_SERVER_TDGC_CONTROLLER_H
#define GAME_SERVER_TDGC_CONTROLLER_H

#include <list>

#include "TDComponent.h"

class TDController
{
    class CStack
    {
    public:
        void add(class YatdComponent *pComponent)
        {
            m_paComponents.push_back(pComponent);
        }

        void free()
        {
            for (auto *pComponent : m_paComponents)
                delete pComponent;
            m_paComponents.clear();
        }

        std::list<class YatdComponent *> m_paComponents;
    };
    CStack m_Components;

    class CAccount *m_pAcc;

public:
    TDController(CGameContext *pGameServer);
    ~TDController();

    CGameContext *m_pGameServer;
	CGameContext *GS() const { return m_pGameServer; }

    CAccount *Account() const { return m_pAcc; }
};

#endif
