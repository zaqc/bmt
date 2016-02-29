/*
 * MydrvThread.cpp
 *
 *  Created on: Feb 9, 2016
 *      Author: zaqc
 */

#include <stdio.h>
#include <unistd.h>

#include <pthread.h>
#include <fcntl.h>

#include "MydrvThread.h"
//============================================================================

void * thread_proc(void *aParam) {
	((MydrvThread*) aParam)->ThreadProc();
	pthread_exit(NULL);
	return NULL;
}
//----------------------------------------------------------------------------

MydrvThread::MydrvThread() {
	m_MydrvHandle = open("/dev/mydrv", O_RDWR);
	if (m_MydrvHandle < 0)
		throw "can't open Mydrv driver";

	if (0 != pthread_create(&m_Thread, NULL, &thread_proc, this)) {
		close(m_MydrvHandle);
		throw "can't create thread";
	}
}
//----------------------------------------------------------------------------

MydrvThread::~MydrvThread() {

	pthread_join(m_Thread, NULL);
}
//----------------------------------------------------------------------------

void * MydrvThread::ThreadProc() {
	char buf[4096];
	while (1) {
		if (m_MydrvHandle < 0)
			break;

		int res = read(m_MydrvHandle, buf, 4096);
		if (res < 0)
			break;

		printf("%s\n", buf);
	}
	return NULL;
}
//----------------------------------------------------------------------------
