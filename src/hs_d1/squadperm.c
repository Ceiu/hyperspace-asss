
/* dist: public */

#include "asss.h"
#include <string.h>

/* callbacks */
local void MyPA(Player *p, int action, Arena *arena);

/* local data */
local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icapman *capman;
local Ilogman *lm;

local int HasPermission(Player *p, Arena *arena)
{
	if (arena && arena->status == ARENA_RUNNING)
	{
		ConfigHandle c = arena->cfg;
		/* cfghelp: General:NeedSquad, arena, string, mod: squadperm
		 * If this setting is present for an arena, any player entering
		 * the arena must be on the squad specified this setting.
		 * This can be used to restrict arenas to certain groups of
		 * players. */
		const char *capname = cfg->GetStr(c, "General", "NeedSquad");
		return capname ? (strcasecmp(capname, p->squad) == 0) : 1;
	}
	else
		return 0;
}


local void MyPA(Player *p, int action, Arena *arena)
{
	if (action == PA_PREENTERARENA)
	{
		if (! HasPermission(p, arena))
		{
			/* try to find a place for him */
			Arena *a = NULL;
			Link *link;

			aman->Lock();
			FOR_EACH_ARENA(a)
				if (HasPermission(p, a))
					break;
			aman->Unlock();

			if (!link || !a)
			{
				if (lm) lm->Log(L_WARN, "<squadperm> [%s] can't find any unrestricted arena!",
						p->name);
			}
			else
			{
				p->arena = a; /* redirect him to new arena! */
				chat->SendMessage(p, "You don't have permission to enter arena %s.",
						arena->name);
				if (lm) lm->Log(L_INFO, "<squadperm> [%s] redirected from arena {%s} to {%s}",
						p->name, arena->name, a->name);
			}
		}
	}
}

EXPORT int MM_squadperm(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		mm->RegCallback(CB_PLAYERACTION, MyPA, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, MyPA, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}
