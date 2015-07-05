#include <stdlib.h>

#include "asss.h"
#include "hscore.h"
#include "selfpos.h"

struct adata
{
	int on;

	LinkedList *sectionList;
	LinkedList *warpSectionList;
};

struct Section
{
	int x1;
	int y1;
	int x2;
	int y2;
};

struct pdata
{
	int x, y, xspeed, yspeed;

	int returning;
	ticks_t timeout;

	struct Section *section;
};

struct WarpSection
{
	int in_x1;
	int in_y1;
	int in_x2;
	int in_y2;

	int out_x1;
	int out_y1;
	int out_x2;
	int out_y2;
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

local int pdkey;
local int adkey;

local void LoadData(Arena *arena)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	int i;
	
	ad->sectionList = LLAlloc();
	ad->warpSectionList = LLAlloc();
	
	for (i = 0; i < 8; i++)
	{
		struct WarpSection *warpSection = amalloc(sizeof(*warpSection));
		struct Section *section = amalloc(sizeof(*section));
		
		if (i < 4)
		{
			warpSection->in_x1 = i * 12 + 454;
			warpSection->in_y1 = 510;
			warpSection->in_x2 = i * 12 + 461;
			warpSection->in_y2 = 517;
		}
		else
		{
			warpSection->in_x1 = (i - 4) * 12 + 526;
			warpSection->in_y1 = 510;
			warpSection->in_x2 = (i - 4) * 12 + 533;
			warpSection->in_y2 = 517;
		}
		
		warpSection->out_x1 = section->x1 = i * 60 + 272;
		warpSection->out_y1 = section->y1 = 258;
		warpSection->out_x2 = section->x2 = i * 60 + 331;
		warpSection->out_y2 = section->y2 = 497;
		
		LLAdd(ad->sectionList, section);
		LLAdd(ad->warpSectionList, warpSection);
	}
}

local void UnloadData(Arena *arena)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);

	LLEnum(ad->sectionList, afree);
	LLEnum(ad->warpSectionList, afree);

	LLFree(ad->sectionList);
	LLFree(ad->warpSectionList);
}

local struct Section *getContainingSection(Arena *arena, int x, int y)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	
	for (link = LLGetHead(ad->sectionList); link; link = link->next)
	{
		struct Section *section = link->data;
		if (section->x1 <= x && x <= section->x2)
		{
			if (section->y1 <= y && y <= section->y2)
			{
				return section;
			}
		}
	}
	
	return NULL;
}

local struct WarpSection *getContainingWarpSection(Arena *arena, int x, int y)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	
	for (link = LLGetHead(ad->warpSectionList); link; link = link->next)
	{
		struct WarpSection *warpSection = link->data;
		if (warpSection->in_x1 <= x && x <= warpSection->in_x2)
		{
			if (warpSection->in_y1 <= y && y <= warpSection->in_y2)
			{
				return warpSection;
			}
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
	struct Section *section;
	struct WarpSection *warpSection;

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
	{
		data->section = NULL;
		return;
	}


	section = getContainingSection(arena, pos->x >> 4, pos->y >> 4);
	
	if (section == data->section)
	{
		//they're in their current section
		data->returning = 0;
		
		//update position
		data->x = pos->x;
		data->y = pos->y;	
		data->xspeed = pos->xspeed;
		data->yspeed = pos->yspeed;	
	}
	else if (section == NULL)
	{
		//they left the field. let them
		data->section = NULL;
		data->returning = 0;
	}
	else
	{
		if (data->section == NULL)
		{
			data->section = section;
		}
		else
		{
			if (!data->returning || (data->returning && data->timeout < current_ticks()))
			{
				//they left, and haven't been handled yet.

				selfpos->WarpPlayer(p, data->x, data->y, -data->xspeed, data->yspeed, p->position.rotation, 0);

				data->returning = 1;
				data->timeout = current_ticks() + 50;
			}
		}
	}
	
	warpSection = getContainingWarpSection(arena, pos->x >> 4, pos->y >> 4);
	if (warpSection != NULL)
	{
		//warp them!
		int x = (rand() % (warpSection->out_x2 - warpSection->out_x1)) + warpSection->out_x1;
		int y = (rand() % (warpSection->out_y2 - warpSection->out_y1)) + warpSection->out_y1;
		selfpos->WarpPlayer(p, x << 4, y << 4, 0, 0, p->position.rotation, 0);
	}
}

EXPORT int MM_hs_foosball(int action, Imodman *_mm, Arena *arena)
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

		if (!lm || !net || !mapdata || !pd || !aman || !game || !selfpos) return MM_FAIL;

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

