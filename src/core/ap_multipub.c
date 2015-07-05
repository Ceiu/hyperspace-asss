
/* dist: public */

#include <stdio.h>

#include "asss.h"


local Iarenaman *aman;
local Iconfig *cfg;

local LinkedList pubnames;


local void load_pubnames(void)
{
	const char *pn, *temp = NULL;
	char buf[20];

	LLInit(&pubnames);
	/* cfghelp: General:PublicArenas, global, string, mod: ap_multipub
	 * A list of public arena types that the server will place people in
	 * when they don't request a specific arena. */
	pn = cfg->GetStr(GLOBAL, "General", "PublicArenas");
	while (strsplit(pn, " ,:;", buf, sizeof(buf), &temp))
		LLAdd(&pubnames, astrdup(buf));
}


local int Place(char *retname, int namelen, int *x, int *y, Player *pp)
{
	LinkedList *trylist;
	Link *l;
	char buf[20];
	int pass;

	/* if the player connected through an ip/port that specified a
	 * connectas field, then try just that arena */
	Link deflink = { NULL, (void*)pp->connectas };
	LinkedList deflist = { &deflink, &deflink };

	if (pp->connectas)
		trylist = &deflist;
	else
		trylist = &pubnames;

	/* clear this buffer, use it for an empty arena as a backup choice */
	*retname = 0;

	/* if we don't find anything in 9 passes (unlikely), just do the
	 * default action */
	for (pass = 1; pass < 10; pass++)
		for (l = LLGetHead(trylist); l; l = l->next)
		{
			const char *name = l->data;
			int total, playing, des;
			Arena *arena;

			snprintf(buf, sizeof(buf), "%s%d", name, pass);
			arena = aman->FindArena(buf, &total, &playing);
			if (!arena)
			{
				/* doesn't exist yet, use a backup only */
				if (!*retname)
					astrncpy(retname, buf, namelen);
			}
			else
			{
				ConfigHandle ch = arena->cfg;
				/* cfghelp: General:DesiredPlaying, arena, int, def: 15, \
				 * mod: ap_multipub
				 * This controls when the server will create new public
				 * arenas. */
				des = cfg->GetInt(ch, "General", "DesiredPlaying", 15);
				if (playing < des)
				{
					/* we have fewer playing than we want, dump here */
					astrncpy(retname, buf, namelen);
					return TRUE;
				}
			}
		}

	return *retname;
}


local Iarenaplace myint =
{
	INTERFACE_HEAD_INIT(I_ARENAPLACE, "ap-multipub")
	Place
};

EXPORT const char info_ap_multipub[] = CORE_MOD_INFO("ap_multipub");

EXPORT int MM_ap_multipub(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		if (!aman || !cfg) return MM_FAIL;

		load_pubnames();

		mm->RegInterface(&myint, arena);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myint, arena))
			return MM_FAIL;
		LLEnum(&pubnames, afree);
		LLEmpty(&pubnames);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	return MM_FAIL;
}

