
/* dist: public */

#ifndef __PACKETS_FLAGS_H
#define __PACKETS_FLAGS_H

#pragma pack(push,1)

struct S2CFlagLocation
{
	u8 type; /* 0x12 */
	u16 fid;
	u16 x;
	u16 y;
	u16 freq;
};

struct C2SFlagPickup
{
	u8 type; /* 0x13 */
	i16 fid;
};

struct S2CFlagPickup
{
	u8 type; /* 0x13 */
	i16 fid;
	i16 pid;
};

struct S2CFlagVictory
{
	u8 type; /* 0x14 */
	i16 freq;
	i32 points;
};

struct S2CFlagDrop
{
	u8 type; /* 0x16 */
	i16 pid;
};

#pragma pack(pop)

#endif

