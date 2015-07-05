
/* dist: public */

#include <string.h>
#include <stdio.h>

#include "asss.h"

#include "db_layout.h"


#define DEFAULT "default"

typedef struct
{
	char group[MAXGROUPLEN];
	enum
	{
		src_default,
		src_global,
		src_arena,
#ifdef CFG_USE_ARENA_STAFF_LIST
		src_arenalist,
#endif
		src_temp
	} source;
} pdata;

/* data */
local ConfigHandle groupdef, staff_conf;
local int pdkey;

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Ilogman *lm;
local Iconfig *cfg;


local void update_group(Player *p, pdata *pdata, Arena *arena, int log)
{
	const char *g;

	if (!p->flags.authenticated)
	{
		/* if the player hasn't been authenticated against either the
		 * biller or password file, don't assign groups based on name. */
		astrncpy(pdata->group, DEFAULT, MAXGROUPLEN);
		pdata->source = src_default;
		return;
	}

	if (arena && (g = cfg->GetStr(staff_conf, arena->basename, p->name)))
	{
		astrncpy(pdata->group, g, MAXGROUPLEN);
		pdata->source = src_arena;
		if (log)
			lm->LogP(L_DRIVEL, "capman", p, "assigned to group '%s' (arena)", pdata->group);
	}
#ifdef CFG_USE_ARENA_STAFF_LIST
	else if (arena && arena->cfg && (g = cfg->GetStr(arena->cfg, "Staff", p->name)))
	{
		astrncpy(pdata->group, g, MAXGROUPLEN);
		pdata->source = src_arenalist;
		if (log)
			lm->LogP(L_DRIVEL, "capman", p, "assigned to group '%s' (arenaconf)", pdata->group);
	}
#endif
	else if ((g = cfg->GetStr(staff_conf, AG_GLOBAL, p->name)))
	{
		/* only global groups available for now */
		astrncpy(pdata->group, g, MAXGROUPLEN);
		pdata->source = src_global;
		if (log)
			lm->LogP(L_DRIVEL, "capman", p, "assigned to group '%s' (global)", pdata->group);
	}
	else
	{
		astrncpy(pdata->group, DEFAULT, MAXGROUPLEN);
		pdata->source = src_default;
	}
}


local void paction(Player *p, int action, Arena *arena)
{
	pdata *pdata = PPDATA(p, pdkey);
	if (action == PA_PREENTERARENA)
		update_group(p, pdata, arena, TRUE);
	else if (action == PA_CONNECT)
		update_group(p, pdata, NULL, TRUE);
	else if (action == PA_DISCONNECT || action == PA_LEAVEARENA)
		astrncpy(pdata->group, "none", MAXGROUPLEN);
}

local void new_player(Player *p, int isnew)
{
	char *group = ((pdata*)PPDATA(p, pdkey))->group;
	if (isnew)
		astrncpy(group, "none", MAXGROUPLEN);
}


local const char *GetGroup(Player *p)
{
	return ((pdata*)PPDATA(p, pdkey))->group;
}


local void SetTempGroup(Player *p, const char *newgroup)
{
	pdata *pdata = PPDATA(p, pdkey);
	if (newgroup)
	{
		astrncpy(pdata->group, newgroup, MAXGROUPLEN);
		pdata->source = src_temp;
	}
}


local void SetPermGroup(Player *p, const char *group, int global, const char *info)
{
	pdata *pdata = PPDATA(p, pdkey);

	/* first set it for the current session */
	astrncpy(pdata->group, group, MAXGROUPLEN);

	/* now set it permanently */
	if (global)
	{
		cfg->SetStr(staff_conf, AG_GLOBAL, p->name, group, info, TRUE);
		pdata->source = src_global;
	}
	else if (p->arena)
	{
		cfg->SetStr(staff_conf, p->arena->basename, p->name, group, info, TRUE);
		pdata->source = src_arena;
	}
}


local void RemoveGroup(Player *p, const char *info)
{
	pdata *pdata = PPDATA(p, pdkey);

	if (!p->arena)
		return;

	/* in all cases, set current group to default */
	astrncpy(pdata->group, DEFAULT, MAXGROUPLEN);

	switch (pdata->source)
	{
		case src_default:
			/* player is in the default group already, nothing to do */
			break;

		case src_global:
			cfg->SetStr(staff_conf, AG_GLOBAL, p->name, DEFAULT, info, TRUE);
			break;

		case src_arena:
			cfg->SetStr(staff_conf, p->arena->basename, p->name, DEFAULT, info, TRUE);
			break;

#ifdef CFG_USE_ARENA_STAFF_LIST
		case src_arenalist:
			cfg->SetStr(p->arena->cfg, "Staff", p->name, DEFAULT, info);
			break;
#endif

		case src_temp:
			break;
	}
}


local int CheckGroupPassword(const char *group, const char *pw)
{
	const char *correctpw =
		cfg->GetStr(staff_conf, "GroupPasswords", group);
	return correctpw ? (strcmp(correctpw, pw) == 0) : 0;
}


local int HasCapability(Player *p, const char *cap)
{
	const char *group = ((pdata*)PPDATA(p, pdkey))->group;
	return cfg->GetStr(groupdef, group, cap) != NULL;
}


local int HasCapabilityInArena(Player *p, Arena *a, const char *cap)
{
	pdata tmp_pdata;
	update_group(p, &tmp_pdata, a, FALSE);
	return cfg->GetStr(groupdef, tmp_pdata.group, cap) != NULL;
}


local int HasCapabilityByName(const char *name, const char *cap)
{
	/* figure out his group */
	const char *group = cfg->GetStr(staff_conf, AG_GLOBAL, name);
	if (!group)
		group = DEFAULT;
	return cfg->GetStr(groupdef, group, cap) != NULL;
}


local int HigherThan(Player *a, Player *b)
{
	char cap[MAXGROUPLEN+16];
	const char *bgrp = ((pdata*)PPDATA(b, pdkey))->group;
	snprintf(cap, sizeof(cap), "higher_than_%s", bgrp);
	return HasCapability(a, cap);
}


/* interface */

local Icapman capint =
{
	INTERFACE_HEAD_INIT(I_CAPMAN, "capman-groups")
	HasCapability, HasCapabilityByName, HasCapabilityInArena, HigherThan
};

local Igroupman grpint =
{
	INTERFACE_HEAD_INIT(I_GROUPMAN, "groupman")
	GetGroup, SetPermGroup, SetTempGroup,
	RemoveGroup,
	CheckGroupPassword
};

EXPORT const char info_capman[] = CORE_MOD_INFO("capman");

EXPORT int MM_capman(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!pd || !aman || !lm || !cfg) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_NEWPLAYER, new_player, ALLARENAS);

		groupdef = cfg->OpenConfigFile(NULL, "groupdef.conf", NULL, NULL);
		staff_conf = cfg->OpenConfigFile(NULL, "staff.conf", NULL, NULL);

		mm->RegInterface(&capint, ALLARENAS);
		mm->RegInterface(&grpint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&capint, ALLARENAS))
			return MM_FAIL;
		if (mm->UnregInterface(&grpint, ALLARENAS))
			return MM_FAIL;
		cfg->CloseConfigFile(groupdef);
		cfg->CloseConfigFile(staff_conf);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->UnregCallback(CB_NEWPLAYER, new_player, ALLARENAS);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


