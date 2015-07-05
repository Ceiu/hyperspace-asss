#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"

typedef struct item_entry
{
	Item *item;
	int object_id;
	int always_on;
	int zero_id;
	int max_id;
	int increment;
	char required_property[32];
} item_entry;

typedef struct adata
{
	int on;
	int count;
	item_entry *entries;
} adata;

typedef struct lvz_status
{
	int on;
	int id;
} lvz_status;

typedef struct pdata
{
	time_t last_update;
	lvz_status *status;
} pdata;

local Ilogman *lm;
local Iarenaman *aman;
local Ihscoreitems *items;
local Iconfig *cfg;
local Imainloop *ml;
local Iplayerdata *pd;
local Iobjects *objects;
local Ihscoredatabase *database;

local int pdkey;
local int adkey;

local void update_player(Player *p, item_entry *entry, lvz_status *status, int count)
{
	int new_on, new_id, delta;
	pdata *data = PPDATA(p, pdkey);

	Target target;
	target.type = T_PLAYER;
	target.u.p = p;

	delta = (count / entry->increment);

	if (entry->always_on || delta != 0)
	{
		new_on = 1;
		new_id = entry->zero_id + delta;

		if (new_id > entry->max_id)
		{
			new_id = entry->max_id;
		}
	}
	else
	{
		new_on = 0;
	}

	if (new_on)
	{
		if (new_id != status->id || !status->on)
		{
			objects->Image(&target, entry->object_id, new_id);
			data->last_update = current_ticks();
		}

		if (!status->on)
		{
			objects->Toggle(&target, entry->object_id, 1);
			data->last_update = current_ticks();
		}
	}
	else if (status->on == 1)
	{
		objects->Toggle(&target, entry->object_id, 0);
		data->last_update = current_ticks();
	}

	status->on = new_on;
	status->id = new_id;
}

local void init_player(Player *p)
{
	int i;
	pdata *data = PPDATA(p, pdkey);
	adata *adata = P_ARENA_DATA(p->arena, adkey);

	Target target;
	target.type = T_PLAYER;
	target.u.p = p;

	if (!adata->on)
		return;

	data->status = amalloc(sizeof(lvz_status) * adata->count);

	for (i = 0; i < adata->count; i++)
	{
		if (p->p_ship == SHIP_SPEC)
		{
			data->status[i].on = 0;
		}
		else
		{
			int sum = 1;
			int item_count = items->getItemCount(p, adata->entries[i].item, p->p_ship);

			// pretend it started as off
			data->status[i].on = 0;

			if (strlen(adata->entries[i].required_property) > 0)
			{
				sum = items->getPropertySum(p, p->p_ship, adata->entries[i].required_property, 0);
			}

			if (sum)
			{
				// do the updates
				update_player(p, &adata->entries[i], &data->status[i], item_count);
			}

			if (!data->status[i].on)
			{
				// if it's still off after update_player, make sure to set it as off
				objects->Toggle(&target, adata->entries[i].object_id, 0);
			}
		}
	}
}

local void deinit_player(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	adata *adata = P_ARENA_DATA(p->arena, adkey);
	int i;

	Target target;
	target.type = T_PLAYER;
	target.u.p = p;

	if (!adata->on)
		return;

	for (i = 0; i < adata->count; i++)
	{
		if (data->status[i].on)
		{
			// turn off the object
			objects->Toggle(&target, adata->entries[i].object_id, 0);
		}
	}

	afree(data->status);
}

local int load_arena_config(void *param)
{
	Arena *a = param;
	adata *adata = P_ARENA_DATA(a, adkey);
	int i;
	char buf[256];
	Player *j;
	Link *link;

	/* cfghelp: AmmoLVZ:Count, arena, int, def: 0, mod: hs_ammolvz
	 * The number of ammo lvz entries to load */
	adata->count = cfg->GetInt(a->cfg, "AmmoLVZ", "Count", 0);

	if (adata->count < 1)
		return FALSE;

	adata->entries = amalloc(sizeof(item_entry) * adata->count);

	for (i = 0; i < adata->count; i++)
	{
		Item *item;
		item_entry *entry;
		const char *item_name;
		const char *reqprop;

		/* cfghelp: AmmoLVZ:Ammo0Name, arena, string, mod: hs_ammolvz
		 * The name of the item to track */
		sprintf(buf, "Ammo%dName", i);
		item_name = cfg->GetStr(a->cfg, "AmmoLVZ", buf);

		if (item_name != NULL)
		{
			item = items->getItemByName(item_name, a);
			if (item == NULL)
			{
				lm->LogA(L_ERROR, "hs_ammolvz", a, "Unknown item %s! Trying again momentarily.", item_name);
				afree(adata->entries);
				return TRUE;
			}
		}
		else
		{
			lm->LogA(L_ERROR, "hs_ammolvz", a, "Unset config AmmoLVZ:%s!", buf);
			afree(adata->entries);
			return FALSE;
		}

		entry = &adata->entries[i];
		entry->item = item;

		/* cfghelp: AmmoLVZ:Ammo0Object, arena, int, def: 0, mod: hs_ammolvz
		 * The object id of the ammo lvz */
		sprintf(buf, "Ammo%dObject", i);
		entry->object_id = cfg->GetInt(a->cfg, "AmmoLVZ", buf, 0);

		/* cfghelp: AmmoLVZ:Ammo0ReqProp, arena, string, mod: hs_ammolvz
		 * A property sum needed to display the lvz */
		sprintf(buf, "Ammo%dReqProp", i);
		reqprop = cfg->GetStr(a->cfg, "AmmoLVZ", buf);
		entry->required_property[0] = '\0';
		if (reqprop != NULL)
		{
			if (strlen(reqprop) != 0)
			{
				astrncpy(entry->required_property, reqprop, 30);
			}
		}

		/* cfghelp: AmmoLVZ:Ammo0AlwaysShow, arena, int, def: 0, mod: hs_ammolvz
		 * If the lvz should be shown even on zero */
		sprintf(buf, "Ammo%dAlwaysShow", i);
		entry->always_on = cfg->GetInt(a->cfg, "AmmoLVZ", buf, 0);

		/* cfghelp: AmmoLVZ:Ammo0ZeroId, arena, int, def: 0, mod: hs_ammolvz
		 * The image id of the zero image */
		sprintf(buf, "Ammo%dZeroID", i);
		entry->zero_id = cfg->GetInt(a->cfg, "AmmoLVZ", buf, 0);

		/* cfghelp: AmmoLVZ:Ammo0MaxID, arena, int, def: 1, mod: hs_ammolvz
		 * The image id of the maximum image */
		sprintf(buf, "Ammo%dMaxID", i);
		entry->max_id = cfg->GetInt(a->cfg, "AmmoLVZ", buf, 1);

		/* cfghelp: AmmoLVZ:Ammo0Increment, arena, int, def: 1, mod: hs_ammolvz
		 * How many items represent a change
		 * in image id */
		sprintf(buf, "Ammo%dIncrement", i);
		entry->increment = cfg->GetInt(a->cfg, "AmmoLVZ", buf, 1);
	}

	adata->on = 1;

	//init all players present in arena
	pd->Lock();
	FOR_EACH_PLAYER(j)
	if (j->status == S_PLAYING && j->arena == a && IS_HUMAN(j))
	{
		init_player(j);
	}
	pd->Unlock();

	return FALSE;
}

local void unload_arena_config(Arena *a)
{
	adata *adata = P_ARENA_DATA(a, adkey);

	afree(adata->entries);
}

local void itemCountChangedCallback(Player *p, int ship, int shipset, Item *item, InventoryEntry *entry, int newCount, int oldCount) //called with lock held
{
	int i;
	pdata *data = PPDATA(p, pdkey);
	adata *adata = P_ARENA_DATA(p->arena, adkey);

	Target target;
	target.type = T_PLAYER;
	target.u.p = p;

	if (!adata->on)
		return;

	if (data->status == NULL) // probably a fake player
		return;

	if (data->last_update + 100 > current_ticks())
		return;

	// Make sure we're not updating if the player's actual ship didn't change.
	if (shipset != database->getPlayerShipSet(p))
		return;

	for (i = 0; i < adata->count; i++)
	{
		if (adata->entries[i].item == item)
		{
			int sum = 1;
			if (strlen(adata->entries[i].required_property) > 0)
			{
				sum = items->getPropertySum(p, p->p_ship, adata->entries[i].required_property, 0);
			}

			if (sum)
			{
				// do the updates
				update_player(p, &adata->entries[i], &data->status[i], newCount);
			}

			break;
		}
	}
}

local void shipFreqChangeCallback(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	int i;
	pdata *data = PPDATA(p, pdkey);
	adata *adata = P_ARENA_DATA(p->arena, adkey);

	Target target;
	target.type = T_PLAYER;
	target.u.p = p;

	if (!adata->on)
		return;

	if (data->last_update + 100 > current_ticks())
		return;

	if (newship != oldship)
	{
		for (i = 0; i < adata->count; i++)
		{
			if (newship == SHIP_SPEC)
			{
				if (data->status[i].on)
				{
					objects->Toggle(&target, adata->entries[i].object_id, 0);
					data->status[i].on = 0;
				}
			}
			else
			{
				int sum = 1;

				if (strlen(adata->entries[i].required_property) > 0)
				{
					sum = items->getPropertySum(p, p->p_ship, adata->entries[i].required_property, 0);
				}

				if (sum)
				{
					int item_count = items->getItemCount(p, adata->entries[i].item, p->p_ship);
					update_player(p, &adata->entries[i], &data->status[i], item_count);
				}
			}
		}
	}
}

local void playerActionCallback(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
	{
		init_player(p);
	}
	if (action == PA_LEAVEARENA)
	{
		deinit_player(p);
	}
}

EXPORT const char info_hs_ammolvz[] = "v1.1 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_ammolvz(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		objects = mm->GetInterface(I_OBJECTS, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);


		if (!lm || !aman || !items || !cfg || !ml || !pd || !objects || !database)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(aman);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(objects);
			mm->ReleaseInterface(database);

			return MM_FAIL;
		}

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(struct pdata));
		if (pdkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(objects);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		//Load config
		ml->SetTimer(load_arena_config, 50, 100, arena, arena);

		mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->RegCallback(CB_PLAYERACTION, playerActionCallback, arena);
		mm->RegCallback(CB_ITEM_COUNT_CHANGED, itemCountChangedCallback, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		Player *i;
		Link *link;
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ml->ClearTimer(load_arena_config, arena);

		mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, arena);
		mm->UnregCallback(CB_ITEM_COUNT_CHANGED, itemCountChangedCallback, arena);

		//remove lvzs from all players present in arena
		pd->Lock();
		FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING && i->arena == arena && IS_HUMAN(i))
		{
			deinit_player(i);
		}
		pd->Unlock();

		ad->on = 0;

		unload_arena_config(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
