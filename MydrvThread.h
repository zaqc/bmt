/*
 * MydrvThread.h
 *
 *  Created on: Feb 9, 2016
 *      Author: zaqc
 */

#ifndef MYDRVTHREAD_H_
#define MYDRVTHREAD_H_
//----------------------------------------------------------------------------

#define	MYDRV_DATA_BASE		0x00000000
#define MYDRV_CONTROL_BASE	0x00001000
//----------------------------------------------------------------------------

class MydrvThread {
protected:
	pthread_t m_Thread;
	int m_MydrvHandle;

public:
	MydrvThread();
	virtual ~MydrvThread();

	void *ThreadProc(void);
};
//----------------------------------------------------------------------------

#endif /* MYDRVTHREAD_H_ */
