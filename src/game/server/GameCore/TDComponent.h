/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_YATD_COMPONENT_H
#define GAME_SERVER_YATD_COMPONENT_H

class YatdComponent
{
protected:
	class CGameContext* m_GameServer;
	class IServer* m_pServer;
	class YatdController* m_Job;
	friend YatdController; // provide access for the controller

	CGameContext* GameServer() const { return m_GameServer; }
	IServer* Server() const { return m_pServer; }
	YatdController* Job() const { return m_Job; }

public:
	virtual ~YatdComponent() {}

private:
	virtual void OnInitWorld(const char* pWhereLocalWorld) {};
	virtual void OnInit() {};
	virtual void OnInitAccount(class CPlayer* pPlayer) {};
	virtual void OnTick() {};
	virtual void OnResetClient(int ClientID) {};
	virtual bool OnMessage(int MsgID, void* pRawMsg, int ClientID) { return false; };
};

#endif