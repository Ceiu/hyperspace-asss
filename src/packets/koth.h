
/* dist: public */

#ifndef __PACKETS_KOTH_H
#define __PACKETS_KOTH_H

#pragma pack(push,1)

struct S2CKoth
{
	u8 type;
	u8 action;
	u32 time;
	i16 pid;
};

#define KOTH_ACTION_ADD_CROWN 1
#define KOTH_ACTION_REMOVE_CROWN 0

struct S2CSetKothTimer
{
	u8 type;
	u32 time;
};

#pragma pack(pop)

#endif

