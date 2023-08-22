/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
/* Kurosio */
#ifndef GAME_SERVER_TW_COMPONENT_H
#define GAME_SERVER_TW_COMPONENT_H

class TWorldComponent
{
protected:
	class CGameContext* m_GameServer;
	class IServer* m_pServer;
	class TWorldController* m_Job;
	friend TWorldController; // provide access for the controller

	CGameContext* GameServer() const { return m_GameServer; }
	IServer* Server() const { return m_pServer; }
	TWorldController* Job() const { return m_Job; }

public:
	virtual ~TWorldComponent() {}

private:
	virtual void OnInitWorld(const char* pWhereLocalWorld) {};
	virtual void OnInit() {};
	virtual void OnInitAccount(class CPlayer* pPlayer) {};
	virtual void OnTick() {};
	virtual void OnResetClient(int ClientID) {};
	virtual bool OnMessage(int MsgID, void* pRawMsg, int ClientID) { return false; };
};

#endif
/* Kurosio */