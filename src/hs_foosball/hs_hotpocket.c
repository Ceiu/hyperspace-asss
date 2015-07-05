#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "selfpos.h"

struct adata
{
	int on;

	int usex;

	LinkedList *regions;
};

struct pdata
{
	int x, y, xspeed, yspeed;

	int returning;
	ticks_t timeout;

	Region *region;
};

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Imapdata *mapdata;
local Iplayerdata *pd;
local Iarenaman *aman;
local Igame *game;
local Iselfpos *selfpos;
local Iconfig *cfg;

local int pdkey;
local int adkey;

local void LoadData(Arena *arena)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	int i;
	int count;
	char region_name[32];
	const char *base_name;
	
	ad->regions = LLAlloc();
	
	ad->usex = cfg->GetInt(arena->cfg, "Hotpocket", "UseX", 1);
	count = cfg->GetInt(arena->cfg, "Hotpocket", "RegionCount", 0);
	base_name = cfg->GetStr(arena->cfg, "Hotpocket", "RegionName");

	if (base_name && *base_name)
	{
		for (i = 0; i < count; i++)
		{
			Region *region;

			snprintf(region_name, sizeof(region_name), "%s%d", base_name, i);
			region = mapdata->FindRegionByName(arena, region_name);
			if (region)
			{
				LLAdd(ad->regions, region);
			}
			else
			{
				lm->LogA(L_WARN, "hs_hotpocket", arena, "Couldn't find region %s", region_name);
			}
		}
	}
	else
	{
		lm->LogA(L_WARN, "hs_hotpocket", arena, "No base name for regions. Check config.");
	}
}

local void UnloadData(Arena *arena)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);

	LLFree(ad->regions);
}

local Region *getContainingRegion(Arena *arena, int x, int y)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	
	for (link = LLGetHead(ad->regions); link; link = link->next)
	{
		Region *region = link->data;
		if (mapdata->Contains(region, x, y))
		{	
			return region;
		}
	}
	
	return NULL;
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	struct pdata *data = PPDATA(p, pdkey);
	Region *region;

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
	{
		data->region = NULL;
		return;
	}


	region = getContainingRegion(arena, pos->x >> 4, pos->y >> 4);
	
	if (region == data->region)
	{
		//they're in their current section
		data->returning = 0;
		
		//update position
		data->x = pos->x;
		data->y = pos->y;	
		data->xspeed = pos->xspeed;
		data->yspeed = pos->yspeed;	
	}
	else if (region == NULL)
	{
		//they left the field. let them
		data->region = NULL;
		data->returning = 0;
	}
	else
	{
		if (data->region == NULL)
		{
			data->region = region;
		}
		else
		{
			if (!data->returning || (data->returning && data->timeout < current_ticks()))
			{
				//they left, and haven't been handled yet.
				int newxspeed = data->xspeed;
				int newyspeed = data->yspeed;

				if (ad->usex)
				{
					newxspeed = -newxspeed;
				}
				else
				{
					newyspeed = -newyspeed;
				}

				selfpos->WarpPlayer(p, data->x, data->y, newxspeed, newyspeed, p->position.rotation, 0);

				data->returning = 1;
				data->timeout = current_ticks() + 50;
			}
		}
	}
}

EXPORT int MM_hs_hotpocket(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!lm || !net || !mapdata || !pd || !aman || !game || !selfpos || !cfg) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(struct pdata));
		if (pdkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(cfg);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 1;

		LoadData(arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 0;

		UnloadData(arena);

		return MM_OK;
	}
	return MM_FAIL;
}

