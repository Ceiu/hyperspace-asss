
/* dist: public */

#include "asss.h"
#include "fg_wz.h"
#include "jackpot.h"


/* prototypes */

local void MyFlagWin(Arena *a, int freq, int *points);

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;

EXPORT const char info_points_flag[] = CORE_MOD_INFO("points_flag");

EXPORT int MM_points_flag(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_WARZONEWIN, MyFlagWin, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_WARZONEWIN, MyFlagWin, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


void MyFlagWin(Arena *arena, int freq, int *ppoints)
{
	int players = 0, onfreq = 0, points;
	Player *i;
	Link *link;
	Ijackpot *jackpot;

	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING &&
		    i->arena == arena &&
		    i->p_ship != SHIP_SPEC &&
		    IS_HUMAN(i))
		{
			players++;
			if (i->p_freq == freq)
				onfreq++;
		}
	pd->Unlock();

	/* cfghelp: Flag:FlagReward, arena, int, def: 5000, mod: points_flag
	 * The basic flag reward is calculated as (players in arena)^2 *
	 * reward / 1000. */
	points = players * players *
		cfg->GetInt(arena->cfg, "Flag", "FlagReward", 5000) / 1000;

	jackpot = mm->GetInterface(I_JACKPOT, arena);
	if (jackpot)
	{
		points += jackpot->GetJP(arena);
		mm->ReleaseInterface(jackpot);
	}

	/* cfghelp: Flag:SplitPoints, arena, bool, def: 0
	 * Whether to split a flag reward between the members of a freq or
	 * give them each the full amount. */
	if (onfreq > 0 && cfg->GetInt(arena->cfg, "Flag", "SplitPoints", 0))
		points /= onfreq;

	*ppoints += points;
}

