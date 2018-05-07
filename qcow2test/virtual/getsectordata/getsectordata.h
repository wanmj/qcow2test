#ifndef __GET_SECTOR_DATA_H__
#define __GET_SECTOR_DATA_H__

int getDiskSector(int fd, char *buffer, int start, int num);
// int getSectorSize(int fd);
int getSectorNum(int fd);
int getSectorData(int fd, void *buffer, int index);

#endif
