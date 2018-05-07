#include "macro.h"
#include "fmtprint/fmtprint.h"
#include "attach/attach.h"
#include "connection/connection.h"
#include "localread/read.h"
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <libvirt/libvirt.h>
// this file depend on shared library libvirt.so
// use g++ by -lvirt
//
using namespace std;

int main(int argc, char *argv[]){
	if(argc != 4) {
		printf("Usage: %s domain-id xml-path vdisk-path.\n", argv[0]);
		return 1;
	}
	if(virAttachVolume(atoi(argv[1]), argv[2], argv[3], "vdddd") != 0) {
		return 1;
	}

	// 挂载时间未考虑，导致开始几次循环读取不到磁盘
	sleep(1);
	for(int i = 0; i < 1000; ++i) {
		Connection *pc = NULL;
		LocalRead *pl = NULL;
		try {
			pc = new Connection("192.168.237.133", 10240);
			pl = new LocalRead();
		}
		catch(const char *e) {
			if(pc != NULL) delete pc;
			if(pl != NULL) delete pl;
			std::cout << e << std::endl;
			virDetachVolume(atoi(argv[1]), argv[2]);    // 异常发生时，卸载虚拟盘
			return 1;
		}
		// bad_alloc exception
		catch(...) {
			if(pc != NULL) delete pc;
			if(pl != NULL) delete pl;
			std::cout << "bad_alloc exception." << std::endl;
			virDetachVolume(atoi(argv[1]), argv[2]);    // 异常发生时，卸载虚拟盘
			return 1;
		}

		int start = rand() % (SECTOR_NUM - MAX_RANDOM_SECTOR_RANGE);
		int num = (rand() % MAX_RANDOM_SECTOR_RANGE) + 1;
//
//		测试专用
//		int start = 5;
//		int num = 5;

		const char *p1 = NULL, *p2 = NULL;
		try {
			printf("起始扇区:扇区数量：%d:%d\n", start, num);
			p1 = pc->getRemoteData(start, num);
			p2 = pl->getLocalData(argv[3], start, num);
		}
		catch(const char *e) {
			if(pc != NULL) delete pc;
			if(pl != NULL) delete pl;
			std::cout << e << std::endl;
			virDetachVolume(atoi(argv[1]), argv[2]);    // 异常发生时，卸载虚拟盘
			return 1;
		}

//		测试专用
//		printf("remote data:\n");
//		print((void *)p1, 512 * num);
//		printf("\nlocal read:\n");
//		print((void *)p2, 512 * num);
//		printf("\n");
//
//		getchar();

		if(memcmp(p1, p2, num * SECTOR_SIZE) == 0) {
			std::cout << "----------------------------same data--------------------------" << std::endl;
		}
		else {
			std::cout << "--------------------------NOT same data------------------------" << std::endl;
		}
		delete pc;
		delete pl;
	}

	virDetachVolume(atoi(argv[1]), argv[2]);    // 异常发生时，卸载虚拟盘

	return 0;
}
