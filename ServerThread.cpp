/*
 * ServerThread.cpp
 *
 *  Created on: Feb 10, 2016
 *      Author: zaqc
 */

#include <memory.h>
#include <pthread.h>

#include <sys/socket.h>

#include "ServerThread.h"
//----------------------------------------------------------------------------

void *listen_thread_proc(void *aParam) {
	((ServerThread*) aParam)->ListenThreadProc();
	pthread_exit(NULL);

	return NULL;
}
//----------------------------------------------------------------------------

void *message_thread_proc(void *aParam) {
	//((ServerThread*) aParam)->MessageThread();
	pthread_exit(NULL);

	return NULL;
}
//----------------------------------------------------------------------------

//============================================================================
//	ServerThread
//============================================================================
ServerThread::ServerThread() {
	pthread_create(&m_MessageThread, NULL, &message_thread_proc, this);

	pthread_create(&m_ListenThread, NULL, &listen_thread_proc, this);
}
//----------------------------------------------------------------------------

ServerThread::~ServerThread() {
	// TODO Auto-generated destructor stub
}
//----------------------------------------------------------------------------

void ServerThread::ListenThreadProc(void) {

}
//----------------------------------------------------------------------------

//============================================================================
//	ClientItem
//============================================================================
void *client_send_thread_proc(void *aParam) {
	//((ClientItem*) aParam)->SendThreadProc();
	pthread_exit(NULL);

	return NULL;
}
//----------------------------------------------------------------------------

void *client_recv_thread_proc(void *aParam) {
	((ClientItem*) aParam)->RecvThreadProc();
	pthread_exit(NULL);

	return NULL;
}
//----------------------------------------------------------------------------

ClientItem::ClientItem(int aSocket) {
	m_Socket = aSocket;

	pthread_mutex_init(&m_SendLock, NULL);
	pthread_mutex_init(&m_MessageLock, NULL);

	pthread_create(&m_RecvThread, NULL, &client_recv_thread_proc, this);

	pthread_mutex_lock(&m_MessageLock);
	m_MessageThreadRun = (0 == pthread_create(&m_MessageThread, NULL, &client_send_thread_proc, this));
	pthread_mutex_unlock(&m_MessageLock);
}
//----------------------------------------------------------------------------

ClientItem::~ClientItem() {
	pthread_join(m_MessageThread, NULL);
	pthread_join(m_RecvThread, NULL);

	pthread_mutex_destroy(&m_SendLock);
}
//----------------------------------------------------------------------------

void ClientItem::PushMessage(char *aBuf, int aSize) {
	char *tmp = new char[aSize + sizeof(int)];
	*(int *) tmp = aSize;
	if (0 != aSize)
		memcpy(tmp + sizeof(int), aBuf, aSize);

	pthread_mutex_lock(&m_MessageLock);
	m_Message.push(tmp);
	if (m_Message.size() == 1)
		pthread_cond_signal(&m_MessageWait);
	pthread_mutex_unlock(&m_MessageLock);
}
//----------------------------------------------------------------------------

void ClientItem::MessageThreadProc(void) {
	while (true) {
		pthread_mutex_lock(&m_MessageLock);
		if (m_Message.size() == 0)
			pthread_cond_wait(&m_MessageWait, &m_MessageLock);

		if (!m_MessageThreadRun) {
			pthread_mutex_unlock(&m_MessageLock);
			break;
		}
		char *msg = m_Message.front();
		m_Message.pop();
		pthread_mutex_unlock(&m_MessageLock);

		int size = *(int*) msg;
		pthread_mutex_lock(&m_SendLock);
		int hdr = HDR_MESSAGE;
		send(m_Socket, &hdr, sizeof(int), 0);
		send(m_Socket, &size, sizeof(int), 0);
		send(m_Socket, msg + sizeof(int), size, 0);
		pthread_mutex_unlock(&m_SendLock);

		delete[] msg;
	}
}
//----------------------------------------------------------------------------

void ClientItem::ReadBuf(char *aBuf, int aSize) {

	int len = 0;
	while (len < aSize) {
		int bc = recv(m_Socket, aBuf + len, aSize - len, 0);
		if (bc > 0)
			len += bc;
		else
			throw "socket reading error...";
	}
}
//----------------------------------------------------------------------------

int ClientItem::ReadInt(void) {

	int res;
	ReadBuf((char*) &res, sizeof(int));
	return res;
}
//----------------------------------------------------------------------------

void ClientItem::RecvThreadProc(void) {

	int hdr = 0;
	int size = 0;
	char *buf = NULL;
	int buf_size = 0;

	while (true) {
		try {
			hdr = ReadInt();
			size = ReadInt();
			if (size != 0) {
				if (buf_size < size) {
					if (NULL != buf) {
						delete[] buf;
						buf = NULL;
					}
					buf = new char[size];
				}
				ReadBuf(buf, size);
			}
		} catch (...) {
			break;
		}

		switch (hdr) {
		case HDR_RESPONCE:
			break;
		case HDR_MESSAGE:
			break;
		}

		if (NULL != buf)
			delete[] buf;
	}
}
//----------------------------------------------------------------------------

