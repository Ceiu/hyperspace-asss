
/* dist: public */

#ifndef __PACKETS_CLIENTSET_H
#define __PACKETS_CLIENTSET_H

#pragma pack(push,1)

/* structs for packet types and data */

struct WeaponBits /* 4 bytes */
{
	u32 ShrapnelMax    : 5;
	u32 ShrapnelRate   : 5;
	u32 CloakStatus    : 2;
	u32 StealthStatus  : 2;
	u32 XRadarStatus   : 2;
	u32 AntiWarpStatus : 2;
	u32 InitialGuns    : 2;
	u32 MaxGuns        : 2;
	u32 InitialBombs   : 2;
	u32 MaxBombs       : 2;
	u32 DoubleBarrel   : 1;
	u32 EmpBomb        : 1;
	u32 SeeMines       : 1;
	u32 Unused1        : 3;
};

struct MiscBitfield /* 2 bytes */
{
	u16 SeeBombLevel   : 2;
	u16 DisableFastShooting : 1;
	u16 Radius         : 8;
	u16 _padding       : 5;
};

struct ShipSettings /* 144 bytes */
{
	i32 long_set[2];
	i16 short_set[49];
	i8 byte_set[18];
	struct WeaponBits Weapons;
	byte Padding[16];
};


struct ClientSettings
{
	struct /* 4 bytes */
	{
		u32 type : 8; /* 0x0F */
		u32 ExactDamage : 1;
		u32 HideFlags : 1;
		u32 NoXRadar : 1;
		u32 SlowFrameRate : 3;
		u32 DisableScreenshot : 1;
		u32 _reserved : 1;
		u32 MaxTimerDrift : 3;
		u32 DisableBallThroughWalls : 1;
		u32 DisableBallKilling : 1;
		u32 _padding : 11;
	} bit_set;
	struct ShipSettings ships[8];
	i32 long_set[20];
	struct /* 4 bytes */
	{
		u32 x : 10;
		u32 y : 10;
		u32 r : 9;
		u32 pad : 3;
	} spawn_pos[4];
	i16 short_set[58];
	i8 byte_set[32];
	u8 prizeweight_set[28];
};

#pragma pack(pop)

#endif

