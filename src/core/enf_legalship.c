
/* dist: public */

#include <stdio.h>

#include "asss.h"

local Iconfig *cfg;


local shipmask_t GetAllowableShips(Player *p, int freq, char *err_buf, int buf_len)
{
	char keyBuffer[32];

	shipmask_t arenaMask, freqMask;
	/* cfghelp: Legalship:ArenaMask, arena, int, range: 0-255, def: 255, mod: enf_legalship
	 * The ship mask of allowed ships in the arena. 1=warbird, 2=javelin, etc. */
	arenaMask = cfg->GetInt(p->arena->cfg, "Legalship", "ArenaMask", 255);

	snprintf(keyBuffer, sizeof(keyBuffer), "Freq%iMask", freq);
	/* cfghelp: Legalship:Freq0Mask, arena, int, range: 0-255, def: 255, mod: enf_legalship
	 * The ship mask allowed on freq 0. Ships must also be allowed on the arena mask. You can define a mask for any freq (FreqXMask). */
	/* cfghelp: Legalship:Freq1Mask, arena, int, range: 0-255, def: 255, mod: enf_legalship
	 * The ship mask allowed on freq 1. Ships must also be allowed on the arena mask. You can define a mask for any freq (FreqXMask). */
	freqMask = cfg->GetInt(p->arena->cfg, "Legalship", keyBuffer, 255);

	return arenaMask & freqMask;
}

local int CanChangeToShip(Player *p, int new_ship, int is_changing, char *err_buf, int buf_len)
{
	shipmask_t playerMask = GetAllowableShips(p, p->p_freq, NULL, 0);
	int allow = !SHIPMASK_HAS(new_ship, playerMask);

	if (err_buf && !allow)
	{
		StringBuffer shipsDescriptor;

		SBInit(&shipsDescriptor);

		if (playerMask != SHIPMASK_NONE)
		{
			int i;
			for (i = 0; i < SHIP_SPEC; ++i)
			{
				if (SHIPMASK_HAS(i, playerMask))
				{
					SBPrintf(&shipsDescriptor, ", %i", i+1);
				}
			}
		}

		if (playerMask == SHIPMASK_NONE)
			snprintf(err_buf, buf_len, "You may not leave spectator mode on this frequency.");
		else
			snprintf(err_buf, buf_len, "Your allowed ships on this frequency are: %s", SBText(&shipsDescriptor, 2));

		SBDestroy(&shipsDescriptor);
	}

	return allow;
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

EXPORT const char info_enf_legalship[] = CORE_MOD_INFO("enf_legalship");

EXPORT int MM_enf_legalship(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!cfg)
			return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(cfg);

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

