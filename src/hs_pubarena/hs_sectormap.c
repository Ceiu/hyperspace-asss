#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"

typedef struct Sector
{
	Region *region;
	int has_flag;
	int old_has_flag;
	int number;
} Sector;

typedef struct adata
{
	LinkedList sectors;
	int max_sector;
} adata;

typedef struct pdata
{
	int dummy;
} pdata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Imapdata *mapdata;
local Ichat *chat;
local Iarenaman *aman;
local Icmdman *cmdman;
local Iconfig *cfg;
local Iplayerdata *pd;
local Iobjects *obj;
local Iflagcore *flagcore;
local Imainloop *ml;

local int adkey;
local int pdkey;

local void flash_sector(Arena *arena, Sector *sector, LinkedList *list)
{
	int id = cfg->GetInt(arena->cfg, "Hyperspace", "SectorMapFlashBase", 110) + sector->number;
	Target target;
	Player *p;
	Link *link;

	target.type = T_PLAYER;

	FOR_EACH(list, p, link)
	{
		target.u.p = p;
		obj->Toggle(&target, id, 1);
	}
}

local void turn_on_sector(Arena *arena, Sector *sector)
{
	int id = cfg->GetInt(arena->cfg, "Hyperspace", "SectorMapImageBase", 100) + sector->number;
	Target target;
	target.type = T_ARENA;
	target.u.arena = arena;

	obj->Toggle(&target, id, 1);
}

local void turn_off_sector(Arena *arena, Sector *sector)
{
	int id = cfg->GetInt(arena->cfg, "Hyperspace", "SectorMapImageBase", 100) + sector->number;
	Target target;
	target.type = T_ARENA;
	target.u.arena = arena;

	obj->Toggle(&target, id, 0);
}

local Sector * get_sector(Arena *arena, int x, int y)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	Sector *sector;
	Link *link;

	FOR_EACH(&ad->sectors, sector, link)
	{
		if (mapdata->Contains(sector->region, x, y))
		{
			return sector;
		}
	}

	return NULL;
}

local Sector * get_sector_by_number(Arena *arena, int sector_number)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	Sector *sector;
	Link *link;

	FOR_EACH(&ad->sectors, sector, link)
	{
		if (sector->number == sector_number)
		{
			return sector;
		}
	}

	return NULL;
}

local helptext_t target_help =
"Targets: team\n"
"Args: [message]\n"
"Flashes the sector map for your team.\n";

local void Ctarget(const char *command, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (IS_RESTRICTED(chat->GetPlayerChatMask(p), MSG_FREQ))
		return;

	if (target->type == T_FREQ && target->u.freq.freq == p->p_freq)
	{
		Sector *sector = NULL;

		if (params && *params)
		{
			char *next;
			int sector_number = (int)strtol(params, &next, 0);
			if (next != params)
			{
				if (sector_number < 0 || ad->max_sector < sector_number)
				{
					chat->SendMessage(p, "Bad sector number!");
					return;
				}
				sector = get_sector_by_number(arena, sector_number);
				params = next;
				if (!sector)
				{
					chat->SendMessage(p, "Could not find sector %d!", sector_number);
					return;
				}

				/* remove all of the leading spaces */
				if (params)
				{
					while (*params == ' ')
						params++;
				}
			}
		}

		if (sector == NULL)
		{
			sector = get_sector(arena, p->position.x >> 4, p->position.y >> 4);
		}

		if (sector)
		{
			char target_message[30];
			LinkedList list;

			LLInit(&list);
			pd->TargetToSet(target, &list);

			if (sector->number == 0)
			{
				sprintf(target_message, "<<Target: Center>>");
			}
			else
			{
				sprintf(target_message, "<<Target: Sector%d>>", sector->number);
			}

			if (params && *params)
			{
				chat->SendAnyMessage(&list, MSG_FREQ, 0, p, "%s %s", target_message, params);
			}
			else
			{
				chat->SendAnyMessage(&list, MSG_FREQ, 0, p, "%s", target_message);
			}

			flash_sector(arena, sector, &list);

			LLEmpty(&list);
		}
		else
		{
			chat->SendMessage(p, "You are not in a sector!");
		}
	}
}

local int update_flags_timer(void *clos)
{
	Arena *arena = clos;
	adata *ad = P_ARENA_DATA(arena, adkey);
	FlagInfo flags[30];
	int i;
	int flagcount;
	Sector *sector;
	Link *link;

	FOR_EACH(&ad->sectors, sector, link)
	{
		sector->old_has_flag = sector->has_flag;
		sector->has_flag = 0;
	}

	flagcount = flagcore->GetFlags(arena, 0, flags, 30);

	for (i = 0; i < flagcount; i++)
	{
		int x = -1;
		int y = -1;

		// find where the flag is
		switch (flags[i].state)
		{
			case FI_NONE:
				break;
			case FI_ONMAP:
				x = flags[i].x;
				y = flags[i].y;
				break;
			case FI_CARRIED:
				if (flags[i].carrier)
				{
					x = flags[i].carrier->position.x >> 4;
					y = flags[i].carrier->position.y >> 4;
				}
				break;
		}

		// mark the sector as holding a flag
		sector = get_sector(arena, x, y);
		if (sector)
		{
			sector->has_flag = 1;
		}
	}

	FOR_EACH(&ad->sectors, sector, link)
	{
		if (sector->old_has_flag == 1 && sector->has_flag == 0)
		{
			turn_off_sector(arena, sector);
		}
		else if (sector->old_has_flag == 0 && sector->has_flag == 1)
		{
			turn_on_sector(arena, sector);
		}
	}

	return TRUE;
}

local void init_regions(Arena *arena)
{
	char name[80];
	adata *ad = P_ARENA_DATA(arena, adkey);
	int i = 0;
	int delay;

	delay = cfg->GetInt(arena->cfg, "Hyperspace", "SectorMapUpdateDelay", 300);

	LLInit(&ad->sectors);

	while (1)
	{
		Sector *sector;
		Region *region;

		sprintf(name, "sector%d", i);

		region = mapdata->FindRegionByName(arena, name);
		if (!region)
		{
			break;
		}

		sector = amalloc(sizeof(*sector));
		sector->region = region;
		sector->has_flag = 0;
		sector->old_has_flag = 0;
		sector->number = i;
		LLAdd(&ad->sectors, sector);

		i++;
	}

	ad->max_sector = i - 1;

	ml->SetTimer(update_flags_timer, 0, delay, arena, arena);
}

local void deinit_regions(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	Sector *sector;
	Link *link;

	ml->ClearTimer(update_flags_timer, arena);

	FOR_EACH(&ad->sectors, sector, link)
	{
		if (sector->has_flag)
		{
			turn_off_sector(arena, sector);
		}
		afree(sector);
	}

	LLEmpty(&ad->sectors);
}

EXPORT const char info_hs_sectormap[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_sectormap(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		obj = mm->GetInterface(I_OBJECTS, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!lm || !mapdata || !chat || !aman || !cmdman || !cfg || !pd || !obj || !flagcore || !ml) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmdman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(obj);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(ml);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		init_regions(arena);

		cmdman->AddCommand("target", Ctarget, arena, target_help);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		cmdman->RemoveCommand("target", Ctarget, arena);

		deinit_regions(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
