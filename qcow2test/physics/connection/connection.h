#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "../macro.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>


class Connection {
public:
	Connection(const char *ip, uint16_t port);
	~Connection();

private:
	Connection(const Connection&);
	Connection& operator=(const Connection&);

public:
	// TODO
	// 怎么做看源文件
	//
	const char *getRemoteData(int startSector, int numSector);
	
private:
	int sockfd_;
	char *data_;
	struct sockaddr_in servaddr;
};

#endif
