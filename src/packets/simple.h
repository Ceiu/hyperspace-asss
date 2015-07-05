
/* dist: public */

#ifndef __PACKETS_SIMPLE_H
#define __PACKETS_SIMPLE_H

#pragma pack(push,1)

/* simple.h - generic packets */

struct SimplePacket
{
	u8 type;
	i16 d1, d2, d3, d4, d5;
};

struct SimplePacketA
{
	u8 type;
	i16 d[1];
};

#pragma pack(pop)

#endif

