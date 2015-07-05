
/* dist: public */

#ifndef __PACKETS_KILL_H
#define __PACKETS_KILL_H

#pragma pack(push,1)

struct KillPacket
{
	u8 type;
	u8 green;
	i16 killer;
	i16 killed;
	i16 bounty;
	i16 flags;
};

#pragma pack(pop)

#endif

