
/* dist: public */

#ifndef __PACKETS_REQUESTFILE_H
#define __PACKETS_REQUESTFILE_H

#pragma pack(push,1)

struct S2CRequestFile
{
	u8 type;
	char path[256];
	char fname[16];
};

#pragma pack(pop)

#endif

