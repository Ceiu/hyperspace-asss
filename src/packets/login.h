
/* dist: public */

#ifndef __PACKETS_LOGIN_H
#define __PACKETS_LOGIN_H

#pragma pack(push,1)

/* login.h - player login packet */

struct LoginPacket
{
	u8 type;
	u8 flags;
	char name[32];
	char password[32];
	u32 macid;
	i8 blah;
	u16 timezonebias;
	u16 unk1;
	i16 cversion;
	i32 field444, field555;
	u32 D2;
	i8 blah2[12];
	u8 contid[64]; /* cont only */
};

#define LEN_LOGINPACKET_VIE (sizeof(struct LoginPacket) - 64)
#define LEN_LOGINPACKET_CONT sizeof(struct LoginPacket)

#pragma pack(pop)

#endif

