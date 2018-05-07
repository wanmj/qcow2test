#include "connection.h"
#include <errno.h>

extern int errno;

Connection::Connection(const char *ip, uint16_t port):sockfd_(-1), data_(new char[MAX_RANDOM_SECTOR_RANGE * SECTOR_SIZE]) {
	memset(&servaddr, 0, sizeof(servaddr));
	memset(data_, 0, MAX_RANDOM_SECTOR_RANGE * SECTOR_SIZE);

	if((sockfd_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
//		printf("%d\n", errno);             // 调试专用
		delete[] data_;
		throw "socket error.";
	}

	servaddr.sin_family = AF_INET;
	inet_pton(AF_INET, ip, &servaddr.sin_addr.s_addr);
	servaddr.sin_port = htons(port);

	if(connect(sockfd_, (struct sockaddr *)&servaddr, sizeof(servaddr)) != 0) {
		printf("%d\n", errno);              // 调试专用
		delete[] data_;
		throw "connect error.";
	}
}

Connection::~Connection() {
	close(sockfd_);
	delete[] data_;
}

// TODO
// 需要修改，增加挂载点信息，函数要加一个const char *参数，表示挂载点
// 即在virAttachVolume函数中devname参数
// xml文档中的<target dev='vdc'>字段
// 这里的这个字段在虚拟机中并不对应，添加也没用，想别的办法
//
const char *Connection::getRemoteData(/*const char *attach_point, */int startSector, int numSector) {
	if(startSector > SECTOR_NUM || numSector > MAX_RANDOM_SECTOR_RANGE) {
		throw "too large startSector or numSector.";
	}

	char order[20] = { 0 };

	sprintf(order, "%d:%d", startSector, numSector);
//	printf("起始扇区:扇区数量：%s\n", order);

	if(send(sockfd_, order, 20, 0) < 0) {
		throw "write error.";
	}

//	sleep(1);

	if(recv(sockfd_, data_, numSector * SECTOR_SIZE, MSG_WAITALL) < 0) {
		throw "read error.";
	}

	return data_;
}
