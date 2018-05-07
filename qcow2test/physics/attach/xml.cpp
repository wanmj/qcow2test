#include "xml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//	@return need to free by virDeviceXMLFree
//	read xml file to a buffer for virDomainAttachDevice function
//
char *virDeviceXMLGet(const char *xmlpath) {
	FILE *fp = NULL;
	if((fp = fopen(xmlpath, "r")) == NULL) {
		fprintf(stderr, "failed to open(r) file %s.\n", xmlpath);
		return NULL;
	}
	
	char *pbuff = (char *)malloc(400);
	memset(pbuff, 0, 400);
	do {
		fread(pbuff, 1, 399, fp);
	}while(!feof(fp));
	
	fclose(fp);
	return pbuff;
}

//	@return need to free by virDeviceXMLFree
//	create a xml file for a virtual disk
//
char *virDeviceXMLCreate(const char *xmlpath, const char *imgpath, const char *targetdev) {
	if(strlen(xmlpath) > 64 || strlen(imgpath) > 64) {
		printf("xml path is too long.\n");
		return NULL;
	}
	if(strlen(targetdev) > 10) {
		printf("target device name is too long.\n");
		return NULL;
	}

	FILE *fp = fopen(xmlpath, "w");
	if(NULL == fp) {
		printf("failed to open(w) file %s.\n", xmlpath);
		return NULL;
	}

	fprintf(fp, "<disk type='file' device='disk'>\n<driver name='qemu' type='qcow2' cache='none' io='native'/>\n<source file='%s'/>\n<target dev='%s' bus='virtio'/>\n</disk>", imgpath, targetdev);
	fclose(fp);

	char *ret = virDeviceXMLGet(xmlpath);
	return ret;

}

//	free memory created by malloc of virDeviceXMLCreate or virDeviceXMLGet
//
int virDeviceXMLFree(char *buff) {
	if(buff != NULL) {
		free(buff);
	}
	return 0;
}
