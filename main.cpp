#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <memory.h>

#define soc_cv_av

#include "hwlib.h"

#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include "soc_cv_av/socal/socal.h"
#include "soc_cv_av/socal/hps.h"
#include "soc_cv_av/socal/alt_gpio.h"
#include "hps_0.h"
#include "led.h"
#include <stdbool.h>

#include <pthread.h>

#include <queue>

#define PAGE_FILE "/proc/self/pagemap"

#define HW_REGS_BASE	0xFF400000	// ( ALT_STM_OFST )
//#define HW_REGS_BASE	(0xC0000000)
#define HW_REGS_SPAN ( 0x04000000 )
#define HW_REGS_MASK ( HW_REGS_SPAN - 1 )

volatile unsigned long *h2p_lw_led_addr = NULL;
void *rcvr_addr = NULL;

void LEDR_LightCount(unsigned char LightCount);
void LEDR_OffCount(unsigned char OffCount);
void LEDR_AllOn(void);
void LEDR_AllOff(void);

void LEDR_LightCount(unsigned char LightCount) { // 1: light, 0:unlight
	uint32_t Mask = 0;
	int i;
	for (i = 0; i < LightCount; i++) {
		Mask <<= 1;
		Mask |= 0x01;
	}
	//IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, Mask);  //0:ligh, 1:unlight
	alt_write_word(h2p_lw_led_addr, Mask);  //0:ligh, 1:unlight
}
void LEDR_OffCount(unsigned char OffCount) { // 1: light, 0:unlight
	uint32_t Mask = 0x03ff;
	int i;
	for (i = 0; i < OffCount; i++) {
		Mask >>= 1;
	}
	//IOWR_ALTERA_AVALON_PIO_DATA(LEDG_BASE, Mask);  //0:ligh, 1:unlight
	alt_write_word(h2p_lw_led_addr, Mask);  //0:ligh, 1:unlight
}

void LEDR_AllOn(void) {
	alt_write_word(h2p_lw_led_addr, 0x3FF);  //0:ligh, 1:unlight
}
void LEDR_AllOff(void) {
	alt_write_word(h2p_lw_led_addr, 0x00);  //0:ligh, 1:unlight
}

using namespace std;

pthread_t ctrl_recv_thread;
pthread_t recv_thread;
pthread_t send_thread;

int my_drv;
int udp_sock;
sockaddr_in udp_addr;

#define	BLOCK_SIZE	0x2000

std::queue<char*> q;
pthread_mutex_t q_lock;
pthread_cond_t q_signal;

void * recv_thread_proc(void *arg) {

	printf("print from thread proc.....\n");

	int i = 0;
	timeval ts;
	gettimeofday(&ts, 0);
	double long tc = (double long) (ts.tv_sec * 1000000 + ts.tv_usec);

//	cpu_set_t cpuset;
//	CPU_ZERO(&cpuset);
//	CPU_SET(0, &cpuset);
//	pthread_t current_thread = pthread_self();
//	printf("recv set affinity=%i\n", pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset));

	while (1) {
		char *buf = new char[BLOCK_SIZE];
		int bc = read(my_drv, buf, BLOCK_SIZE);

		pthread_mutex_lock(&q_lock);
		q.push(buf);
		if (q.size() == 1)
			pthread_cond_signal(&q_signal);
		pthread_mutex_unlock(&q_lock);

		i += bc;
		gettimeofday(&ts, 0);
		double long tc_now = (double long) (ts.tv_sec * 1000000 + ts.tv_usec);
		if (tc_now - tc >= 1000000) {
			printf("recv %.3f \n", (float) i / (float) (tc_now - tc));
			tc = tc_now;
			i = 0;
		}
	}

	return NULL;
}

void * send_thread_proc(void *arg) {

	char *buf = NULL;
	int i;
	timeval ts;
	gettimeofday(&ts, 0);
	double long tc = (double long) (ts.tv_sec * 1000000 + ts.tv_usec);
	int bc, wc = 0;

//	cpu_set_t cpuset;
//	CPU_ZERO(&cpuset);
//	CPU_SET(0, &cpuset);
//	pthread_t current_thread = pthread_self();
//	printf("send set affinity=%i\n", pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset));

	while (1) {

		pthread_mutex_lock(&q_lock);
		if (q.size() == 0) {
			wc++;
			pthread_cond_wait(&q_signal, &q_lock);
		}
		buf = q.front();
		q.pop();
		pthread_mutex_unlock(&q_lock);

		bc = sendto(udp_sock, buf, BLOCK_SIZE, 0, (sockaddr*) &udp_addr, sizeof(sockaddr_in));

		delete[] buf;

		i += bc;
		gettimeofday(&ts, 0);
		double long tc_now = (double long) (ts.tv_sec * 1000000 + ts.tv_usec);
		if (tc_now - tc >= 1000000) {
			printf("waiting=%i times, send %.3f \n", wc, (float) i / (float) (tc_now - tc));
			wc = 0;
			tc = tc_now;
			i = 0;
		}
	}
	return NULL;
}

unsigned int mk_cmd(unsigned int addr, unsigned int data) {
	return ((addr << 16) & 0xFFFF0000) | (data & 0xFFFF);
}

unsigned int mk_addr(int loop_num, int param_num, int addr) {
	return 0x7000 | ((loop_num << 9) & 0x0E00) | ((param_num << 5) & 0x1E0) | (addr & 0x1F);
}

void *ctrl_start;
void *ctrl_cmd;

void *pio_sync;
void *pio_res_alt;

#define PRM_SET_PARAM	0
#define	PRM_VRC_WRITE	5
#define	PRM_FILT_KOFF	8
#define	PRM_VRC_PTR_RST	9

void SetParam(int loop_num, int ch_num, int scan_type_a, int data_on, int imp_on, int pre_amp_on) {
	unsigned int param = ch_num & 0x7;
	param |= (scan_type_a << 3) & 0x08;
	param |= (data_on << 4) & 0x10;
	param |= (imp_on << 5) & 0x20;
	param |= (pre_amp_on << 6) & 0x40;

	alt_write_word(ctrl_cmd, mk_cmd(mk_addr(loop_num, PRM_SET_PARAM, 0), param));
	alt_write_word(ctrl_start, 0x0);
	while (alt_read_word(ctrl_cmd) != 1)
		usleep(100);
}

void SetVRC(int loop_num, int amp1, int amp2, int len) {

	alt_write_word(ctrl_cmd, mk_cmd(mk_addr(loop_num, PRM_VRC_PTR_RST, 0), 0));
	alt_write_word(ctrl_start, 0x0);
	while (alt_read_word(ctrl_start) != 1)
		usleep(100);

	if (len > 0) {
		for (int i = 0; i <= len; i++) {
			int a = (amp1 * 2) * i / len;

			a = a < 256 ? a << 8 : 0xFF00 | (a - 255);
			alt_write_word(ctrl_cmd, mk_cmd(mk_addr(loop_num, PRM_VRC_WRITE, 0), a));
		}
	}

	for (int i = 1; i <= 60 - len; i++) {
		int a = amp1 * 2 + (float) (amp2 - amp1) * 2 * (float) i / (float) (60 - len);

		a = a < 256 ? a << 8 : 0xFF00 | (a - 255);
		alt_write_word(ctrl_cmd, mk_cmd(mk_addr(loop_num, PRM_VRC_WRITE, 0), a));
	}

	alt_write_word(ctrl_start, 0x0);
	while (alt_read_word(ctrl_start) != 1)
		usleep(100);
}

#define DCgain 131072
const short FIRCoef[23] = { -3067, -1093, -565, -2779, -7429, -12290, -14061, -10091, -127, 12960, 24117, 28506, 24117,
		12960, -127, -10091, -14061, -12290, -7429, -2779, -565, -1093, -3067 };

void SetFilter(int loop_num) {
	for (int i = 0; i < 23; i++) {
		alt_write_word(ctrl_cmd, mk_cmd(mk_addr(loop_num, PRM_FILT_KOFF, i), FIRCoef[i]));
		alt_write_word(ctrl_start, 0);
		while (alt_read_word(ctrl_cmd) != 1)
			usleep(100);
	}
	alt_write_word(ctrl_cmd, mk_cmd(mk_addr(loop_num, PRM_FILT_KOFF, 23), 17));
	alt_write_word(ctrl_start, 0);
	while (alt_read_word(ctrl_cmd) != 1)
		usleep(100);
}

#define	CMD_SET_VRC		0xAA550012

int ctrl_sock;
pthread_t recv_param_thread;
void * recv_param_thread_proc(void *arg) {

	ctrl_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	sockaddr_in bind_addr;

	bind_addr.sin_family = AF_INET;
	bind_addr.sin_port = htons(11744);
	bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	bind(ctrl_sock, (sockaddr*) &bind_addr, sizeof(sockaddr_in));

	sockaddr_in addr;
	socklen_t addr_len = sizeof(sockaddr_in);

	int buf[256];

	while (true) {
		int bc = recvfrom(ctrl_sock, buf, 256 * sizeof(int), 0, (sockaddr*) &addr, &addr_len);
		if (bc <= 0)
			break;

		switch (buf[0]) {
		case CMD_SET_VRC:
			SetVRC(buf[2], buf[3], buf[4], buf[5]);
			break;
		}

		//printf("Receive byte count=%i\n", bc);
	}

	return NULL;
}

int data_sock;
pthread_t send_data_thread;
void * send_data_thread_proc(void * arg) {

	for (int i = 0; i < 8; i++)
		SetParam(i, i, 0, 1, 1, 0);

	for (int i = 0; i < 8; i++) {
		SetVRC(i, 200, 250, 128);
	}

	for (int i = 0; i < 8; i++) {
		SetFilter(i);
	}

	data_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	sockaddr_in addr;

	addr.sin_family = AF_INET;
	addr.sin_port = htons(17012);
	addr.sin_addr.s_addr = inet_addr("192.168.1.71");

	char data[1024];

	while (true) {

		alt_write_word((void * )((long int ) rcvr_addr + 0x3000 * 4), 0x00000000);	// clear data counter
		alt_write_word((void * )((long int ) rcvr_addr + 0x3000 * 4), 0xFFFFFFFF);	// enable data counter

		for (int i = 0; i < 8; i++) {
			alt_write_word(pio_sync, 0);
			alt_write_word(pio_sync, 1);	// pulse sync

			while ((alt_read_word((void * )((long int ) rcvr_addr + 0x3400 * 4)) & 1) == 0)
				usleep(100);

			usleep(1000);
			//printf("%i ", alt_read_word((void * )((long int ) rcvr_addr + 0x3000 * 4)));
		}

		//printf("\n");

		memcpy(data, (void *) *&rcvr_addr, 1024);

		if (sendto(data_sock, data, 1024, 0, (sockaddr*) &addr, sizeof(sockaddr_in)) <= 0)
			break;
	}

	return NULL;
}

int main() {

	void *lw_h2f;
	void *h2f;
	int fd;

	printf("programm is started... [OK]\n");

	// map the address space for the LED registers into user space so we can interact with them.
	// we'll actually map in errorthe entire CSR span of the HPS since we want to access various registers within that span
	if ((fd = open("/dev/mem", ( O_RDWR | O_SYNC))) == -1) {
		printf("ERROR: could not open \"/dev/mem\"...\n");
		return (1);
	}

	lw_h2f = mmap( NULL, 0x20000, ( PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0xFF200000);
	if (lw_h2f == MAP_FAILED) {
		printf("ERROR: mmap() failed...\n");
		close(fd);
		return (1);
	}

	pio_sync = (void*) ((unsigned long) lw_h2f + 0x20);
	pio_res_alt = (void*) ((unsigned long) lw_h2f + 0x10);

	ctrl_start = lw_h2f;
	ctrl_cmd = (void *) ((unsigned long) lw_h2f + 0x4);

	h2f = mmap( NULL, 0x20000, ( PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0xC0000000);
	if (h2f == MAP_FAILED)
		printf("ERROR: mmap() failed...\n");

	rcvr_addr = (void *) ((unsigned long) h2f + 0x00000);

	alt_write_word(pio_res_alt, 0);
	alt_write_word(pio_res_alt, 1);	// reset DScope
	usleep(20000);
	alt_write_word(pio_res_alt, 0);
	usleep(20000);

	alt_write_word(ctrl_cmd, 0x02);	// switch to normal mode (from default(on reset) "send 0x7" test mode)
	alt_write_word(ctrl_start, 0x0);
	while (alt_read_word(ctrl_start) != 1)
		usleep(100);

	pthread_create(&recv_param_thread, NULL, &recv_param_thread_proc, NULL);

	pthread_create(&send_data_thread, NULL, &send_data_thread_proc, NULL);

	pthread_join(recv_param_thread, NULL);
	return 0;

	udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (udp_sock < 0)
		return -1;

	udp_addr.sin_family = AF_INET;
	udp_addr.sin_port = htons(17289);
	udp_addr.sin_addr.s_addr = inet_addr("192.168.1.71");

//	my_drv = open("/dev/mydrv", O_RDWR);
//	if (my_drv < 0) {
//		printf("error open /dev/mydrv");
//		return -1;
//	}

	pthread_mutex_init(&q_lock, NULL);
	pthread_cond_init(&q_signal, NULL);

	pthread_create(&send_thread, NULL, &send_thread_proc, NULL);
	pthread_create(&recv_thread, NULL, &recv_thread_proc, NULL);

	pthread_join(recv_thread, NULL);
	pthread_join(send_thread, NULL);

	write(my_drv, "123123", 6);
	write(my_drv, "678678", 6);

	printf("SEEK_SET result=%i\n", (int) lseek(my_drv, 1024, SEEK_SET));

	write(my_drv, "111", 3);

	printf("SEEK_SET result=%i\n", (int) lseek(my_drv, 0, SEEK_SET));

	close(my_drv);

	return 0;

//	while(true) {
//		alt_write_word(ctrl_cmd, 0x04);
//		alt_write_word(ctrl_start, 0x0);
//		int val1 = alt_read_word(ctrl_start);
//		usleep(500000);
//		int val2 = alt_read_word(ctrl_start);
//
//		printf("v1=%i v2=%i\n", val1, val2);
//
//		alt_write_word(ctrl_cmd, 0x0);
//		alt_write_word(ctrl_start, 0x0);
//		usleep(500000);
//	}

	alt_write_word(ctrl_cmd, mk_cmd(mk_addr(0, 0, 0), 0x30));
	alt_write_word(ctrl_start, 0x0);

	alt_write_word((void * )((long int ) rcvr_addr + 0x3000 * 4), 0x00000000);	// clrear data counter
	alt_write_word((void * )((long int ) rcvr_addr + 0x3000 * 4), 0xFFFFFFFF);	// enable data counter

	while (1) {
		alt_write_word(pio_sync, 0);
		alt_write_word(pio_sync, 1);	// pulse sync
		usleep(100);
		alt_write_word(pio_sync, 0);

		usleep(750000);

		unsigned char buf[200];
		memcpy(buf, (void *) *&rcvr_addr, 200);

		int val = *(unsigned int *) buf;
		int count = alt_read_word((long int ) rcvr_addr + 0x3000 * 4);
		printf("%i %i\n", val, count);
	}

//	while (1) {
//		alt_write_word(pio_sync, 0);
//		alt_write_word(pio_sync, 1);
//		usleep(20000);
//		alt_write_word(pio_sync, 0);
//		usleep(500000);
//		unsigned char buf[4096 * 4];
//		timeval ts;
//		gettimeofday(&ts, 0);
//		int val = 0;
//		long long int lGetTickCount = (long long int) (ts.tv_sec * 1000000 + ts.tv_usec);
//		//for (i = 0; i < 1024; i++) {
//		//val = alt_read_dword(btn_addr);
//		memcpy(buf, (void *) *&btn_addr, 8 * 1024);
//		val = *(int*) buf;
//		//}
//		gettimeofday(&ts, 0);
//		long long int _lGetTickCount = (long long int) (ts.tv_sec * 1000000 + ts.tv_usec);
//		int dsr = (int) (_lGetTickCount - lGetTickCount);
//
////		alt_write_dword(h2p_lw_led_addr, a);
////		a = ~a;
//
//		int dc = alt_read_word(btn_addr + 0x3000);
//
//		printf("LED ON %i cnt=%i\n", val, dc);
//
//		/*
//		 for(i=0;i<=8;i++){
//		 LEDR_LightCount((unsigned char)i);
//		 usleep(100*1000);
//		 }
//		 printf("LED OFF \r\n");
//		 for(i=0;i<=8;i++){
//		 LEDR_OffCount(i);
//		 usleep(100*1000);
//		 }
//		 */
//	}

	unsigned int *m; // = (unsigned int *)valloc(1024 * 1024);
	posix_memalign((void **) &m, getpagesize(), 1024 * 1024);
	cout << (int) m << endl;

	if (mlock(m, 1024 * 1024)) {
		perror("mlock");
		exit(-1);
	}

	//posix_memalign((void **) &m, getpagesize(), 1024 * 1024);

	fd = open(PAGE_FILE, O_RDWR);
	if (fd < 0) {
		printf("page file open error...\n");
		exit(-1);
	}

	unsigned long offset = (unsigned long) m / getpagesize() * 8;

	if (lseek(fd, offset, SEEK_SET) < 0) {
		printf("lseek error...\n");
		exit(-1);
	}

	uint64_t page_info;
	if (read(fd, &page_info, sizeof(page_info)) != sizeof(page_info)) {
		printf("read error...\n");
		exit(-1);
	}

	page_info &= 0x7FFFFFFFFFFFFFLU;
	page_info *= getpagesize();

	printf("read 0x%08X\n", (unsigned int) page_info);

//	alt_write_dword((int )btn_addr + 1, page_info / 8);
//	alt_write_dword((int )btn_addr + 2, 1024);
//	alt_write_dword((int )btn_addr + 3, 255);
//	alt_write_dword((int )btn_addr + 4, 1);

	for (int i = 0; i < 100; i++)
		cout << *((unsigned int*) (m + i)) << " ";

	cout << endl;

	if (munmap(lw_h2f, HW_REGS_SPAN) != 0) {
		printf("ERROR: munm ap() failed...\n");
		close(fd);
		return (1);

	}
	close(fd);

	int sock = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in addr;
	memset(&addr, 0, sizeof(sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(12432);

	int res = bind(sock, (sockaddr*) &addr, sizeof(sockaddr_in));
	cout << "Bind result is: " << res << endl;

	res = listen(sock, 10);
	cout << "Listen result is: " << res << endl;

	while (1) {
		sockaddr_in in_sock_addr;
		memset(&in_sock_addr, 0, sizeof(sockaddr_in));
		socklen_t in_sock_addr_len = sizeof(sockaddr_in);
		int in_sock = accept(sock, (sockaddr*) &in_sock_addr, &in_sock_addr_len);

		cout << "new socket handle=" << in_sock << endl;

		close(in_sock);
	}

	cout << "hi" << endl;

	return 0;
}
