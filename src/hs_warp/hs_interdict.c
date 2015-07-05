#include <stdlib.h>

#include "asss.h"
#include "hs_interdict.h"
#include "kill.h"

#define KILLER_NAME "<engine failure>"
#define REGION_DELAY 100

struct adata
{
	int on;
	Killer *killer;
	Region *region;
	Region *regionExit;
};

struct pdata
{
	time_t timeout;
	int interdicted;
	struct S2CWeapons wpn;

	int portals;
	int returning;
};

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Imapdata *mapdata;
local Iplayerdata *pd;
local Iarenaman *aman;
local Igame *game;
local Ikill *kill;
local Imainloop *ml;
local Iconfig *cfg;

local int pdkey;
local int adkey;

local void RemoveInterdiction(Player *p)
{
	struct pdata *data = PPDATA(p, pdkey);

	data->interdicted = 0;
	data->timeout = current_ticks() + 100;
}

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	//remove interdiction while they're dead
	struct pdata *data = PPDATA(killer, pdkey);

	int delay = cfg->GetInt(arena->cfg, "Kill", "EnterDelay", 0);

	data->interdicted = 0;
	data->timeout = current_ticks() + delay + 100;
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	struct pdata *data = PPDATA(p, pdkey);

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (mapdata->Contains(ad->region, pos->x>>4, pos->y>>4))
	{
		if (!data->interdicted)
		{
			if (data->timeout < current_ticks()) //if we have timed out
			{
				//interdict them
				data->interdicted = 1;
			}
			//ignore them if they haven't timed out yet
		}
		else
		{
			//update position
			data->wpn.type = S2C_WEAPON;
			data->wpn.rotation = pos->rotation;
			data->wpn.time = pos->time & 0xFFFF;
			data->wpn.x = pos->x;
			data->wpn.yspeed = pos->yspeed;
			data->wpn.playerid = p->pid;
			data->wpn.xspeed = pos->xspeed;
			data->wpn.status = pos->status;
			data->wpn.c2slatency = 0;
			data->wpn.y = pos->y;
			data->wpn.bounty = pos->bounty;

			if (data->returning)
			{
				if (pos->extra.portals < data->portals)
				{
					//they layed a portal.
					//nuke em
					kill->Kill(p, ad->killer, 0, 0);
				}
			}

			data->portals = pos->extra.portals;
			data->returning = 0;
		}
	}
	else
	{
		if (data->interdicted)
		{
			//they left the interdiction zone

			if (ad->regionExit && mapdata->Contains(ad->regionExit, pos->x>>4, pos->y>>4)) //left legally
			{
				data->interdicted = 0;
			}
			else if (p->p_attached != -1)
			{
				data->interdicted = 0;
			}
			else
			{
				if (data->wpn.type == S2C_WEAPON)
				{
					data->wpn.time = current_ticks() & 0xFFFF;

					game->DoWeaponChecksum(&(data->wpn));
					net->SendToArena(arena, NULL, (byte*)&(data->wpn), sizeof(struct S2CWeapons) -
							sizeof(struct ExtraPosData), NET_PRI_P4);

					data->returning = 1;
				}
			}
		}
		//we dont care if they weren't already interdicted
	}
}

local int load_regions(void *clos)
{
	Arena *arena = clos;
	struct adata *ad = P_ARENA_DATA(arena, adkey);

	ad->region = mapdata->FindRegionByName(arena, "interdict");
	if (ad->region != NULL)
	{
		ad->on = 1;
		ad->killer = kill->LoadKiller(KILLER_NAME, arena, 0, 9999);
		ad->regionExit = mapdata->FindRegionByName(arena, "interdictexit");
		return FALSE;
	}

	return TRUE;
}

local Ihsinterdict myint =
{
	INTERFACE_HEAD_INIT(I_HS_INTERDICT, "hs_interdict")
	RemoveInterdiction,
};

EXPORT const char info_hs_interdict[] = "v1.2 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_interdict(int action, Imodman *_mm, Arena *arena)
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
		kill = mm->GetInterface(I_KILL, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!lm || !net || !mapdata || !pd || !aman || !game || !kill || !ml || !cfg) return MM_FAIL;

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

		ml->ClearTimer(load_regions, NULL);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(kill);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(cfg);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&myint, arena);
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 0; //haven't done regions yet
		ml->SetTimer(load_regions, REGION_DELAY, REGION_DELAY, arena, arena);

		mm->RegCallback(CB_KILL, killCallback, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		if (mm->UnregInterface(&myint, arena))
			return MM_FAIL;

		struct adata *ad = P_ARENA_DATA(arena, adkey);

		mm->UnregCallback(CB_KILL, killCallback, arena);

		ml->ClearTimer(load_regions, arena);

		if (ad->on)
		{
			ad->on = 0;
			kill->UnloadKiller(ad->killer);
		}

		return MM_OK;
	}
	return MM_FAIL;
}

