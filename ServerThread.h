/*
 * ServerThread.h
 *
 *  Created on: Feb 10, 2016
 *      Author: zaqc
 */

#ifndef SERVERTHREAD_H_
#define SERVERTHREAD_H_
//----------------------------------------------------------------------------

#include <queue>
//----------------------------------------------------------------------------

#define	HDR_REQUEST		(0x15 << 28)
#define	HDR_RESPONCE	(0x3C << 28)
#define	HDR_MESSAGE		(0x5B << 28)
#define	HDR_BROADCAST	(0x7E << 28)

#define	CMD_SET_VRC		0x0001

#define	MSG_SYSTEM_DOWN	0xF00F

#define MSG_SYNC_SOURCE	0x01
#define	MSG_VRC_CHANGED	0x02
//----------------------------------------------------------------------------

class Channel {
public:
	int m_PulseCount;
	int m_PulseRise;
	int m_PulseFreq;
	int m_GndDelay;	// in nSec Circuit to ground
	int m_Alpha;	// main angle
	int m_Beta;		// rotate angle
	int m_PhyDelay;	// in nSec delay in body
	int m_Delay;	// in nSec delay after pulse
	int m_AmpOne;
	int m_AmpTwo;
	int m_VrcLen;
	int m_TickTime;	// in nSec
	int m_TickCount;
};
//----------------------------------------------------------------------------

class EchoComplex {
public:
	int m_PortMask;
	int *m_ChannelMask;
};
//----------------------------------------------------------------------------

class ClientItem;
class Message;
//----------------------------------------------------------------------------

class ServerThread {
protected:
	int m_ServerSocket;
	std::queue<ClientItem*> m_ClientItem;

	pthread_mutex_t m_ClientLock;
	pthread_mutex_t m_MessageLock;

	pthread_cond_t m_Wait;

	pthread_t m_ListenThread;
	pthread_t m_MessageThread;

public:
	ServerThread();
	virtual ~ServerThread();

	void ListenThreadProc(void);

	void UnsubscribeClient(ClientItem *aItem);

	void cmdSetSync(int aSource);	// 0 - off 1 - RS232 2..5000 - internal sync in Hz
	void cmdSetVRC(int aPort, int aCh, int aAmpOne, int aAmpTwo, int aVrcLen);
};
//----------------------------------------------------------------------------

class Message {
private:
	char *m_Buf;
	int m_BufSize;
	int m_Length;

public:
	Message();
	virtual ~Message();

	void WriteInt(int aVal);
	void WriteBuf(char *aBuf, int aSize);
};
//----------------------------------------------------------------------------

class ClientItem {
protected:
	int m_Socket;

	std::queue<char*> m_Message;
	pthread_mutex_t m_MessageLock;
	pthread_cond_t m_MessageWait;

	pthread_t m_RecvThread;
	pthread_t m_MessageThread;

	bool m_RecvThreadRun;
	bool m_MessageThreadRun;

	char *m_SendBuf;
	int m_SendBufLen;
	pthread_mutex_t m_SendLock;
	pthread_cond_t m_SendFlag;

public:
	ClientItem(int aSocket);
	virtual ~ClientItem();

	void PushMessage(char *aBuf, int aSize);
	void MessageThreadProc(void);

	void ReadBuf(char *aBuf, int aSize);
	int ReadInt(void);

	void RecvThreadProc(void);

	void cmdGetSync(void);
};
//----------------------------------------------------------------------------

#endif /* SERVERTHREAD_H_ */
