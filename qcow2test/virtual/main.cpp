#include "macro.h"
#include "fmtprint/fmtprint.h"
#include "connection/connection.h"
#include "getsectordata/getsectordata.h"
#include <iostream>
#include <fcntl.h>

using namespace std;

int main(int argc, char *argv[]) {
	Connection *pc = NULL;
	try {
		pc = new Connection(10240);
	}
	catch(const char *e) {
		std::cout << e << std::endl;
		return 1;
	}
	catch(...) {
		std::cout << "bad_alloc exception." << std::endl;
		return 1;
	}

	while(1) {
		try {
			pc->accept();
		}
		catch(const char *e) {
			std::cout << e << std::endl;
			return 1;
		}

		// TODO
		// 此处还需要添加读取挂载点信息
		// 暂时取消
		//
		char *p = NULL;
		int startSector, numSector;
		try {
			p = pc->recvData(startSector, numSector);
			printf("起始扇区:扇区数量：%d:%d\n", startSector, numSector);
		}
		catch(const char *e) {
			std::cout << e << std::endl;
			return 1;
		}
		
		// TODO
		// 此处需要处理本地磁盘的读取 ok
		// 保留的p为写数据的接口      ok
		// readDiskSector(p, s)       ok
		// open的传入参数需要修改     TODO
		// 由宿主机发送消息确定
		//

		int fd;
		if((fd = open("/dev/vdb", O_RDONLY)) > 0);
		else if((fd = open("/dev/vdc", O_RDONLY)) > 0);
		else if((fd = open("/dev/vdd", O_RDONLY)) > 0);
		else fd = -1;

//		getDiskSector(fd, p, startSector, numSector);
		pc->getLocalData(fd, startSector, numSector);
		
		close(fd);
		// 辅助输出内容
		//
//		print(p, SECTOR_SIZE * numSector);
//		getchar();

		try {
			pc->sendData();
		}
		catch(const char *e) {
			std::cout << e << std::endl;
			return 1;
		}
		pc->closeConnection();
	}

	return 0;
}
