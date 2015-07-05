
/* dist: public */

#ifndef __PACKETS_WATCHDAMAGE_H
#define __PACKETS_WATCHDAMAGE_H

#pragma pack(push,1)

typedef struct DamageData
{
	i16 shooteruid;
	struct Weapons weapon;
	i16 energy;
	i16 damage;
	u8 unknown;
} DamageData;

struct S2CWatchDamage
{
	u8 type;
	i16 damageuid;
	u32 tick;
	DamageData damage[1];
};

struct C2SWatchDamage
{
	u8 type;
	u32 tick;
	DamageData damage[1];
};

#pragma pack(pop)

#endif

