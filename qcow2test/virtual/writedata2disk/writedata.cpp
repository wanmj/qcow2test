#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

int main(int argc, char *argv[]){
	if(argc != 3) {
		printf("Usage: %s source target\n", argv[0]);
		return 1;
	}
	int sfd;
	if((sfd = open(argv[1], O_RDONLY)) < 0) {
		printf("open %s error\n", argv[1]);
		return 1;
	}

	char buffer[1025];
	if(read(sfd, buffer, 1024) < 0) {
		printf("read %s error\n", argv[1]);
		return 1;
	}
	close(sfd);

	int tfd;
	if((tfd = open(argv[2], O_WRONLY | O_APPEND)) < 0) {
		printf("open %s error\n", argv[2]);
		return 1;
	}
	for(int i = 0; i < 1000; ++i) {
		int start = rand() % 800;
		int num = rand() % 200;
		int seek = rand() % 1000;

		lseek(tfd, seek, SEEK_CUR);

		if(write(tfd, buffer + start, num) < 0) {
			printf("write %s error\n", argv[2]);
			return 1;
		}
	}

	close(tfd);
	return 0;
}
