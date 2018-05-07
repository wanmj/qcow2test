#include <stdio.h>		// for fprintf, stderr
#include <stdlib.h>		// for free, malloc, realloc, system, getenv
#include <sys/wait.h>	// for WIFEXITED WEXITSTATUS for old glibc
#include <libgen.h>		// for dirname
#include <string.h>		// for strlen, strcpy, strcat, strtok_r, strdup
#include <unistd.h>		// for stat, usleep, pread
#include <errno.h>		// for errno
#include <stdbool.h>	// for bool
#include <sys/types.h>	// for stat, open
#include <sys/stat.h>	// for stat, open
#include <assert.h>		// for assert
#include <fcntl.h>		// for open
#include <sys/file.h>	// for flock
#include <inttypes.h>	// for PRIu64 e.t.c
#include "qcow2Lib.h"	// header file
#include <endian.h>
#include <bits/byteswap.h>
#include <linux/limits.h>

// 外部快照、内部快照或默认
enum {
	QCOW2_EXTERNAL_SNAPSHOT = 0,
	QCOW2_INTERNAL_SNAPSHOT = 1,
	QCOW2_DEFAULT = 2,
};

struct GenericLogFunc {
	Qcow2LibGenericLogFunc *log;
	Qcow2LibGenericLogFunc *warn;
	Qcow2LibGenericLogFunc *panic;
	Qcow2SnapshotType snapshotType;
	string path;
} g_logFunc;

static void qcow2_info (const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logFunc.log(fmt, ap);
	va_end(ap);
}

static void qcow2_warn (const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logFunc.warn(fmt, ap);
	va_end(ap);
}

static void qcow2_panic (const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	g_logFunc.panic(fmt, ap);
	// may not return
	va_end(ap);
}

/**************************************************************************/
static void
bswap_16 (uint16_t *bsx)
{
	*bsx = (uint16_t)__bswap_16 ((unsigned short int)*bsx);
}

static void
bswap_32 (uint32_t *bsx)
{
	*bsx = (uint32_t)__bswap_32 ((unsigned int)*bsx);
}

static void
bswap_64 (__uint64_t *bsx)
{
	*bsx = (uint64_t)__bswap_64 (*bsx);
	// l1_table 和 l2_table的高两位被保留用于置copied和compressed，因此需过滤掉高两位
	uint64_t num = 3;
	*bsx &= ~(num << (8 * sizeof(uint64_t) - 2));
}

static void
swapb_16 (uint16_t *bsx)
{
	*bsx = (uint16_t)__bswap_16 ((unsigned short int)*bsx);
}

static void
swapb_32 (uint32_t *bsx)
{
	*bsx = (uint32_t)__bswap_32 ((unsigned int)*bsx);
}

static void
swapb_64 (__uint64_t *bsx)
{
	uint64_t num = 1;
	*bsx |= num << (8 * sizeof(uint64_t) - 1);
	*bsx = (uint64_t)__bswap_64 (*bsx);
}

static void
swap0b_64 (__uint64_t *bsx)
{
	*bsx = (uint64_t)__bswap_64 (*bsx);
}

static void
zero_16 (uint16_t *bsx)
{
}

static void
zero_32 (uint32_t *bsx)
{
}

static void
zero_64 (__uint64_t *bsx)
{
	// l1_table 和 l2_table的高两位被保留用于置copied和compressed，因此需过滤掉高两位
	uint64_t num = 3;
	*bsx &= ~(num << (8 * sizeof(uint64_t) - 2));
}

static void
one_16 (uint16_t *bsx)
{
}

static void
one_32 (uint32_t *bsx)
{
}

static void
one_64 (__uint64_t *bsx)
{
	uint64_t num = 1;
	*bsx |= num << (8 * sizeof(uint64_t) - 1);
}

static void
notone_64 (__uint64_t *bsx)
{
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define __beNN_to_cpus(bits, ptr) bswap_ ## bits(ptr)
#define __cpus_to_beNN(bits, ptr) swapb_ ## bits(ptr)
#define __cpus_to_0beNN(bits, ptr) swap0b_ ## bits(ptr)
#endif
 
#if __BYTE_ORDER == __BIG_ENDIAN
#define __beNN_to_cpus(bits, ptr) zero_ ## bits(ptr)
#define __cpus_to_beNN(bits, ptr) one_ ## bits(ptr)
#define __cpus_to_0beNN(bits, ptr) notone_ ## bits(ptr)
#endif

#define be16_to_cpus(ptr) __beNN_to_cpus (16, ptr)
#define be32_to_cpus(ptr) __beNN_to_cpus (32, ptr)
#define be64_to_cpus(ptr) __beNN_to_cpus (64, ptr)
#define cpus_to_be16(ptr) __cpus_to_beNN (16, ptr)
#define cpus_to_be32(ptr) __cpus_to_beNN (32, ptr)
#define cpus_to_be64(ptr) __cpus_to_beNN (64, ptr)
#define cpus_to_0be64(ptr) __cpus_to_0beNN (64, ptr)

#define QCOW_MAGIC (('Q' << 24) | ('F' << 16) | ('I' << 8) | 0xfb)

struct ncQCOW2Info {
	string		filePath;			// 后端文件绝对路径
	int			filedes;			// 文件唯一标识符
	uint32_t	cluster_bits;
	uint64_t	realSize;			// 文件真实大小
	uint64_t	size;
	uint32_t	l1_size;			// L1表大小(即L2表数量)
	uint64_t	l1_table_offset;	// L1表偏移量
};
vector<ncQCOW2Info> g_backingFileInfoSet;
map<string, vector<ncQCOW2Info> > g_backingFileInfoSetMap;
map<uint64_t, uint64_t> g_changeSectorMap;
string g_snapshotName;

// 打开文件
static int OpenFile (const char *pathname, int flags)
{
	int filedes = open (pathname, flags);
	if (filedes <= 0) {
		string error = strerror(errno);
		qcow2_warn ("%s\n", error.c_str());
	}
	qcow2_info ("OpenFile(path = %s) fd =%d.\n", pathname, filedes);
	return filedes;
}

// 关闭文件
static void CloseFile (int& filedes)
{
	qcow2_info ("CloseFile(fd = %d).\n", filedes);
	if (filedes > 0) {
		close (filedes);
		filedes = 0;
	}
}

// 获取文件的真实大小
static uint64_t GetFileSize (const char *fileName)
{
	qcow2_info ("GetFileSize start.\n");
	uint64_t size;
	struct stat st;
	stat (fileName, &st);
	size = st.st_size;
	qcow2_info ("file length in bytes: %lu.\n", size);
	return size;
}

// 获取某个Qcow2磁盘的头信息
static bool GetQCOW2Header (const char *path,
							QcowHeader &header,
							size_t numberL1TableInfo = 0)
{
	qcow2_info ("GetQCOW2Header start [%d].\n", numberL1TableInfo);

	/*
	** numberL1TableInfo 
	** 0 : 返回的l1_table信息为磁盘真实使用的
	** 1 : 返回的l1_table信息为上个快照使用的
	** 2 ：返回的l1_table信息为上上个快照使用的
	** 同上
	*/

	/* read the Header */
	int filedes = OpenFile (path, O_RDONLY|O_LARGEFILE);
	if (filedes <= 0)
		return false;

	int ret = pread (filedes, &header, sizeof(header), 0);
	if (ret == -1) {
		qcow2_warn ("Could not read qcow2 header.\n");
		CloseFile (filedes);
		return false;
	}

	be32_to_cpus (&header.magic);
	be32_to_cpus (&header.version);
	be64_to_cpus (&header.backing_file_offset);
	be32_to_cpus (&header.backing_file_size);
	be32_to_cpus (&header.cluster_bits);
	be64_to_cpus (&header.size);
	be32_to_cpus (&header.crypt_method);
	be32_to_cpus (&header.l1_size);
	be64_to_cpus (&header.l1_table_offset);
	be64_to_cpus (&header.refcount_table_offset);
	be32_to_cpus (&header.refcount_table_clusters);
	be32_to_cpus (&header.nb_snapshots);
	be64_to_cpus (&header.snapshots_offset);

	be64_to_cpus (&header.incompatible_features);
	be64_to_cpus (&header.compatible_features);
	be64_to_cpus (&header.autoclear_features);
	be32_to_cpus (&header.refcount_order);
	be32_to_cpus (&header.header_length);

	if (header.magic != QCOW_MAGIC) {
		qcow2_warn ("Image is not in qcow2 format.\n");
		CloseFile (filedes);
		return false;
	}
	qcow2_info ("%s qcow2 version : %d\n", path, header.version);
	if (header.version == 2) {
		header.refcount_order = 4;
	}
	else if (header.version != 3){
		qcow2_warn ("QCOW version is not support.\n");
		CloseFile (filedes);
		return false;
	}
	
	if (numberL1TableInfo > header.nb_snapshots){
		qcow2_warn ("The number of internal snapshot is less than %d.\n", numberL1TableInfo);
		CloseFile (filedes);
		return false;	
	}
	if (numberL1TableInfo) {
		QCowSnapshotHeader snapshotHeader;
		uint64_t snapshots_offset = header.snapshots_offset;
		size_t index = header.nb_snapshots - numberL1TableInfo;
		for (size_t i = 0; i <= index; ++i) {
			ret = pread (filedes, &snapshotHeader, sizeof(snapshotHeader), snapshots_offset);
			if (ret == -1) {
				qcow2_warn ("Could not read qcow2 snapshot header.\n");
				CloseFile (filedes);
				return false;
			}

			be64_to_cpus (&snapshotHeader.l1_table_offset);
			be32_to_cpus (&snapshotHeader.l1_size);
			be16_to_cpus (&snapshotHeader.id_str_size);
			be16_to_cpus (&snapshotHeader.name_size);
			be32_to_cpus (&snapshotHeader.date_sec);
			be32_to_cpus (&snapshotHeader.date_nsec);
			be64_to_cpus (&snapshotHeader.vm_clock_nsec);
			be32_to_cpus (&snapshotHeader.vm_state_size);
			be32_to_cpus (&snapshotHeader.extra_data_size);

			char snapshot_name[snapshotHeader.name_size + 1];
			int ret = pread (filedes, snapshot_name, snapshotHeader.name_size, snapshots_offset + snapshotHeader.extra_data_size + sizeof(snapshotHeader) + snapshotHeader.id_str_size);
			if (ret == -1) {
				qcow2_warn ("Could not read snapshot name.\n");
				return false;
			}
			snapshot_name[snapshotHeader.name_size] = '\0';
			string str(snapshot_name);
			g_snapshotName = str;
			qcow2_info ("snapshot name : %s.\n", g_snapshotName.c_str());

			uint32_t size = ((snapshotHeader.id_str_size + snapshotHeader.name_size) / 8 + bool((snapshotHeader.id_str_size + snapshotHeader.name_size)%8)) * 8;
			snapshots_offset += sizeof(snapshotHeader) + size + snapshotHeader.extra_data_size;
		}
		header.l1_size = snapshotHeader.l1_size;
		header.l1_table_offset = snapshotHeader.l1_table_offset;
	}
	
	CloseFile (filedes);

	qcow2_info ("GetQCOW2Header end.\n");
	return true;
}

// 为获取变化块做初始化工作 (获取需要备份的磁盘信息 最原始->最新)
static bool InitForQueryChangedDiskAreas (string startPath,
										  string endPath,
										  string startSpName,
										  string endSpName,
										  bool isNeedRelease)
{
	qcow2_info ("InitForQueryChangedDiskAreas start.\n");
	qcow2_info ("startPath : %s, endPath : %s, startSpName : %s, endSpName : %s.\n", startPath.c_str(), endPath.c_str(), startSpName.c_str(), endSpName.c_str());
	
	if (isNeedRelease) {
		for (vector<ncQCOW2Info>::iterator iter = g_backingFileInfoSet.begin(); iter != g_backingFileInfoSet.end(); ++iter) {
			CloseFile (iter->filedes);
		}
	}
	vector<ncQCOW2Info>().swap(g_backingFileInfoSet);
	g_backingFileInfoSet.clear();

	bool isExternalSnapshot = g_logFunc.snapshotType == QCOW2_EXTERNAL_SNAPSHOT;

	QcowHeader header;
	string filePath = endPath;
	bool isFindStartPath = false;
	do {
		bool result = GetQCOW2Header (filePath.c_str(), header);
		if (! result) {
			qcow2_warn ("Could not get qcow2 header.\n");
			return false;
		}

		/* read the backing file name */
		int filedes = OpenFile (filePath.c_str(), O_RDONLY|O_LARGEFILE);
		if (filedes <= 0)
			return false;

		char backing_file[PATH_MAX] = {0};
		qcow2_info ("backing_file_offset : %lu.\n", header.backing_file_offset);
		if (header.backing_file_offset) {
			uint32_t len = header.backing_file_size;
			int ret = pread (filedes, backing_file, len, header.backing_file_offset);
			if (ret == -1) {
				qcow2_warn ("Could not read backing file.\n");
				return false;
			}
			backing_file[len] = '\0';
		}

		if (endPath.compare(filePath)) {
			if (0 == startPath.compare("*") || 0 == startSpName.compare("*")) {
				// 全量
				ncQCOW2Info info;
				info.filePath = filePath;
				info.filedes = filedes;
				info.cluster_bits = header.cluster_bits;
				info.size = header.size;
				info.realSize = GetFileSize (info.filePath.c_str());
				info.l1_size = header.l1_size;
				info.l1_table_offset = header.l1_table_offset;
				g_backingFileInfoSet.push_back(info);
			}
			else {
				// 增量
				if (0 == g_backingFileInfoSet.size() && 0 == header.backing_file_offset) {
					CloseFile (filedes);
					qcow2_warn ("The number of backing files is less than 2.\n");
					return false;
				}
				ncQCOW2Info info;
				info.filePath = filePath;
				info.filedes = filedes;
				info.cluster_bits = header.cluster_bits;
				info.size = header.size;
				info.realSize = GetFileSize (info.filePath.c_str());
				info.l1_size = header.l1_size;
				info.l1_table_offset = header.l1_table_offset;
				g_backingFileInfoSet.push_back(info);

				if (0 == filePath.compare(startPath)) {
					isFindStartPath = true;
					break;
				}
			}
		}
		else {
			if (isExternalSnapshot) {
				CloseFile (filedes);
			}
			else {
				// 内部快照或默认时
				QcowHeader header2;
				result = GetQCOW2Header (filePath.c_str(), header2, 0);
				if (! result) {
					qcow2_warn ("Could not get the header info.\n");
					return false;
				}

				if (g_logFunc.snapshotType == QCOW2_INTERNAL_SNAPSHOT) {
					bool isFind = false;
					for (size_t i = 1; i <= header2.nb_snapshots; ++i) {
						result = GetQCOW2Header (filePath.c_str(), header2, i);
						if (! result) {
							qcow2_warn ("Could not get the info of internal snapshot.\n");
							return false;
						}
						if (0 == g_snapshotName.compare(endSpName)) {
							isFind = true;
							break;
						}
					}
					if (! isFind) {
						qcow2_warn ("Could not find the snapshot : %s.\n", endSpName.c_str());
						return false;
					}
				}
	
				ncQCOW2Info info;
				info.filePath = filePath;
				info.filedes = filedes;
				info.cluster_bits = header.cluster_bits;
				info.size = header.size;
				info.realSize = GetFileSize (info.filePath.c_str());
				info.l1_size = header2.l1_size;
				info.l1_table_offset = header2.l1_table_offset;
				g_backingFileInfoSet.push_back(info);
				// 基于内部快照的增备，则不需要再循环遍历（因增量数据存储在自身磁盘中）
				if (0 != startSpName.compare("*") && !startSpName.empty())
					break;
			}
		}
		string str(backing_file);
		qcow2_info ("%s -> %s.\n", filePath.c_str(), str.c_str());
		filePath = str;
	} while (header.backing_file_offset);

	if (!isFindStartPath && !startPath.empty() && 0 != startPath.compare("*")) {
		qcow2_warn ("Could not find the path : %s.\n", startPath.c_str());
		return false;
	}
	
	qcow2_info ("InitForQueryChangedDiskAreas end.\n");
	return true;
}

static uint64_t GetQCOW2ClusterOffset (ncQCOW2Info info, uint64_t offset)
{
	uint64_t cluster_offset = 0;
	uint32_t l2_bits = info.cluster_bits - 3;
	uint32_t l1_bits = l2_bits + info.cluster_bits;
	uint64_t l1_index = offset >> l1_bits;
	if (l1_index >= info.l1_size) {
		return cluster_offset;
	}
	uint64_t l2_offset;
	int rc = pread(info.filedes, &l2_offset, sizeof(uint64_t), info.l1_table_offset + l1_index * sizeof(uint64_t));
	if (rc == -1) {
		qcow2_warn ("Could not read l2 offset.\n");
		return cluster_offset;
	}
	be64_to_cpus (&l2_offset);
	if (0 == l2_offset) {
		return cluster_offset;
	}

	uint32_t l2_size = 1 << l2_bits;
	uint64_t l2_index = (offset >> info.cluster_bits) & (l2_size - 1);
	uint64_t clusterOffset;
	rc = pread(info.filedes, &clusterOffset, sizeof(uint64_t), l2_offset + l2_index * sizeof(uint64_t));
	if (rc == -1) {
		qcow2_warn ("Could not read l2 offset.\n");
		return cluster_offset;
	}
	be64_to_cpus (&clusterOffset);

	if (clusterOffset >= info.realSize) {
		clusterOffset = 0;
	}

	return clusterOffset;
}

// 查询某个磁盘的变化块
static bool QueryChangedAreas (const ncQCOW2Info& qcow2Info,
							   string startSpName,
							   bool	isFullBackup,
							   map<uint64_t, ncQCOW2ChangeInfo>& changeInfoMap)
{
	qcow2_info ("QueryChangedAreas start.\n");
	qcow2_info ("filePath : %s, startSpName : %s, isFullBackup : %d.\n", qcow2Info.filePath.c_str(), startSpName.c_str(), isFullBackup);

	// l1或l2表未分配，则不需再遍历（说明此磁盘中无数据）
	if (0 == qcow2Info.l1_table_offset)
		return true;

	int filedes = qcow2Info.filedes;
	if (filedes <= 0) {
		string error = strerror(errno);
		qcow2_warn ("%s\n", error.c_str());
		return false;
	}
	uint64_t* l1_table;
	l1_table = new uint64_t[qcow2Info.l1_size];
	memset (l1_table, 0, qcow2Info.l1_size * sizeof(uint64_t));
	qcow2_info ("l1_table_offset : %lu.\n", qcow2Info.l1_table_offset);
	int ret = pread (filedes, l1_table, qcow2Info.l1_size * sizeof(uint64_t), qcow2Info.l1_table_offset);
	if (ret == -1) {
		qcow2_warn ("Could not read L1 table.\n");
		delete[] l1_table;
		l1_table = NULL;
		return false;
	}
	bool isL2TableEmpty = true;
	for (size_t i = 0; i < (size_t)qcow2Info.l1_size; ++i) {
		be64_to_cpus (&l1_table[i]);
		if (l1_table[i]) {
			isL2TableEmpty = false;
			break;
		}
	}
	delete[] l1_table;
	l1_table = NULL;

	if (isL2TableEmpty)
		return true;

	// L1且L2表不为空，则遍历出此磁盘中的变化块
	bool isIncreBackupOnInternalSnapshot = (g_logFunc.snapshotType == QCOW2_INTERNAL_SNAPSHOT) && (! isFullBackup);
	QcowHeader header2;
	if (isIncreBackupOnInternalSnapshot) {
		bool isFind = false;
		bool result = GetQCOW2Header (qcow2Info.filePath.c_str(), header2, 1);
		if (! result) {
			qcow2_warn ("Could not get the info of internal snapshot.\n");
			return false;
		}
		for (size_t i = 2; i <= header2.nb_snapshots; ++i) {
			result = GetQCOW2Header (qcow2Info.filePath.c_str(), header2, i);
			if (! result) {
				qcow2_warn ("Could not get the info of internal snapshot.\n");
				return false;
			}
			if (0 == g_snapshotName.compare(startSpName)) {
				isFind = true;
				break;
			}
		}
		if (! isFind) {
			qcow2_warn ("Could not find the snapshot : %s.\n", startSpName.c_str());
			return false;
		}
	}
	qcow2_info ("l1_table_offset[-2] : %lu.\n", header2.l1_table_offset);

	uint64_t sectorNum = qcow2Info.size / QCOW2_SECTOR_SIZE;
	uint64_t startSector = 0;
	while (sectorNum) {
		uint64_t cluster_Sectors = (1 << qcow2Info.cluster_bits) / QCOW2_SECTOR_SIZE;

		uint64_t offset = startSector * QCOW2_SECTOR_SIZE;
		ncQCOW2Info info;
		info.cluster_bits = qcow2Info.cluster_bits;
		info.filePath = qcow2Info.filePath;
		info.size = qcow2Info.size;
		info.realSize = qcow2Info.realSize;
		info.filedes = filedes;
		info.l1_size = qcow2Info.l1_size;
		info.l1_table_offset = qcow2Info.l1_table_offset;
		uint64_t cluster_offset = GetQCOW2ClusterOffset (info, offset);
		// 基于内部快照的增备时，若此簇在两个快照中不相等，则说明是变化块
		if (isIncreBackupOnInternalSnapshot) {
			ncQCOW2Info info2;
			info2.cluster_bits = header2.cluster_bits;
			info2.size = qcow2Info.size;
			info2.filedes = filedes;
			info2.filePath = qcow2Info.filePath;
			info2.realSize = qcow2Info.realSize;
			info2.l1_size = header2.l1_size;
			info2.l1_table_offset = header2.l1_table_offset;
			uint64_t cluster_offset2 = GetQCOW2ClusterOffset (info2, offset);
			if (cluster_offset != cluster_offset2) {
				if (! cluster_offset) {
					cluster_offset = cluster_offset2;
				}
			}
			else {
				cluster_offset = 0;
			}
		}
		// 基于外部快照时，若此簇已分配，则说明是变化块
		if (cluster_offset) {
			ncQCOW2ChangeInfo changeInfo;
			changeInfo.filePath = qcow2Info.filePath;
			changeInfo.lengthSector = cluster_Sectors;
			uint64_t remainSectorLen = (qcow2Info.realSize - cluster_offset)/QCOW2_SECTOR_SIZE;
			if (remainSectorLen < cluster_Sectors) {
				changeInfo.lengthSector = remainSectorLen;
			}

			// 此次只剔除startSector重复项 (map会自动从小到大排序)
			map<uint64_t, ncQCOW2ChangeInfo>::iterator iter = changeInfoMap.find(startSector);
			if (iter != changeInfoMap.end()) {
				if (iter->second.lengthSector <= changeInfo.lengthSector) {
					changeInfoMap.erase(iter);
				}
				else {
					uint64_t tempStartSector = startSector + changeInfo.lengthSector;
					ncQCOW2ChangeInfo tempChangeInfo;
					tempChangeInfo.lengthSector = iter->second.lengthSector - changeInfo.lengthSector;
					tempChangeInfo.filePath = iter->second.filePath;
					changeInfoMap.erase(iter);
					changeInfoMap.insert(make_pair(tempStartSector, tempChangeInfo));
				}
			}
			changeInfoMap.insert(make_pair(startSector, changeInfo));
		}
		if (sectorNum < cluster_Sectors)
			break;

		startSector += cluster_Sectors;
		sectorNum -= cluster_Sectors;
	}

	// 合并位于相同文件的连续区间（以文件划分区间）
	for (map<uint64_t, ncQCOW2ChangeInfo>::iterator iter = changeInfoMap.begin(); iter != changeInfoMap.end(); ) {
		uint64_t firstStartSector = iter->first;
		ncQCOW2ChangeInfo firstChangeInfo;
		firstChangeInfo.lengthSector = iter->second.lengthSector;
		firstChangeInfo.filePath = iter->second.filePath;
		uint64_t firstLen = firstStartSector + firstChangeInfo.lengthSector;
		++iter;
		if (iter == changeInfoMap.end())
			break;

		uint64_t secondStartSector = iter->first;
		ncQCOW2ChangeInfo secondChangeInfo;
		secondChangeInfo.lengthSector = iter->second.lengthSector;
		secondChangeInfo.filePath = iter->second.filePath;

		int res = firstChangeInfo.filePath.compare(secondChangeInfo.filePath);
		if (0 == res && firstLen == secondStartSector) {
			changeInfoMap.erase(firstStartSector);
			changeInfoMap.erase(secondStartSector);

			uint64_t tempStartSector = firstStartSector;
			ncQCOW2ChangeInfo tempChangeInfo;
			tempChangeInfo.lengthSector = firstChangeInfo.lengthSector + secondChangeInfo.lengthSector;
			tempChangeInfo.filePath = firstChangeInfo.filePath;
			pair<map<uint64_t, ncQCOW2ChangeInfo>::iterator, bool> ret;
			ret = changeInfoMap.insert(make_pair(tempStartSector, tempChangeInfo));
			iter = ret.first;
		}
	}

	// 剔除重叠扇区区间
	for (map<uint64_t, ncQCOW2ChangeInfo>::iterator iter = changeInfoMap.begin(); iter != changeInfoMap.end(); ) {
		uint64_t firstStartSector = iter->first;
		ncQCOW2ChangeInfo firstChangeInfo;
		firstChangeInfo.lengthSector = iter->second.lengthSector;
		firstChangeInfo.filePath = iter->second.filePath;
		++iter;
		if (iter == changeInfoMap.end())
			break;

		uint64_t secondStartSector = iter->first;
		ncQCOW2ChangeInfo secondChangeInfo;
		secondChangeInfo.lengthSector = iter->second.lengthSector;
		secondChangeInfo.filePath = iter->second.filePath;

		uint64_t firstLen = firstStartSector + firstChangeInfo.lengthSector;
		uint64_t secondLen = secondStartSector + secondChangeInfo.lengthSector;
		if (firstLen > secondStartSector) {
			int firstRes = firstChangeInfo.filePath.compare(qcow2Info.filePath);
			int secondRes = secondChangeInfo.filePath.compare(qcow2Info.filePath);
			if (0 == firstRes) {
				changeInfoMap.erase(iter);
				iter = changeInfoMap.find(firstStartSector);

				if (firstLen < secondLen) {
					uint64_t tempStartSector = firstLen;
					ncQCOW2ChangeInfo tempChangeInfo;
					tempChangeInfo.lengthSector = secondLen - firstLen;
					tempChangeInfo.filePath = secondChangeInfo.filePath;
					changeInfoMap.insert(make_pair(tempStartSector, tempChangeInfo));
					iter = changeInfoMap.begin();
				}
			}
			else if (0 == secondRes) {
				uint64_t tempStartSector1 = firstStartSector;
				ncQCOW2ChangeInfo tempChangeInfo1;
				tempChangeInfo1.lengthSector = secondStartSector - firstStartSector;
				tempChangeInfo1.filePath = firstChangeInfo.filePath;
				changeInfoMap.erase(firstStartSector);
				changeInfoMap.insert(make_pair(tempStartSector1, tempChangeInfo1));

				if (firstLen > secondLen) {
					uint64_t tempStartSector2 = secondLen;
					ncQCOW2ChangeInfo tempChangeInfo2;
					tempChangeInfo2.lengthSector = firstLen - secondLen;
					tempChangeInfo2.filePath = firstChangeInfo.filePath;
					changeInfoMap.insert(make_pair(tempStartSector2, tempChangeInfo2));
				}
				iter = changeInfoMap.find(secondStartSector);
			}
		}
	}

	qcow2_info ("QueryChangedAreas end.\n");
	return true;
}

// 若数据全为0，则不分配并写入到qcow2磁盘文件中
inline bool isMemcmpZero (const void *s, size_t n)
{
	bool res;
	char *s2 = new char[n];
	memset ((void *)s2, 0, n);
	if (0 == memcmp(s, s2, n))
		res = true;
	else
		res = false;

	delete[] s2;
	s2 = NULL;

	return res;
}

static bool
IsFileExists (const char *path)
{
	bool ret = false;
	int filedes = OpenFile (path, O_RDONLY);
	if (filedes > 0) {
		ret = true;
		CloseFile (filedes);
	}
	return ret;
}

// 若引用计数为0，将此簇引用计数设置为1
inline bool setRefcount (Qcow2LibHandle diskHandle, uint64_t offset)
{
	uint64_t cluster_size = diskHandle->cluster_size;
	uint32_t refcount_bits = 1 << diskHandle->pHeader->refcount_order;
	uint64_t refcount_block_entries = (cluster_size * 8 / refcount_bits);

	uint32_t refcount_block_index = (offset / cluster_size) % refcount_block_entries;
	uint32_t refcount_table_index = (offset / cluster_size) / refcount_block_entries;
	uint64_t refcount_block = 0;
	int rc = pread(diskHandle->filedes, &refcount_block, sizeof(uint64_t), diskHandle->pHeader->refcount_table_offset + refcount_table_index * sizeof(uint64_t));
	if (rc == -1) {
		qcow2_warn ("Could not read refcount_block offset.\n");
		return false;
	}
	be64_to_cpus (&refcount_block);
	if (0 == refcount_block) {
		refcount_block = diskHandle->cluster_offset + cluster_size;
		if (refcount_block < diskHandle->pHeader->l1_table_offset) {
			qcow2_warn ("Could not write refcont table.\n");
			return false;
		}
		uint64_t refcountBlock = refcount_block;
		cpus_to_0be64 (&refcountBlock);
		int rc = pwrite(diskHandle->filedes, (const void *)&refcountBlock, sizeof(uint64_t), diskHandle->pHeader->refcount_table_offset + refcount_table_index * sizeof(uint64_t));
		if (rc == -1) {
			qcow2_warn ("pwrite error %d.\n", errno);
			return false;
		}
		else if (rc != sizeof(uint64_t)) {
			qcow2_warn ("Expected %lu bytes but set %uz bytes.\n", sizeof(uint64_t), rc);
			return false;
		}
		diskHandle->cluster_offset += cluster_size;
		if (! setRefcount (diskHandle, refcount_block)) {
			qcow2_warn ("Set the refcount of offset(%lu) failed.\n", refcount_block);
			return false;
		}
	}

	uint16_t refcount = 0;
	rc = pread(diskHandle->filedes, &refcount, sizeof(uint16_t), refcount_block + refcount_block_index * sizeof(uint16_t));
	if (rc == -1) {
		qcow2_warn ("Could not read refcount.\n");
		return false;
	}
	be16_to_cpus (&refcount);
	if (0 == refcount) {
		refcount = 1;
		cpus_to_be16 (&refcount);
		int rc = pwrite(diskHandle->filedes, (const void *)&refcount, sizeof(uint16_t), refcount_block + refcount_block_index * sizeof(uint16_t));
		if (rc == -1) {
			qcow2_warn ("pwrite error %d.\n", errno);
			return false;
		}
		else if (rc != sizeof(uint16_t)) {
			qcow2_warn ("Expected %lu bytes but set %uz bytes.\n", sizeof(uint16_t), rc);
			return false;
		}
	}

	return true;
}

static bool readSectors (Qcow2LibHandle diskHandle,
						 Qcow2LibSectorType startSector,
						 Qcow2LibSectorType numSectors,
						 bool &isFind,
						 uint8_t *readBuffer)
{
	qcow2_info ("readSectors start. startSector : %lu, numSectors : %lu.\n", startSector, numSectors);
	
	/*
	 * 用户获取到的是以文件划分开的区间，因此按获取区间读扇区时，不需再拆分
	 * 需再根据长度判断是否是多个簇（分别读取再拼接）
	 */
	isFind = false;
	for (vector<ncQCOW2Info>::iterator iter = g_backingFileInfoSet.begin(); iter != g_backingFileInfoSet.end(); ++iter) {
		uint64_t offset = startSector * QCOW2_SECTOR_SIZE;
		uint64_t cluster_offset = GetQCOW2ClusterOffset (*iter, offset);
		if (0 == cluster_offset) {
			continue;
		}
		else {
			isFind = true;
			// 找到簇
			uint64_t count = numSectors * QCOW2_SECTOR_SIZE;
			uint64_t cluster_size = 1 << iter->cluster_bits;
			uint64_t cluster_sector = cluster_size/QCOW2_SECTOR_SIZE;
			uint8_t* pReadBuf = readBuffer;
			while (count) {
				uint64_t index_in_cluster = startSector & (cluster_sector - 1);
				uint64_t len_sector = (index_in_cluster + numSectors > cluster_sector) ? (cluster_sector - index_in_cluster) : numSectors;
				uint64_t len_byte = len_sector * QCOW2_SECTOR_SIZE;
				cluster_offset = GetQCOW2ClusterOffset (*iter, offset);
				if (0 == cluster_offset) {
					qcow2_warn( "Unexpected error.\n");
					return false;
				}
				char* diskPath = strdup (iter->filePath.c_str());
				if (! IsFileExists(diskPath)) {
					string error = strerror(errno);
					qcow2_warn ("%s\n", error.c_str());
					free (diskPath);
					return false;
				}
				free (diskPath);
				char* readBuf = new char[len_byte];
				int rc = pread (iter->filedes, (void *)readBuf, len_byte, cluster_offset + index_in_cluster * QCOW2_SECTOR_SIZE);
				if (rc == -1) {
					qcow2_warn( "pread error %d.\n", errno);
					delete[] readBuf;
					readBuf = NULL;
					return false;
				}
				else if (rc != (int)len_byte) {
					qcow2_warn ("Expected %lu bytes but got %u bytes.\n", len_byte, rc);
					delete[] readBuf;
					readBuf = NULL;
					return false;
				}
				memcpy ((void *)pReadBuf, (const void *)readBuf, len_byte);
				pReadBuf += len_byte;
				delete[] readBuf;
				readBuf = NULL;

				startSector += len_sector;
				numSectors -= len_sector;
				count -= len_byte;
				offset += len_byte;
			}
			break;
		}
	}
	
	qcow2_info ("readSectors end.\n");
	return true;
}

////////////////////////////////////////////////////////////////////////////////////

static bool
QueryChangedDiskAreas (string startSpName,
					   bool	isFullBackup,
					   map<uint64_t, uint64_t> &changeSectorMap)
{
	qcow2_info ("QueryChangedDiskAreas start.\n");

	changeSectorMap.clear();

	map<uint64_t, ncQCOW2ChangeInfo> changeInfoMap;
	vector<ncQCOW2Info> backingFileInfoSet;
	for (vector<ncQCOW2Info>::iterator iter = g_backingFileInfoSet.begin(); iter != g_backingFileInfoSet.end(); ++iter) {
		backingFileInfoSet.insert(backingFileInfoSet.begin(), *iter);
	}
	for (vector<ncQCOW2Info>::iterator iter = backingFileInfoSet.begin(); iter != backingFileInfoSet.end(); ++iter) {
		if (! QueryChangedAreas(*iter, startSpName, isFullBackup, changeInfoMap)) {
			qcow2_warn ("QueryChangedAreas failed.\n");
			return false;
		}
	}

	// 遍历赋值
	for (map<uint64_t, ncQCOW2ChangeInfo>::iterator iter = changeInfoMap.begin(); iter != changeInfoMap.end(); ++iter) {
		changeSectorMap.insert(make_pair(iter->first, iter->second.lengthSector));
	}

	qcow2_info ("QueryChangedDiskAreas end.\n");
	return true;
}

bool
QueryChangedAreasOnExtSp (string startPath,
						  string endPath,
						  map<uint64_t, uint64_t> &changeSectorMap)
{
	qcow2_info ("QueryChangedAreasOnExtSp start.\n");
	qcow2_info ("startPath : %s, endPath : %s.\n", startPath.c_str(), endPath.c_str());

	changeSectorMap.clear();
	bool isNeedRelease = g_logFunc.snapshotType == QCOW2_DEFAULT;
	g_logFunc.snapshotType = QCOW2_EXTERNAL_SNAPSHOT;
	if (startPath.empty() || endPath.empty() || 0 == startPath.compare(endPath)) {
		qcow2_warn ("Please input the corrent path.\n");
		return false;
	}

	bool res = InitForQueryChangedDiskAreas (startPath, endPath, "", "", isNeedRelease);
	if (! res || g_backingFileInfoSet.size() == 0) {
		qcow2_warn ("Init for QueryChangedDiskAreas failed.\n");
		return false;
	}
	
	bool isFullBackup = (0 == startPath.compare("*"));
	res = QueryChangedDiskAreas ("", isFullBackup, changeSectorMap);
	if (! res) {
		qcow2_warn ("Query changed disk areas error.\n");
		return false;
	}

	// 记录
	map<string, vector<ncQCOW2Info> >::iterator mapIter = g_backingFileInfoSetMap.find(endPath);
	if (mapIter != g_backingFileInfoSetMap.end()) {
		vector<ncQCOW2Info> backingFileInfoSet = mapIter->second;
		g_backingFileInfoSetMap.erase(mapIter);
		for (vector<ncQCOW2Info>::iterator iter = backingFileInfoSet.begin(); iter != backingFileInfoSet.end(); ++iter) {
			CloseFile (iter->filedes);
		}
		vector<ncQCOW2Info>().swap(backingFileInfoSet);
	}
	g_backingFileInfoSetMap.insert(make_pair(endPath, g_backingFileInfoSet));
	
	qcow2_info ("QueryChangedAreasOnExtSp end.\n");
	return true;
}

bool
QueryChangedAreasOnIntSp (string startSpName,
						  string endSpName,
						  string path,
						  map<uint64_t, uint64_t> &changeSectorMap)
{
	qcow2_info ("QueryChangedAreasOnIntSp start.\n");
	qcow2_info ("startSpName : %s, endSpName : %s, path : %s.\n", startSpName.c_str(), endSpName.c_str(), path.c_str());
	
	changeSectorMap.clear();
	bool isNeedRelease = g_logFunc.snapshotType == QCOW2_DEFAULT;
	g_logFunc.snapshotType = QCOW2_INTERNAL_SNAPSHOT;
	if (startSpName.empty() || endSpName.empty() || 0 == startSpName.compare(endSpName) || path.empty()) {
		qcow2_warn ("Please input the corrent path or snapshot name.\n");
		return false;
	}
	bool res = InitForQueryChangedDiskAreas ("", path, startSpName, endSpName, isNeedRelease);
	if (! res || g_backingFileInfoSet.size() == 0) {
		qcow2_warn ("Init for QueryChangedDiskAreas failed.\n");
		return false;
	}

	bool isFullBackup = (0 == startSpName.compare("*"));
	res = QueryChangedDiskAreas (startSpName, isFullBackup, changeSectorMap);
	if (! res) {
		qcow2_warn ("Query changed disk areas error.\n");
		return false;
	}

	// 记录
	map<string, vector<ncQCOW2Info> >::iterator mapIter = g_backingFileInfoSetMap.find(path);
	if (mapIter != g_backingFileInfoSetMap.end()) {
		vector<ncQCOW2Info> backingFileInfoSet = mapIter->second;
		g_backingFileInfoSetMap.erase(mapIter);
		for (vector<ncQCOW2Info>::iterator iter = backingFileInfoSet.begin(); iter != backingFileInfoSet.end(); ++iter) {
			CloseFile (iter->filedes);
		}
		vector<ncQCOW2Info>().swap(backingFileInfoSet);
	}
	g_backingFileInfoSetMap.insert(make_pair(path, g_backingFileInfoSet));

	qcow2_info ("QueryChangedAreasOnIntSp end.\n");
	return true;
}

Qcow2Error
Qcow2Lib_Init (Qcow2LibGenericLogFunc *log,
			   Qcow2LibGenericLogFunc *warn,
			   Qcow2LibGenericLogFunc *panic)
{
	if (! (log && warn && panic)) {
		return QCOW2_ERROR_INVALID_ARG;
	}

	g_logFunc.log = log;
	g_logFunc.warn = warn;
	g_logFunc.panic = panic;
	g_logFunc.snapshotType = QCOW2_DEFAULT;

	g_backingFileInfoSet.clear();
	g_backingFileInfoSetMap.clear();
	g_changeSectorMap.clear();
	g_snapshotName = "";

	qcow2_info ("Qcow2Lib_Init finished.\n");
	return QCOW2_OK;
}

void
Qcow2Lib_Exit (void)
{
	qcow2_info ("Qcow2Lib_Exit start.\n");

	if (g_logFunc.snapshotType == QCOW2_DEFAULT) {
		for (vector<ncQCOW2Info>::iterator iter = g_backingFileInfoSet.begin(); iter != g_backingFileInfoSet.end(); ++iter) {
			CloseFile (iter->filedes);
		}
		vector<ncQCOW2Info>().swap(g_backingFileInfoSet);
	}
	g_backingFileInfoSet.clear();

	for (map<string, vector<ncQCOW2Info> >::iterator mapIter = g_backingFileInfoSetMap.begin(); mapIter != g_backingFileInfoSetMap.end(); ++mapIter) {
		vector<ncQCOW2Info> backingFileInfoSet = mapIter->second;
		for (vector<ncQCOW2Info>::iterator iter = backingFileInfoSet.begin(); iter != backingFileInfoSet.end(); ++iter) {
			CloseFile (iter->filedes);
		}
		vector<ncQCOW2Info>().swap(backingFileInfoSet);
	}
	g_backingFileInfoSetMap.clear();
	g_changeSectorMap.clear();

	qcow2_info ("Qcow2Lib_Exit end.\n");
}

Qcow2Error
Qcow2Lib_Open (const char *path,
			   uint32_t flags,
			   Qcow2LibHandle *diskHandle)
{
	qcow2_info ("Qcow2Lib_Open start.\n");
	
	if (diskHandle == NULL) {
		qcow2_warn("Fail to malloc a Qcow2LibHandle.\n");
		return QCOW2_ERROR_OUT_OF_MEMORY;
	}

	Qcow2LibHandle handle = new Qcow2LibHandleStruct;
	string filePath(path);
	if (filePath.empty()) {
		if (handle) {
			delete handle;
			handle = NULL;
		}
		qcow2_warn("Please input the current path.\n");
		return QCOW2_ERROR_INVALID_ARG;
	}
	
	if (! (flags & QCOW2LIB_FLAG_OPEN_READ_ONLY)) {
		handle->pHeader = new QcowHeader;
		bool result = GetQCOW2Header (path, *handle->pHeader);
		if (! result) {
			qcow2_warn ("Could not get qcow2 header.\n");
			if (handle->pHeader) {
				delete handle->pHeader;
				handle->pHeader = NULL;
			}
			if (handle) {
				delete handle;
				handle = NULL;
			}
			return QCOW2_ERROR_FAILED;
		}

		int filedes = OpenFile (path, O_RDWR);
		if (filedes <= 0) {
			if (handle->pHeader) {
				delete handle->pHeader;
				handle->pHeader = NULL;
			}
			if (handle) {
				delete handle;
				handle = NULL;
			}
			return QCOW2_ERROR_FAILED;
		}
		handle->path_buf = filePath;
		handle->filedes = filedes;
		handle->cluster_size = 1 << handle->pHeader->cluster_bits;
		handle->cluster_offset = 0;
	}
	else {
		// 只读方式，打开只为验证磁盘文件存在
		int filedes = OpenFile (path, O_RDONLY|O_LARGEFILE);
		if (filedes <= 0) {
			if (handle) {
				delete handle;
				handle = NULL;
			}
			return QCOW2_ERROR_FAILED;
		}
		CloseFile (filedes);
		handle->path_buf = filePath;
		handle->filedes = 0;
		handle->pHeader = NULL;
		handle->cluster_size = 0;
		handle->cluster_offset = 0;

		map<string, vector<ncQCOW2Info> >::iterator mapIter = g_backingFileInfoSetMap.find(filePath);
		if (mapIter != g_backingFileInfoSetMap.end()) {
			g_logFunc.snapshotType = QCOW2_EXTERNAL_SNAPSHOT;
			g_backingFileInfoSet.clear();
			g_backingFileInfoSet = mapIter->second;
		}
		else {
			// 打开的绝对路径未查询过变化块，则按照默认方式读
			bool isNeedRelease = g_logFunc.snapshotType == QCOW2_DEFAULT;
			g_logFunc.snapshotType = QCOW2_DEFAULT;
			bool res = InitForQueryChangedDiskAreas ("*", path, "", "", isNeedRelease);
			if (! res || g_backingFileInfoSet.size() == 0) {
				qcow2_warn ("Init for QueryChangedDiskAreas failed.\n");
				return QCOW2_ERROR_FAILED;
			}

			bool ret = QueryChangedDiskAreas ("", true, g_changeSectorMap);
			if (! ret) {
				qcow2_warn ("Could not query changed areas info.\n");
				return QCOW2_ERROR_FAILED;
			}
		}
		g_logFunc.path = filePath;
	}
	*diskHandle = handle;
	
	qcow2_info ("Qcow2Lib_Open end.\n");
	return QCOW2_OK;
}

Qcow2Error
Qcow2Lib_Close (Qcow2LibHandle diskHandle)
{
	qcow2_info ("Qcow2Lib_Close start.\n");

	if (g_logFunc.snapshotType == QCOW2_DEFAULT) {
		for (vector<ncQCOW2Info>::iterator iter = g_backingFileInfoSet.begin(); iter != g_backingFileInfoSet.end(); ++iter) {
			CloseFile (iter->filedes);
		}
		vector<ncQCOW2Info>().swap(g_backingFileInfoSet);
	}
	if (0 == g_logFunc.path.compare(diskHandle->path_buf)) {
		g_logFunc.path = "";
	}

	CloseFile (diskHandle->filedes);

	if (diskHandle->pHeader) {
		delete diskHandle->pHeader;
		diskHandle->pHeader = NULL;
	}

	if (diskHandle) {
		delete diskHandle;
		diskHandle = NULL;
	}

	g_changeSectorMap.clear();

	qcow2_info ("Qcow2Lib_Close end.\n");
	return QCOW2_OK;
}

Qcow2Error
Qcow2Lib_Clean (const char *path)
{
	qcow2_info ("Qcow2Lib_Clean start.\n");
	// 清空不包含内部快照和外部快照的qcow2文件数据

	int filedes = OpenFile (path, O_RDWR);
	if (filedes <= 0)
		return QCOW2_ERROR_FILE_ACCESS_ERROR;

	uint64_t size = 0;
	size = GetFileSize (path);
	QcowHeader header;
	GetQCOW2Header(path, header);
	// 获取起始扇区（引用计数表开始清空）
	uint64_t refcount_block;
	int rc = pread(filedes, &refcount_block, sizeof(uint64_t), header.refcount_table_offset);
	if (rc == -1) {
		qcow2_warn ("Could not read refcount_block offset.\n");
		return QCOW2_ERROR_FAILED;
	}
	be64_to_cpus (&refcount_block);

	uint64_t cluster_size = 1 << header.cluster_bits;
	uint64_t startByte = refcount_block + (header.l1_table_offset/cluster_size + 1)*sizeof(uint16_t);

	while (startByte < size) {
		uint64_t len_byte = 4*1024*1024;
		if (startByte + len_byte > size) {
			len_byte = size - startByte;
		}
		uint8_t *buff = new uint8_t[len_byte];
		memset (buff, 0, len_byte);
		int rc = pwrite(filedes, (const void *)buff, len_byte, startByte);
		delete[] buff;
		buff = NULL;
		if (rc == -1) {
			qcow2_warn ("Clean data error %d.\n", errno);
			CloseFile (filedes);
			return QCOW2_ERROR_FAILED;
		}
		else if (rc != (int)len_byte) {
			qcow2_warn ("Expected %lu bytes but set %uz bytes.\n", len_byte, rc);
			CloseFile (filedes);
			return QCOW2_ERROR_FILE_ACCESS_ERROR;
		}
		startByte += len_byte;
	}
	CloseFile (filedes);
	return QCOW2_OK;
}

Qcow2Error
Qcow2Lib_Read (Qcow2LibHandle diskHandle,
			   Qcow2LibSectorType startSector,
			   Qcow2LibSectorType numSectors,
			   uint8_t *readBuffer)
{
	qcow2_info ("Qcow2Lib_Read start. startSector : %lu , numSectors : %lu.\n", startSector, numSectors);
	
	if (diskHandle == NULL) {
		qcow2_warn ("The handle is null.\n");
		return QCOW2_ERROR_INVALID_ARG;
	}
	if (diskHandle->path_buf.empty()) {
		qcow2_warn ("Please open the disk.\n");
		return QCOW2_ERROR_FAILED;
	}
	if (numSectors == 0) {
		return QCOW2_OK;
	}
	if (readBuffer == NULL) {
		qcow2_warn ("Fail to malloc a readBuffer.\n");
		return QCOW2_ERROR_OUT_OF_MEMORY;
	}
	// 将内存空间初始化为0
	memset (readBuffer, 0, numSectors * QCOW2_SECTOR_SIZE);

	// 打开的绝对路径未查询过，则按照默认方式读
	if (0 != g_logFunc.path.compare(diskHandle->path_buf)) {
		map<string, vector<ncQCOW2Info> >::iterator mapIter = g_backingFileInfoSetMap.find(diskHandle->path_buf);
		if (mapIter != g_backingFileInfoSetMap.end()) {
			g_logFunc.snapshotType = QCOW2_EXTERNAL_SNAPSHOT;
			g_backingFileInfoSet.clear();
			g_backingFileInfoSet = mapIter->second;
		}
		else {
			bool isNeedRelease = g_logFunc.snapshotType == QCOW2_DEFAULT;
			g_logFunc.snapshotType = QCOW2_DEFAULT;
			bool res = InitForQueryChangedDiskAreas ("*", diskHandle->path_buf, "", "", isNeedRelease);
			if (! res || g_backingFileInfoSet.size() == 0) {
				qcow2_warn ("Init for QueryChangedDiskAreas failed.\n");
				return QCOW2_ERROR_FAILED;
			}

			bool ret = QueryChangedDiskAreas ("", true, g_changeSectorMap);
			if (! ret) {
				qcow2_warn ("Could not query changed areas info.\n");
				return QCOW2_ERROR_FAILED;
			}
		}
		g_logFunc.path = diskHandle->path_buf;
	}

	/*
	 * 需判断相应区间是否存在，如何拼接
	 */
	if (g_logFunc.snapshotType == QCOW2_DEFAULT) {
		// 获取到所包含的变化块区间
		map<uint64_t, uint64_t> changeSectorMap;
		uint64_t markStartSector = startSector;
		uint64_t markNumSectors = numSectors;
		while (markNumSectors) {
			uint64_t markNumSectorsMark = markNumSectors;
			for (map<uint64_t, uint64_t>::iterator iter = g_changeSectorMap.begin(); iter != g_changeSectorMap.end(); ++iter) {
				if (iter->first >= markStartSector + markNumSectors) {
					markNumSectors = 0;
					break;
				}
				if (iter->first + iter->second > markStartSector) {
					uint64_t startSectorTemp = iter->first > markStartSector ? iter->first : markStartSector;
					uint64_t numSectorsTemp = 0;
					if (iter->first + iter->second >= markStartSector + markNumSectors) {
						numSectorsTemp = markStartSector + markNumSectors - startSectorTemp;
						changeSectorMap.insert(make_pair(startSectorTemp, numSectorsTemp));	
						break;
					}
					else {
						numSectorsTemp = iter->first + iter->second - startSectorTemp;
						changeSectorMap.insert(make_pair(startSectorTemp, numSectorsTemp));
					}
					markNumSectors = markStartSector + markNumSectors - (startSectorTemp + numSectorsTemp);
					markStartSector = startSectorTemp + numSectorsTemp;
				}
			}
			if (markNumSectors == markNumSectorsMark)
				break;
		}

		markStartSector = startSector;
		markNumSectors = numSectors;
		uint8_t* markReadBuffer = readBuffer;
		// 遍历获取所有变化块的数据
		for (map<uint64_t, uint64_t>::iterator mapIter = changeSectorMap.begin(); mapIter != changeSectorMap.end(); ++mapIter) {
			startSector = mapIter->first;
			numSectors = mapIter->second;

			markReadBuffer += (startSector - markStartSector) * QCOW2_SECTOR_SIZE;
			markStartSector = startSector;
			bool isFind;
			bool ret = readSectors (diskHandle, startSector, numSectors, isFind, markReadBuffer);
			if (! ret) {
				qcow2_warn ("Could not read sectors data.\n");
				return QCOW2_ERROR_FAILED;
			}
		}
	}
	else {
		bool isFind;
		bool ret = readSectors (diskHandle, startSector, numSectors, isFind, readBuffer);
		if (! ret) {
			qcow2_warn ("Could not read sectors data.\n");
			return QCOW2_ERROR_FAILED;
		}
		if (! isFind) {
			g_logFunc.path = "";
			return Qcow2Lib_Read (diskHandle, startSector, numSectors, readBuffer);
		}
	}

	qcow2_info ("Qcow2Lib_Read end.\n");
	return QCOW2_OK;
}

Qcow2Error
Qcow2Lib_Write (Qcow2LibHandle diskHandle,
				Qcow2LibSectorType startSector,
				Qcow2LibSectorType numSectors,
				const uint8_t *writeBuffer)
{
	qcow2_info ("Qcow2Lib_Write start. startSector : %lu , numSectors : %lu.\n", startSector, numSectors);

	if (diskHandle == NULL) {
		qcow2_warn ("The handle is null.\n");
		return QCOW2_ERROR_INVALID_ARG;
	}
	if (numSectors == 0) {
		return QCOW2_OK;
	}
	if (writeBuffer == NULL) {
		qcow2_warn ("Fail to set a writeBuffer.\n");
		return QCOW2_ERROR_OUT_OF_MEMORY;
	}

	if (diskHandle->filedes <= 0) {
		qcow2_warn ("Qcow2Lib_Open shoule be executed.\n");
		return QCOW2_ERROR_FAILED;
	}

	char* diskPath = strdup (diskHandle->path_buf.c_str());
	if (! IsFileExists(diskPath)) {
		string error = strerror(errno);
		qcow2_warn ("%s\n", error.c_str());
		free (diskPath);
		return QCOW2_ERROR_FAILED;	
	}
	free (diskPath);

	/*
	 * 如果是跨越多个簇，则分别写入
	 */
	uint64_t offset = startSector * QCOW2_SECTOR_SIZE;
	uint64_t count = numSectors * QCOW2_SECTOR_SIZE;
	if (offset + count > diskHandle->pHeader->size) {
		qcow2_warn ("The qcow2 disk is too small.\n");
		return QCOW2_ERROR_INVALID_ARG;
	}
	uint32_t l2_bits = diskHandle->pHeader->cluster_bits - 3;
	uint32_t l1_bits = l2_bits + diskHandle->pHeader->cluster_bits;
	uint32_t l2_size = 1 << l2_bits;
	uint64_t cluster_size = diskHandle->cluster_size;
	uint64_t cluster_sector = cluster_size/QCOW2_SECTOR_SIZE;
	uint64_t nLen = 0;
	const uint8_t* pWriteBuf = writeBuffer;

    uint64_t l1_size_in_one_cluster = diskHandle->cluster_size / sizeof(uint64_t);

    uint32_t l1_cluster_size = diskHandle->pHeader->l1_size % l1_size_in_one_cluster != 0 \
            ? diskHandle->pHeader->l1_size / l1_size_in_one_cluster + 1\
            : diskHandle->pHeader->l1_size / l1_size_in_one_cluster;

	while (count) {
		uint64_t index_in_cluster = startSector & (cluster_sector - 1);
		uint64_t len_sector = (index_in_cluster + numSectors > cluster_sector) ? (cluster_sector - index_in_cluster) : numSectors;
		uint64_t len_byte = len_sector * QCOW2_SECTOR_SIZE;

		uint64_t l1_index = offset >> l1_bits;

		uint64_t l2_offset;
		int rc = pread(diskHandle->filedes, &l2_offset, sizeof(uint64_t), diskHandle->pHeader->l1_table_offset + l1_index * sizeof(uint64_t));
		if (rc == -1) {
			qcow2_warn ("Could not read l2 offset.\n");
			return QCOW2_ERROR_FAILED;
		}
		be64_to_cpus (&l2_offset);
		if (0 == l2_offset) {
            l2_offset = diskHandle->pHeader->l1_table_offset + (l1_index + l1_cluster_size) * cluster_size;
			qcow2_info ("l2_offset : %lu.\n", l2_offset);
			uint64_t l2Offset = l2_offset;
			cpus_to_be64 (&l2Offset);
			int rc = pwrite(diskHandle->filedes, (const void *)&l2Offset, sizeof(uint64_t), diskHandle->pHeader->l1_table_offset + l1_index * sizeof(uint64_t));
			if (rc == -1) {
				qcow2_warn ("pwrite error %d.\n", errno);
				qcow2_warn ("l1_table[%d] : %lu.\n", l1_index, diskHandle->pHeader->l1_table_offset + l1_index * sizeof(uint64_t));
				return QCOW2_ERROR_FAILED;
			}
			else if (rc != sizeof(uint64_t)) {
				qcow2_warn ("Expected %lu bytes but set %uz bytes.\n", sizeof(uint64_t), rc);
				return QCOW2_ERROR_FAILED;
			}

			if (! setRefcount (diskHandle, l2_offset)) {
				qcow2_warn ("Set the refcount of offset(%lu) failed.\n", l2_offset);
				return QCOW2_ERROR_FAILED;			
			}
		}
		
		uint64_t l2_index = (offset >> diskHandle->pHeader->cluster_bits) & (l2_size - 1);
		uint64_t cluster_offset = 0;
		rc = pread(diskHandle->filedes, &cluster_offset, sizeof(uint64_t), l2_offset + l2_index * sizeof(uint64_t));
		if (rc == -1) {
			qcow2_warn ("Could not read l2 offset.\n");
			return QCOW2_ERROR_FAILED;
		}
		be64_to_cpus (&cluster_offset);
		// 簇未分配时，数据为0，则不分配簇，不写入
		bool isWrite = true;

		if (0 == cluster_offset) {
			// 尚未分配此簇，且数据不为0，则分配簇
			if (! isMemcmpZero((const void*)pWriteBuf, len_byte)) {
				if (0 == diskHandle->cluster_offset) {
					// 获取已分配的最大簇偏移量
					uint64_t cluster_off = 0;
					for (size_t i = 0; i < diskHandle->pHeader->l1_size; ++i) {
						uint64_t l1_off = diskHandle->pHeader->l1_table_offset + i * sizeof(uint64_t);
						uint64_t l2_off;
						int rc = pread(diskHandle->filedes, &l2_off, sizeof(uint64_t), l1_off);
						if (rc == -1) {
							qcow2_warn ("Could not read l2 offset.\n");
							return QCOW2_ERROR_FAILED;
						}
						be64_to_cpus (&l2_off);
						if (l2_off) {
							for (size_t j = 0; j < l2_size; ++j) {
								rc = pread(diskHandle->filedes, &cluster_off, sizeof(uint64_t), l2_off + j * sizeof(uint64_t));
								if (rc == -1) {
									qcow2_warn ("Could not read l2 offset.\n");
									return QCOW2_ERROR_FAILED;
								}
								be64_to_cpus (&cluster_off);
								if (cluster_off > cluster_offset) {
									cluster_offset = cluster_off;
								}
							}
						}
					}
					if (0 == cluster_offset) {
						cluster_offset = diskHandle->pHeader->l1_table_offset + (diskHandle->pHeader->l1_size + 1) * cluster_size;
					}
					else {
						cluster_offset += cluster_size;
					}
				}
				else {
					cluster_offset = diskHandle->cluster_offset + cluster_size;
				}

				uint64_t clusterOffset = cluster_offset;
				if (clusterOffset > diskHandle->pHeader->size) {
					qcow2_warn ("The qcow2 disk is too small.\n");
					return QCOW2_ERROR_INVALID_ARG;
				}
				
				cpus_to_be64 (&clusterOffset);
				int rc = pwrite(diskHandle->filedes, (const void *)&clusterOffset, sizeof(uint64_t), l2_offset + l2_index * sizeof(uint64_t));
				if (rc == -1) {
					qcow2_warn ("pwrite error %d.\n", errno);
					qcow2_warn ("l2_table[%d] : %lu.\n", l2_index, l2_offset + l2_index * sizeof(uint64_t));
					return QCOW2_ERROR_FAILED;
				}
				else if (rc != sizeof(uint64_t)) {
					qcow2_warn ("Expected %lu bytes but set %uz bytes.\n", sizeof(uint64_t), rc);
					return QCOW2_ERROR_FAILED;
				}
				diskHandle->cluster_offset = cluster_offset;
				if (! setRefcount (diskHandle, cluster_offset)) {
					qcow2_warn ("Set the refcount of offset(%lu) failed.\n", cluster_offset);
					return QCOW2_ERROR_FAILED;			
				}
			}
			else {
				isWrite = false;
			}
		}
		// 写入数据
		if (isWrite) {
			rc = pwrite(diskHandle->filedes, (const void *)pWriteBuf, len_byte, cluster_offset + index_in_cluster * QCOW2_SECTOR_SIZE);
			if (rc == -1) {
				qcow2_warn ("pwrite error %d.\n", errno);
				qcow2_warn ("cluster_offset : %lu\n", cluster_offset);
				return QCOW2_ERROR_FAILED;
			}
			else if (rc != (int)len_byte) {
				qcow2_warn ("Expected %lu bytes but set %uz bytes.\n", len_byte, rc);
				return QCOW2_ERROR_FILE_ACCESS_ERROR;
			}
		}

		pWriteBuf += len_byte;
		startSector += len_sector;
		numSectors -= len_sector;
		count -= len_byte;
		offset += len_byte;
		nLen += len_byte;
	}

	qcow2_info ("Qcow2Lib_Write end.\n");
	return QCOW2_OK;
}
