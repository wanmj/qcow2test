#include "getsectordata.h"
#include "../macro.h"
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

static int getSectorSize(int fd) {
	int size = 0;
	if(ioctl(fd, BLKSSZGET, &size) < 0) return -1;
	return size;
}

int getSectorNum(int fd) {
	int size = 0;
	if(ioctl(fd, BLKGETSIZE, &size) < 0) return -1;
	return size;
}

int getSectorData(int fd, void *buffer, int index) {
	extern int errno;
	int sector_size = SECTOR_SIZE;   // getSectorSize(fd);
	if(sector_size < 0) return -1;

	lseek(fd, 0, SEEK_SET);
	if(lseek(fd, index * sector_size, SEEK_CUR) == -1) {
		printf("errno: %d\n", errno);
		printf("lseek error\n");
		return -1;
	}

	return read(fd, buffer, sector_size);
}

// 这个函数没毛病，毛病出在传输那一块，可能传输有问题，可能接收有问题
//
int getDiskSector(int fd, char *buffer, int start, int num) {
	for(int i = 0; i < num; ++i, ++start) {
		getSectorData(fd, buffer, start);
		buffer += SECTOR_SIZE;
	}
	return 0;
}
