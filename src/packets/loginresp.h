
/* dist: public */

#ifndef __PACKETS_LOGINRESP_H
#define __PACKETS_LOGINRESP_H

#pragma pack(push,1)

/* loginresp.h - login repsonse packet */

struct S2CLoginResponse
{
	u8 type;
	u8 code;
	u32 serverversion;
	u8 isvip;
	u8 blah[3];
	u32 exechecksum;
	u8 blah2[5];
	u8 demodata;
	u32 codechecksum;
	u32 newschecksum;
	u8 blah4[8];
};

#pragma pack(pop)

#endif

