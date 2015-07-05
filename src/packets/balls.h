
/* dist: public */

#ifndef __PACKETS_BALL_H
#define __PACKETS_BALL_H

#pragma pack(push,1)

struct BallPacket
{
	u8 type; /* 0x2E for s2c, 0x1F for c2s */
	u8 ballid;
	i16 x;
	i16 y;
	i16 xspeed;
	i16 yspeed;
	i16 player;
	u32 time;
};

struct C2SPickupBall
{
	u8 type; /* 0x20 */
	u8 ballid;
	u32 time;
};

struct C2SGoal
{
	u8 type; /* 0x21 */
	u8 ballid;
	i16 x;
	i16 y;
};

#pragma pack(pop)

#endif

