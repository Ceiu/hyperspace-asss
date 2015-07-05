#include "asss.h"

struct adata
{
	int on;
};

struct pdata
{
	time_t last;
	int count;
};

/* global data */
local int adkey;
local int pdkey;

local Iplayerdata *pd;
local Ilogman *lm;
local Inet *net;
local Imapdata *mapdata;
local Iarenaman *aman;
local Igame *game;

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	struct adata *ad = P_ARENA_DATA(arena, adkey);

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (pos->status & STATUS_ANTIWARP && mapdata->InRegion(arena, "noanti", pos->x>>4, pos->y>>4))
	{
		struct pdata *data = PPDATA(p, pdkey);

		//lm->LogP(L_DRIVEL, "hs_antiwarp", p, "removing anti.");
		if (data->last + 50 < current_ticks())
		{
			if (data->count < 4)
			{
				data->count++;

				struct S2CWeapons wpn = {
					S2C_WEAPON, pos->rotation, pos->time & 0xFFFF, pos->x, pos->yspeed,
					p->pid, pos->xspeed, 0, pos->status & ~STATUS_ANTIWARP, 0, pos->y, pos->bounty
				};

				game->DoWeaponChecksum(&wpn);
				net->SendToArena(arena, NULL, (byte*)&wpn, sizeof(struct S2CWeapons) -
						sizeof(struct ExtraPosData), NET_PRI_P4);
			}
			else
			{
				Target t;
				t.type = T_PLAYER;
				t.u.p = p;
				game->GivePrize(&t, -20, 1);

				data->count = 0;
			}

			//update last time
			data->last = current_ticks();
		}
	}
}

EXPORT const char info_hs_antiwarp[] = "v1.2 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_antiwarp(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!pd || !lm || !net || !mapdata || !aman || !game) return MM_FAIL;

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

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 1;
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 0;
		return MM_OK;
	}
	return MM_FAIL;
}

