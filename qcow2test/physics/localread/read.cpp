#include "read.h"
#include "qcow2Lib.h"


void logFunc(const char *fmt, va_list args) {}
void warnFunc(const char *fmt, va_list args) {}
void panicFunc(const char *fmt, va_list args) {}

LocalRead::LocalRead():data_(new char[MAX_RANDOM_SECTOR_RANGE * SECTOR_SIZE]) {
	memset(data_, 0, MAX_RANDOM_SECTOR_RANGE * SECTOR_SIZE);
}

LocalRead::~LocalRead() {
	delete[] data_;
}

const char *LocalRead::getLocalData(const char *path, int startSector, int numSector) {
	Qcow2LibHandle handle;
	Qcow2Error error;

	if((error = Qcow2Lib_Init(logFunc, warnFunc, panicFunc)) != QCOW2_OK) {
		throw "Qcow2Lib_Init error.";
	}

	if((error = Qcow2Lib_Open(path, QCOW2LIB_FLAG_OPEN_READ_ONLY, &handle)) != QCOW2_OK) {
		throw "Qcow2Lib_Open error.";
	}

	if((error = Qcow2Lib_Read(handle, (uint64_t)startSector, (uint64_t)numSector, (uint8_t *)data_)) != QCOW2_OK) {
		throw "Qcow2Lib_Read error.";
	}

	if((error = Qcow2Lib_Close(handle)) != QCOW2_OK) {
		throw "Qcow2Lib_Close error.";
	}

	Qcow2Lib_Exit();
	return data_;
}
