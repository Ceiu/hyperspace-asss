
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "asss.h"

#define MAXMSGS 10


typedef struct periodic_msgs
{
	Arena *arena;
	int count;
	struct
	{
		const char *msg;
		int initialdelay, interval;
	} msgs[MAXMSGS];
} periodic_msgs;


local Iconfig *cfg;
local Iplayerdata *pd;
local Iarenaman *aman;
local Ichat *chat;
local Imainloop *ml;


local int msg_timer(void *v)
{
	int i;
	periodic_msgs *pm = (periodic_msgs*)v;

	pm->count++;
	for (i = 0; i < MAXMSGS; i++)
		if (pm->msgs[i].msg && pm->msgs[i].interval > 0)
		{
			int diff = pm->count - pm->msgs[i].initialdelay;
			if (diff >= 0 && (diff % pm->msgs[i].interval) == 0)
				chat->SendArenaMessage(pm->arena, "%s", pm->msgs[i].msg);
		}
	return TRUE;
}

local void msg_cleanup(void *v)
{
	int i;
	periodic_msgs *pm = (periodic_msgs*)v;

	for (i = 0; i < MAXMSGS; i++)
		afree(pm->msgs[i].msg);
	afree(pm);
}


/* handles only greetmessages */
local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
	{
		Arena *arena = p->arena;
		ConfigHandle ch = arena ? arena->cfg : NULL;
		/* cfghelp: Misc:GreetMessage, arena, string
		 * The message to send to each player on entering the arena. */
		const char *msg = ch ? cfg->GetStr(ch, "Misc", "GreetMessage") : NULL;

		if (msg)
			chat->SendMessage(p, "%s", msg);
	}
}


/* starts timer to handle periodmessages */
local void aaction(Arena *arena, int action)
{
	if (action == AA_DESTROY || action == AA_CONFCHANGED)
	{
		ml->CleanupTimer(msg_timer, arena, msg_cleanup);
	}

	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		int i, c = 0;
		periodic_msgs *pm;

		pm = amalloc(sizeof(*pm));
		pm->count = 0;
		pm->arena = arena;

		for (i = 0; i < MAXMSGS; i++)
		{
			char key[32];
			const char *v;

			pm->msgs[i].msg = NULL;
			pm->msgs[i].interval = 0;

			snprintf(key, sizeof(key), "PeriodicMessage%d", i);

			v = cfg->GetStr(arena->cfg, "Misc", key);

			if (v)
			{
				char *next;
				int interval = strtol(v, &next, 0);
				if (next)
				{
					int initialdelay = strtol(next, &next, 0);
					if (next)
					{
						/* skip spaces */
						while (*next && isspace(*next)) next++;
						if (*next)
						{
							pm->msgs[i].initialdelay = initialdelay;
							pm->msgs[i].interval = interval;
							pm->msgs[i].msg = astrdup(next);
							c++;
						}
					}
				}
			}
		}

		if (c)
			ml->SetTimer(msg_timer, 6000, 6000, pm, arena);
		else
			afree(pm);
	}
}

EXPORT const char info_messages[] = CORE_MOD_INFO("messages");

EXPORT int MM_messages(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!cfg || !aman || !pd || !chat || !ml) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);

		ml->CleanupTimer(msg_timer, NULL, msg_cleanup);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(ml);

		return MM_OK;
	}
	return MM_FAIL;
}

