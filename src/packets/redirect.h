
/* dist: public */

#ifndef __PACKETS_REDIRECT_H
#define __PACKETS_REDIRECT_H

#pragma pack(push,1)

struct S2CRedirect
{
	u8 type;
	u32 ip;
	u16 port;
	i16 arenatype;     /* as in ?go packet */
	i8  arenaname[16];
	u32 loginid;
};

#pragma pack(pop)

#endif

