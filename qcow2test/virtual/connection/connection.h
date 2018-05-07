#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


class Connection {
public:
	Connection(uint16_t port = 11111);
	~Connection();

private:
	Connection(const Connection&);
	Connection& operator=(const Connection&);

public:
	void accept();
	char *recvData(int& start, int& num);
	int sendData();
	void getLocalData(int fd, int start, int num);
	void closeConnection();

private:
	int sockfd_;
	int connfd_;
	char *data_;
	struct sockaddr_in localaddr;
	struct sockaddr_in clientaddr;
};

#endif
