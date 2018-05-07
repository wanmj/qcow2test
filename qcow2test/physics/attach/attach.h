#ifndef __ATTACH_H__
#define __ATTACH_H__

int virAttachVolume(int domain_id, const char *xmlpath, const char *imgpath, const char *devname);

int virDetachVolume(int id, const char *imgpath);

#endif
