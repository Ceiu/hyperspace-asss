
/* dist: public */

#ifndef __PACKETS_GREEN_H
#define __PACKETS_GREEN_H

#pragma pack(push,1)

struct GreenPacket
{
	u8 type;
	u32 time;
	i16 x;
	i16 y;
	i16 green;
	i16 pid;
};

#pragma pack(pop)

#endif

