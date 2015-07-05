
/* dist: public */

#include "asss.h"
#include "bwlimit.h"

local BWLimit * New()
{
	return NULL;
}

local void NoOp1(BWLimit *bw)
{
}

local void NoOp2(BWLimit *bw, ticks_t now)
{
}

local int Check(BWLimit *bw, int bytes, int pri)
{
	return TRUE;
}

local int GetCanBufferPackets(BWLimit *bw)
{
	return 30;
}

local void GetInfo(BWLimit *bw, char *buf, int buflen)
{
	astrncpy(buf, "(no limit)", buflen);
}


local struct Ibwlimit bwint =
{
	INTERFACE_HEAD_INIT(I_BWLIMIT, "bw-nolimit")
	New, NoOp1,
	NoOp2, Check,
	NoOp1, NoOp1,
	GetCanBufferPackets, GetInfo
};

EXPORT const char info_bw_nolimit[] = CORE_MOD_INFO("bw_nolimit");

EXPORT int MM_bw_nolimit(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm->RegInterface(&bwint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregInterface(&bwint, ALLARENAS);
		return MM_OK;
	}
	return MM_FAIL;
}

