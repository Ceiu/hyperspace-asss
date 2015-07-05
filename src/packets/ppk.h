
/* dist: public */

#ifndef __PACKETS_PPK_H
#define __PACKETS_PPK_H

#pragma pack(push,1)

/* ppk.h - position and weapons packets */

/* weapon codes */
#define W_NULL          0
#define W_BULLET        1
#define W_BOUNCEBULLET  2
#define W_BOMB          3
#define W_PROXBOMB      4
#define W_REPEL         5
#define W_DECOY         6
#define W_BURST         7
#define W_THOR          8
#define W_WORMHOLE      0 /* used in watchdamage packet only */
#define W_SHRAPNEL     15 /* used in watchdamage packet only */

struct Weapons /* 2 bytes */
{
	u16 type : 5;
	u16 level : 2;
	u16 shrapbouncing : 1;
	u16 shraplevel : 2;
	u16 shrap : 5;
	u16 alternate : 1;
};


struct ExtraPosData /* 10 bytes */
{
	u16 energy;
	u16 s2cping;
	u16 timer;
	u32 shields : 1;
	u32 super : 1;
	u32 bursts : 4;
	u32 repels : 4;
	u32 thors : 4;
	u32 bricks : 4;
	u32 decoys : 4;
	u32 rockets : 4;
	u32 portals : 4;
	u32 padding : 2;
};


struct S2CWeapons
{
	u8 type; /* 0x05 */
	i8 rotation;
	u16 time;
	i16 x;
	i16 yspeed;
	u16 playerid;
	i16 xspeed;
	u8 checksum;
	u8 status;
	u8 c2slatency;
	i16 y;
	u16 bounty;
	struct Weapons weapon;
	struct ExtraPosData extra;
};


struct S2CPosition
{
	u8 type; /* 0x28 */
	i8 rotation;
	u16 time;
	i16 x;
	u8 c2slatency;
	u8 bounty;
	u8 playerid;
	u8 status;
	i16 yspeed;
	i16 y;
	i16 xspeed;
	struct ExtraPosData extra;
};


struct C2SPosition
{
	u8 type; /* 0x03 */
	i8 rotation;
	u32 time;
	i16 xspeed;
	i16 y;
	u8 checksum;
	u8 status;
	i16 x;
	i16 yspeed;
	u16 bounty;
	i16 energy;
	struct Weapons weapon;
	struct ExtraPosData extra;
};

#pragma pack(pop)

#endif

