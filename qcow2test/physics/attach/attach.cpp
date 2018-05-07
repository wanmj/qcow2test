#include "attach.h"
#include "xml.h"
#include <stdio.h>
#include <string.h>
#include <libvirt/libvirt.h>

int virAttachVolume(int domain_id, const char *xmlpath, const char *imgpath, const char *devname) {
	virConnectPtr conn = NULL;
	if((conn = virConnectOpen(NULL)) == NULL) {
		fprintf(stderr, "failed to connect to hypervisor.\n");
		return -1;
	}
	
	virDomainPtr domain = NULL;
	if((domain = virDomainLookupByID(conn, domain_id)) == NULL) {
		fprintf(stderr, "failed to find domain %d.\n", domain_id);
		return -1;
	}

	// TODO
	// 此处需要修改，函数参数修改为xmlpath，而不是imgpath
	// 逻辑是如果存在xml文件读取即可，没有xml文件则创建
	// 或者不论是否存在，直接使用给定参数覆盖
	// DONE
	//
	char *xml = NULL;
	if((xml = virDeviceXMLGet(xmlpath)) == NULL) {         // 说明不存在xml文件，需要新建
		fprintf(stderr, "failed to find xml file, creating now...\n");

		if((xml = virDeviceXMLCreate(xmlpath, imgpath, devname)) == NULL) {
			fprintf(stderr, "failed to create device xml file.\n");
			return -1;
		}
		
	}

	
	if(virDomainAttachDevice(domain, xml) != 0) {
		fprintf(stderr, "failed to attach device.\n");
		return -1;
	}
	
	virDeviceXMLFree(xml);
	return 0;
}

int virDetachVolume(int domain_id, const char *xmlpath) {

	virConnectPtr conn = NULL;
	
	if((conn = virConnectOpen(NULL)) == NULL) {
		fprintf(stderr, "failed to connect to hypervisor.\n");
		return -1;
	}
	
	virDomainPtr domain = NULL;
	if((domain = virDomainLookupByID(conn, domain_id)) == NULL) {
		fprintf(stderr, "failed to find domain %d.\n", domain_id);
		return -1;
	}

	char *xml = virDeviceXMLGet(xmlpath);                // 已经挂载说明存在xml文件，如果不存在说明路径有问题，非程序问题
	
	if(virDomainDetachDevice(domain, xml) != 0) {
		fprintf(stderr, "failed to detach device.\n");
		return -1;
	}
	
	virDeviceXMLFree(xml);
	return 0;
}
