
/* dist: public */

#include "asss.h"

struct autowarp_chunk
{
	i16 x;
	i16 y;
	char arena[16];
};

local Iarenaman *aman;
local Igame *game;
local Imapdata *mapdata;


local void region_cb(Player *p, Region *rgn, int x, int y, int entering)
{
	const struct autowarp_chunk *aw;
	int awlen;

	if (!entering)
		return;

	if (!mapdata->RegionChunk(rgn, MAKE_CHUNK_TYPE(rAWP), (const void **)&aw, &awlen))
		return;

	if (awlen == 4 || (awlen == 20 && aw->arena[0] == '\0'))
	{
		/* in-arena warp */
		Target t;
		t.type = T_PLAYER;
		t.u.p = p;
		game->WarpTo(&t, aw->x < 0 ? x : aw->x, aw->y < 0 ? y : aw->y);
	}
	else if (awlen == 20)
	{
		/* cross-arena warp */
		aman->SendToArena(p, aw->arena, aw->x < 0 ? x : aw->x, aw->y < 0 ? y : aw->y);
	}
}

EXPORT const char info_autowarp[] = CORE_MOD_INFO("autowarp");

EXPORT int MM_autowarp(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		if (!aman || !game || !mapdata) return MM_FAIL;
		mm->RegCallback(CB_REGION, region_cb, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_REGION, region_cb, ALLARENAS);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(mapdata);
		return MM_OK;
	}
	return MM_FAIL;
}

