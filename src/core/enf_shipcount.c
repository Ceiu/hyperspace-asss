
/* dist: public */

#include <stdio.h>

#include "asss.h"

local Iconfig *cfg;
local Ilogman *lm;
local Iplayerdata *pd;


local int CanChangeToShip(Player *p, int new_ship, int is_changing, char *err_buf, int buf_len)
{
	Link *link;
	Player *x;
	int allow = 1;

	if (new_ship >= 0 && new_ship < SHIP_SPEC)
	{
		int limit = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[new_ship], "LimitPerTeam", -1);

		if (!limit)
		{
			allow = 0;

			if (err_buf)
				snprintf(err_buf, buf_len, "No %ss are allowed in this arena.", cfg->SHIP_NAMES[new_ship]);
		}
		else if (limit > 0)
		{
			int count = 0;
			// Count the number of ships already on this their team.
			pd->Lock();
			FOR_EACH_PLAYER_IN_ARENA(x, p->arena)
			{
				if (x->p_freq != p->p_freq)
					continue;
				if (x->p_ship == SHIP_SPEC)
					continue;
				if (x == p)
					continue;

				++count;
			}
			pd->Unlock();

			if (count >= limit)
			{
				allow = 0;

				if (err_buf)
					snprintf(err_buf, buf_len, "There are already the maximum number of %s pilots on your team.", cfg->SHIP_NAMES[new_ship]);
			}
		}
	}

	return allow;
}

local shipmask_t GetAllowableShips(Player *p, int freq, char *err_buf, int buf_len)
{
	int i;
	Link *link;
	Player *x;
	int count[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	shipmask_t mask = SHIPMASK_NONE;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(x, p->arena)
	{
		if (x->p_freq != freq)
			continue;
		if (x->p_ship == SHIP_SPEC)
			continue;
		if (x == p)
			continue;
		++count[x->p_ship];
	}
	pd->Unlock();

	for (i = 0; i < SHIP_SPEC; ++i)
	{
		/* cfghelp: All:LimitPerTeam, arena, int, def: -1, mod: enf_shipcount
		 * The maximum number of this ship on any given frequency. -1 means no limit. */
		int limit = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[i], "LimitPerTeam", -1);

		if (limit == -1)
		{
			mask |= (1 << i);
			continue;
		}

		if (count[i] < limit)
		{
			mask |= (1 << i);
		}
	}

	return mask;
}

local Aenforcer enforceradv =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
  NULL,
  NULL,
  CanChangeToShip,
  NULL,
	GetAllowableShips
};

EXPORT const char info_enf_shipcount[] = CORE_MOD_INFO("enf_shipcount");

EXPORT int MM_enf_shipcount(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!lm)
			return MM_FAIL;

		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!cfg)
		{
			lm->Log(L_ERROR, "<enf_shipcount> unable to get cfg interface %s", I_CONFIG);
			mm->ReleaseInterface(lm);
			return MM_FAIL;
		}

		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!pd)
		{
			lm->Log(L_ERROR, "<enf_shipcount> unable to get pd interface %s", I_PLAYERDATA);
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cfg);
			return MM_FAIL;
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegAdviser(&enforceradv, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregAdviser(&enforceradv, arena);

		return MM_OK;
	}

	return MM_FAIL;
}

