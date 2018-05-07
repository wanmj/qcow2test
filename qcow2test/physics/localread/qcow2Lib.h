#ifndef __QCOW2_LIB_H__
#define __QCOW2_LIB_H__

#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
using namespace std;

typedef uint64_t Qcow2Error;
typedef int	Qcow2SnapshotType;

#define QCOW2_SUCCEEDED(err)	(QCOW2_OK == (err))
#define QCOW2_FAILED(err)		(QCOW2_OK != (err))

enum {
	QCOW2_OK = 0,
	
	/* General errors */
	QCOW2_ERROR_FAILED = 1,
	QCOW2_ERROR_INVALID_ARG = 2,
	QCOW2_ERROR_OUT_OF_MEMORY = 3,
	QCOW2_ERROR_FILE_ACCESS_ERROR = 4,
};

struct QcowHeader {
	uint32_t magic;
	uint32_t version;

	uint64_t backing_file_offset;
	uint32_t backing_file_size;

	uint32_t cluster_bits;
	uint64_t size;		/* in bytes virtual size(not disk size) */
	uint32_t crypt_method;

	uint32_t l1_size;	/* XXX: save number of clusters instead ? 36*/
	uint64_t l1_table_offset;

	uint64_t refcount_table_offset;
	uint32_t refcount_table_clusters;

	uint32_t nb_snapshots;
	uint64_t snapshots_offset;

	/* The following fields are only valid for version >= 3 */
	uint64_t incompatible_features;
	uint64_t compatible_features;
	uint64_t autoclear_features;

	uint32_t refcount_order;
	uint32_t header_length;
};

struct QCowSnapshotHeader {
	/* header is 8 byte aligned */
	uint64_t l1_table_offset;

	uint32_t l1_size;
	uint16_t id_str_size;
	uint16_t name_size;

	uint32_t date_sec;
	uint32_t date_nsec;

	uint64_t vm_clock_nsec;

	uint32_t vm_state_size;
	uint32_t extra_data_size; /* for extension */
	/* extra data follows */
	/* id_str follows */
	/* name follows  */
};

struct ncQCOW2ChangeInfo {
	// uint64_t	startSector;		// 变化区域的起始位置(扇区)
	uint64_t	lengthSector;		// 变化区域的长度(扇区)
	string		filePath;			// 变化区域所属的后端文件绝对路径
};

struct Qcow2LibHandleStruct {
	string		path_buf;			// 磁盘文件路径
	int			filedes;			// 文件标识符
	QcowHeader*	pHeader;			// 文件头信息
	uint64_t	cluster_size;		// 簇长度
	uint64_t	cluster_offset;		// 上次分配的簇偏移量
};
typedef Qcow2LibHandleStruct *Qcow2LibHandle;

typedef uint64_t Qcow2LibSectorType;

#define QCOW2_SECTOR_SIZE	512

/*
 * Flags for open
 */
#define QCOW2LIB_FLAG_OPEN_READ_ONLY   (1 << 2) // open read-only


typedef void (Qcow2LibGenericLogFunc)(const char* fmt, va_list args);

/**
 * @param startPath [in] absolute path of the disk file to start query.
 * @param endPath [in] absolute path of the latest disk file.
 * @param changeSectorMap [out] changed sector infos.
 */
bool
QueryChangedAreasOnExtSp (string startPath,
						  string endPath,
						  map<uint64_t, uint64_t> &changeSectorMap);

/**
 * @param startSpName [in] the internal snapshot name to start query.
 * @param endSpName [in] the internal snapshot name to end query.
 * @path endSpName [in] absolute path of the latest disk file.
 * @param changeSectorMap [out] changed sector infos.
 */
bool
QueryChangedAreasOnIntSp (string startSpName,
						  string endSpName,
						  string path,
						  map<uint64_t, uint64_t> &changeSectorMap);

/**
 * Initializes Qcow2Lib.
 * @param log [in] Callback for Log entries.
 * @param warn [in] Callback for warnings.
 * @param panic [in] Callback for panic.
 * @return QCOW2_OK on success, suitable Qcow2 error code otherwise.
 */
Qcow2Error
Qcow2Lib_Init (Qcow2LibGenericLogFunc *log,
			   Qcow2LibGenericLogFunc *warn,
			   Qcow2LibGenericLogFunc *panic);

/**
 * Cleans up Qcow2Lib.
 */
void
Qcow2Lib_Exit (void);

/**
 * Opens a local virtual disk.
 * @param path [in] absolute path of the latest disk file that need to open.
 * @param flags [in, optional] Bitwise or'ed  combination of
 * 			  QCOW2LIB_FLAG_OPEN_READ_ONLY.
 * @param diskHandle [out] Handle to opened disk, NULL if disk was not opened.
 * @return QCOW2_OK if success, suitable Qcow2 error code otherwise.
 */
Qcow2Error
Qcow2Lib_Open (const char *path,
			   uint32_t flags,
			   Qcow2LibHandle *diskHandle);

/**
 * Closes the disk.
 * @param diskHandle [in] Handle to an open virtual disk.
 * @return WSR_OK if success, suitable Qcow2 error code otherwise.
 */
Qcow2Error
Qcow2Lib_Close (Qcow2LibHandle diskHandle);

/**
 * Clean data of disk that has no snapshot.
 * @param path [in] absolute path of the latest disk file that need to clean.
 */
Qcow2Error
Qcow2Lib_Clean (const char *path);

/**
 * Reads a sector range.
 * @param diskHandle [in] Handle to an open virtual disk.
 * @param startSector [in] Absolute offset.
 * @param numSectors [in] Number of sectors to read.
 * @param readBuffer [out] Buffer to read into.
 * @return WSR_OK if success, suitable Qcow2 error code otherwise.
 */
Qcow2Error
Qcow2Lib_Read (Qcow2LibHandle diskHandle,
			   Qcow2LibSectorType startSector,
			   Qcow2LibSectorType numSectors,
			   uint8_t *readBuffer);

/**
 * Writes a sector range.
 * @param diskHandle [in] Handle to an open virtual disk.
 * @param startSector [in] Absolute offset.
 * @param numSectors [in] Number of sectors to write.
 * @param writeBuffer [in] Buffer to write.
 * @return WSR_OK if success, suitable Qcow2 error code otherwise.
 */
Qcow2Error
Qcow2Lib_Write (Qcow2LibHandle diskHandle,
				Qcow2LibSectorType startSector,
				Qcow2LibSectorType numSectors,
				const uint8_t *writeBuffer);

#endif // __QCOW2_LIB_H__