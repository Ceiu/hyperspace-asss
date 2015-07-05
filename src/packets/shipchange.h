
/* dist: public */

#ifndef __PACKETS_SHIPCHANGE_h
#define __PACKETS_SHIPCHANGE_h

#pragma pack(push,1)

struct ShipChangePacket
{
	u8 type;
	i8 shiptype;
	i16 pnum;
	i16 freq;
};

#pragma pack(pop)

#endif

