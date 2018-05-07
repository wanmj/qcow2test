#ifndef __XMLCREATE_H__
#define __XMLCREATE_H__

//	<disk type='file' device='disk'>
//	<driver name='qemu' type='qcow2' cache='none' io='native'/>
//	<source file='/var/lib/libvirt/images/data.qcow2'/>
//	<target dev='vdb' bus='virtio'/>
//	</disk>
//
//	for example:
//	virDeviceXMLCreate("qcow2", "/var/lib/libvirt/images/data.qcow2", "vdb");
//	
char *virDeviceXMLCreate(const char *xmlpath, const char *imgpath, const char *targetdev);

int virDeviceXMLFree(char *buffer);

char *virDeviceXMLGet(const char *xmlpath);

#endif
