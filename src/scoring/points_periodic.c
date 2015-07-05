
/* dist: public */

#include "asss.h"

local Iarenaman *aman;
local Iconfig *cfg;

local int getpoints(Arena *arena, int freq, int freqplayers, int totalplayers, int flagsowned)
{
	/* cfghelp: Periodic:RewardPoints, arena, int, def: 100, mod: \
	 * points_periodic
	 * Periodic rewards are calculated as follows: If this setting is
	 * positive, you get this many points per flag. If it's negative,
	 * you get it's absolute value points per flag, times the number of
	 * players in the arena. */
	int rwpts = cfg->GetInt(arena->cfg, "Periodic", "RewardPoints", 100);
	if (rwpts > 0)
		return flagsowned * rwpts;
	else
		return flagsowned * (-rwpts) * totalplayers;
}

local Iperiodicpoints myint =
{
	INTERFACE_HEAD_INIT(I_PERIODIC_POINTS, "pp-basic")
	getpoints
};

EXPORT const char info_points_periodic[] = CORE_MOD_INFO("points_periodic");

EXPORT int MM_points_periodic(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!aman || !cfg) return MM_FAIL;
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (myint.head.global_refcount)
			return MM_FAIL;
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&myint, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&myint, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

