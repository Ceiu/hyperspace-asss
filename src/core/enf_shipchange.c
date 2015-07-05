
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"

typedef struct pdata
{
	ticks_t last_change;
} pdata;

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Igame *game;
local Iflagcore *flagcore;

local int pdkey;

local int CanChangeToShip(Player *p, int new_ship, int is_changing, char *err_buf, int buf_len)
{
	pdata *data = PPDATA(p, pdkey);
	int shipchangeinterval, antiwarp_non_flagger, antiwarp_flagger;

	/* cfghelp: Misc:ShipChangeInterval, arena, int, def: 500
	 * The allowable interval between player ship changes, in ticks. */
	shipchangeinterval = cfg->GetInt(p->arena->cfg, "Misc", "ShipChangeInterval", 500);

	/* cfghelp: Misc:AntiwarpShipChange, arena, int, def, 0
	 * prevents players without flags from changing ships
	 * while antiwarped. */
	antiwarp_non_flagger = cfg->GetInt(p->arena->cfg, "Misc", "AntiwarpShipChange", 0);

	/* cfghelp: Misc:AntiwarpFlagShipChange, arena, int, def, 0
	 * prevents players with flags from changing ships
	 * while antiwarped. */
	antiwarp_flagger = cfg->GetInt(p->arena->cfg, "Misc", "AntiwarpFlagShipChange", 0);

	if (shipchangeinterval > 0 && data->last_change + shipchangeinterval > current_ticks())
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "You've changed ships too recently. Please wait.");

		return 0;
	}

	if (p->p_ship != SHIP_SPEC
			&& (antiwarp_non_flagger || antiwarp_flagger)
			&& game->IsAntiwarped(p, NULL))
	{
		int flags;

		if (flagcore)
		{
			flags = flagcore->CountPlayerFlags(p);
		}
		else
		{
			flags = 0;
		}

		if ((flags && antiwarp_flagger) || (!flags && antiwarp_non_flagger))
		{
			if (err_buf)
				snprintf(err_buf, buf_len, "You are antiwarped!");

			return 0;
		}
	}

	return 1;
}

local void ship_change_cb(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	pdata *data = PPDATA(p, pdkey);
	if (newship != oldship && newship != SHIP_SPEC)
	{
		data->last_change = current_ticks();
	}
}

local Aenforcer myadv =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
	NULL,
	NULL,
	CanChangeToShip,
	NULL,
	NULL,
};

EXPORT const char info_enf_shipchange[] = CORE_MOD_INFO("enf_shipchange");

EXPORT int MM_enf_shipchange(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);

		if (!lm || !cfg || !pd || !game) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(flagcore);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegAdviser(&myadv, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, ship_change_cb, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_SHIPFREQCHANGE, ship_change_cb, arena);
		mm->UnregAdviser(&myadv, arena);

		return MM_OK;
	}
	return MM_FAIL;
}

