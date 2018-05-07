#include "../macro.h"
#include "connection.h"
#include "../getsectordata/getsectordata.h"
#include <errno.h>
#include <stdio.h>

Connection::Connection(uint16_t port):sockfd_(-1), connfd_(-1), data_(new char[SECTOR_SIZE * MAX_RANDOM_SECTOR_RANGE]) {
	memset(data_, 0, SECTOR_SIZE * MAX_RANDOM_SECTOR_RANGE);
	memset(&localaddr, 0, sizeof(localaddr));
	memset(&clientaddr, 0, sizeof(clientaddr));

	localaddr.sin_family = AF_INET;
//	inet_pton(AF_INET, INADDR_ANY, &servaddr.sin_addr.s_addr);
	localaddr.sin_addr.s_addr = htons(INADDR_ANY);
	localaddr.sin_port = htons(port);

	if((sockfd_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		delete[] data_;
		throw "socket error.";
	}

	if(bind(sockfd_, (struct sockaddr *)&localaddr, sizeof(struct sockaddr)) != 0) {
		delete[] data_;
		throw "bind error.";
	}

	if(listen(sockfd_, 10) != 0) {
		delete[] data_;
		throw "listen error.";
	}
}

Connection::~Connection() {
	delete[] data_;
	close(sockfd_);
}

void Connection::accept() {
	int clientlength;
	extern int errno;
	if((connfd_ = ::accept(sockfd_, (struct sockaddr *)&clientaddr, (socklen_t *)&clientlength)) < 0) {
		printf("errno: %d\n", errno);
		delete[] data_;
		throw "accept error.";
	}
}


// TODO
// 宿主机客户端发送的命令应该包含一下几项
// 1、挂载的虚拟磁盘的路径，比如/dev/sdb
// 2、起始扇区
// 3、扇区数量
// 形如 "vdc:123:8"
// 意思是挂载到/dev/vdc，起始扇区123号，扇区数量8个
// 宿主机客户端也需要修改
//
char *Connection::recvData(int& start, int& num) {
	if(read(connfd_, data_, 20) < 0) {
		throw "read error.";
	}
	char *p = data_;
	start = 0, num = 0;
	while(*p != ':') {
		start = start * 10 + *p - '0';
		++p;
	}
	++p;
	while(*p != '\0') {
		num = num * 10 + *p - '0';
		++p;
	}
	return data_;
}


int Connection::sendData() {
	int tmp = 0;
	if((tmp = write(connfd_, data_, SECTOR_SIZE * MAX_RANDOM_SECTOR_RANGE)) < 0) {
		throw "write error.";
	}
	return tmp;
}

void Connection::getLocalData(int fd, int start, int num) {
	getDiskSector(fd, data_, start, num);
}

void Connection::closeConnection() {
	close(connfd_);
}
