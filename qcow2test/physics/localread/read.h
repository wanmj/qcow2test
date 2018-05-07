#ifndef __READ_H__
#define __READ_H__

#include "../macro.h"
#include <stdio.h>

class LocalRead {
public:
	LocalRead();
	~LocalRead();

	const char *getLocalData(const char *path, int startSector, int numSector);
private:
	char *data_;
};

#endif
