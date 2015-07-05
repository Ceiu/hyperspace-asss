#include <stdlib.h>

#include "asss.h"
#include "hscore.h"

typedef struct
{
	int max_p_exp;
	int min_p_exp;
	int max_metric;
	int priv_freq_start;
	int privs_independent;
	int max_difference;
	int ignore_during_flagwin;
} adata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iarenaman *aman;
local Iflagcore *flagcore;
local Ihscoredatabase *database;

local int adkey;

local int GetPlayerMetric(Player *p)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	int exp = database->getExp(p);

	exp = exp > ad->max_p_exp ? ad->max_p_exp : exp;
	exp = exp > ad->min_p_exp ? exp : ad->min_p_exp;

	return exp;
}

local int GetMaxMetric(Arena *arena, int freq)
{
	adata *ad = P_ARENA_DATA(arena, adkey);

	return ad->max_metric;
}

local int GetMaximumDifference(Arena *arena, int freq1, int freq2)
{
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (ad->ignore_during_flagwin && flagcore)
		if (flagcore->IsWinning(arena, freq1) ||
				flagcore->IsWinning(arena, freq2))
			return 2100000000;

	if (ad->privs_independent)
	{
		int freq1_is_priv = freq1 >= ad->priv_freq_start;
		int freq2_is_priv = freq2 >= ad->priv_freq_start;

		if (freq1_is_priv == freq2_is_priv)
		{
			return ad->max_difference;
		}
		else
		{
			return 2100000000;
		}
	}
	else
	{
		return ad->max_difference;
	}
}

local void update_config(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	ConfigHandle ch = arena->cfg;

	/* cfghelp: Hyperspace:MaxPlayerMetric, arena, int, def: 2100000000
	 * The maximum value the balancing metric (exp) should take. */
	ad->max_p_exp = cfg->GetInt(ch, "Hyperspace", "MaxPlayerMetric", 2100000000);

	/* cfghelp: Hyperspace:MinPlayerMetric, arena, int, def: 0
	 * The minimum value the balancing metric (exp) should take */
	ad->min_p_exp = cfg->GetInt(ch, "Hyperspace", "MinPlayerMetric", 0);

	ad->priv_freq_start = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);

	/* cfghelp: Hyperspace:MaxMetric, arena, int, def: 2100000000
	 * The maximum metric (exp) allowed on a team. */
	ad->max_metric = cfg->GetInt(ch, "Hyperspace", "MaxMetric", 2100000000);

	/* cfghelp: Hyperspace:PrivsIndependent, arena, bool, def: 0
	 * Whether privates and publics are balanced independently by exp. */
	ad->privs_independent = cfg->GetInt(ch, "Hyperspace", "PrivsIndependent", 0);

	/* cfghelp: Hyperspace:MaxDifference, arena, int, def: 1000
	 * The max difference in metrics (exp) that can seperate two teams
	 * being balanced. */
	ad->max_difference = cfg->GetInt(ch, "Hyperspace", "MaxDifference", 1000);

	/* cfghelp: Hyperspace:IgnoreMaxDuringFlagWin, arena, int, def: 1
	 * Ignore the balancer's maximum during flag wins. */
	ad->ignore_during_flagwin = cfg->GetInt(ch, "Hyperspace", "IgnoreMaxDuringFlagWin", 1);
}

local void aaction(Arena *arena, int action)
{
	if (action == AA_CONFCHANGED)
	{
		update_config(arena);
	}
}

local Ibalancer myint =
{
	INTERFACE_HEAD_INIT(I_BALANCER, "test_balancer")
	GetPlayerMetric, GetMaxMetric, GetMaximumDifference
};

EXPORT const char info_hscore_balancer[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_balancer(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !cfg || !aman || !database) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		aman->FreeArenaData(adkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_ARENAACTION, aaction, arena);

		update_config(arena);

		mm->RegInterface(&myint, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		if (mm->UnregInterface(&myint, arena))
			return MM_FAIL;

		mm->UnregCallback(CB_ARENAACTION, aaction, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
