#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_shipnames.h"
#include "kill.h"
#include "selfpos.h"

#define KILLER_NAME "<engine failure>"

typedef struct Transwarp
{
	int x1, y1, x2, y2;

	int normalAngle; //0-40

	struct Transwarp *dest;
	int destID;

	int noFlags;

	int level; //0 = public
	int eventCount; //number of times a transwarp event happens
} Transwarp;

typedef struct adata
{
	int on;

	Killer *killer;

	int warpCount;
	Transwarp *warpList;

	int cfg_AllowPriv;
} adata;

typedef struct pdata
{
	ticks_t nextWarp;
} pdata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Iarenaman *aman;
local Iconfig *cfg;
local Iplayerdata *pd;
local Ihscoreitems *items;
local Iflagcore *fc;
local Iselfpos *selfpos;
local Ikill *kill;

local int pdkey;
local int adkey;

local int warpPlayer(Player *p, Transwarp *warp, struct C2SPosition *pos, int radius)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	double radians = (-warp->normalAngle + 10) * M_PI / 20;
	double dot = (cos(radians) * pos->xspeed) - (sin(radians) * pos->yspeed); //negative because -y points up

	if (dot < 0 || warp->normalAngle == -1) //check to make sure that the vectors are pointing in opposite directions (incoming)
	{
		//they're moving in the right direction
		if (warp->noFlags == 0 || fc->CountPlayerFlags(p) == 0)
		{
			int i;
			int deltaAngle = 20; //180 degree difference in normals means no rotation
			double deltaRadians, cosDelta, sinDelta;
			int newx, newy, v_x, v_y;

			int srcCenterX = ((warp->x1 + warp->x2 + 1) << 3); //average from tiles to pixels (<<4 >>1)
			int srcCenterY = ((warp->y1 + warp->y2 + 1) << 3);
			int destCenterX = ((warp->dest->x1 + warp->dest->x2 + 1) << 3);
			int destCenterY = ((warp->dest->y1 + warp->dest->y2 + 1) << 3);

			if (warp->normalAngle != -1 && warp->dest->normalAngle != -1)
				deltaAngle += (warp->normalAngle - warp->dest->normalAngle);

			deltaRadians = (-deltaAngle) * M_PI / 20;

			//do the rotations
			cosDelta = cos(deltaRadians);
			sinDelta = sin(deltaRadians);
			newx = (int)((pos->x - srcCenterX) * cosDelta - (pos->y - srcCenterY) * sinDelta) + destCenterX;
			newy = (int)((pos->x - srcCenterX) * sinDelta + (pos->y - srcCenterY) * cosDelta) + destCenterY;
			v_x = (int)((pos->xspeed) * cosDelta - (pos->yspeed) * sinDelta);
			v_y = (int)((pos->xspeed) * sinDelta + (pos->yspeed) * cosDelta);

			if (v_x > 0 && newx - radius - 2 < warp->dest->x1 << 4)
			{
				newx = (warp->dest->x1 << 4) + radius + 2;
			}
			else if (v_x < 0 && ((warp->dest->x2 + 1) << 4) - 1 < newx + radius + 2)
			{
				newx = (((warp->dest->x2 + 1) << 4) - 1) - radius - 2;
			}

			if (v_y > 0 && newy - radius - 2 < warp->dest->y1 << 4)
			{
				newy = (warp->dest->y1 << 4) + radius + 2;
			}
			else if (v_y < 0 && ((warp->dest->y2) << 4) - 1 < newy + radius + 2)
			{
				newy = (((warp->dest->y2 + 1) << 4) - 1) - radius - 2;
			}

			selfpos->WarpPlayer(p, newx, newy, v_x, v_y, (pos->rotation - deltaAngle + 80) % 40, 0);
			data->nextWarp = current_ticks() + 100;

			for (i = 0; i < warp->eventCount; i++)
			{
				items->triggerEvent(p, p->p_ship, "transwarp");
			}

			return 1;
		}
		else
		{
			kill->Kill(p, ad->killer, 0, 0);
			return 1;
		}
	}
	else
	{
		return 0;
	}
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	int i;
	int radius;
	int playerLevel;

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (current_ticks() < data->nextWarp)
		return; //cooldown hasn't expired

	if(!ad->cfg_AllowPriv && p->p_freq >= 100)
		return;

	playerLevel = items->getPropertySum(p, p->p_ship, "transwarp", 0);
	radius = cfg->GetInt(arena->cfg, shipNames[p->p_ship], "Radius", 14);
	if (radius == 0) radius = 14;

	for (i = 0; i < ad->warpCount; i++)
	{
		Transwarp *warp = &(ad->warpList[i]);

		if (warp->dest == NULL)
			continue;

		if (warp->level > playerLevel)
			continue;

		if ((warp->x1 << 4) - radius <= pos->x && pos->x < ((warp->x2 + 1) << 4) + radius)
		{
			if ((warp->y1 << 4) - radius <= pos->y && pos->y < ((warp->y2 + 1) << 4) + radius)
			{
				//warp them
				if (warpPlayer(p, warp, pos, radius))
					return; //don't warp them more than once
			}
		}
	}
}

local void loadArenaConfig(Arena *a)
{
	char buf[256];
	int i;
	struct adata *ad = P_ARENA_DATA(a, adkey);

	ad->cfg_AllowPriv = cfg->GetInt(a->cfg, "Hyperspace", "PrivFTL", 1);

	/* cfghelp: Transwarp:WarpCount, arena, int, def: 0, mod: hs_transwarp
	 * The number of transwarps to load from the config file */
	ad->warpCount = cfg->GetInt(a->cfg, "Transwarp", "WarpCount", 0);

	//init the array
	ad->warpList = amalloc(sizeof(Transwarp) * ad->warpCount);

	for (i = 0; i < ad->warpCount; i++)
	{
		/* cfghelp: Transwarp:Warp0x1, arena, int, def: 0, mod: hs_transwarp
		 * Top left corner of warp */
		sprintf(buf, "Warp%dx1", i);
		ad->warpList[i].x1 = cfg->GetInt(a->cfg, "Transwarp", buf, 1);
		/* cfghelp: Transwarp:Warp0y1, arena, int, def: 0, mod: hs_transwarp
		 * Top left corner of warp */
		sprintf(buf, "Warp%dy1", i);
		ad->warpList[i].y1 = cfg->GetInt(a->cfg, "Transwarp", buf, 1);
		/* cfghelp: Transwarp:Warp0x2, arena, int, def: 0, mod: hs_transwarp
		 * Bottom right corner of warp */
		sprintf(buf, "Warp%dx2", i);
		ad->warpList[i].x2 = cfg->GetInt(a->cfg, "Transwarp", buf, 2);
		/* cfghelp: Transwarp:Warp0y2, arena, int, def: 0, mod: hs_transwarp
		 * Bottom right corner of warp */
		sprintf(buf, "Warp%dy2", i);
		ad->warpList[i].y2 = cfg->GetInt(a->cfg, "Transwarp", buf, 2);
		/* cfghelp: Transwarp:Warp0NormalAngle, arena, int, def: 0, mod: hs_transwarp
		 * Angle in the direction of the output
		 * 0 = north, 10 = east */
		sprintf(buf, "Warp%dNormalAngle", i);
		ad->warpList[i].normalAngle = cfg->GetInt(a->cfg, "Transwarp", buf, 0);
		/* cfghelp: Transwarp:Warp0DestWarp, arena, int, def: 0, mod: hs_transwarp
		 * The ID of the transwarp to warp players into */
		sprintf(buf, "Warp%dDestWarp", i);
		ad->warpList[i].destID = cfg->GetInt(a->cfg, "Transwarp", buf, 0);
		/* cfghelp: Transwarp:Warp0Level, arena, int, def: 0, mod: hs_transwarp
		 * Level of 'transwarp' needed to use */
		sprintf(buf, "Warp%dLevel", i);
		ad->warpList[i].level = cfg->GetInt(a->cfg, "Transwarp", buf, 0);
		/* cfghelp: Transwarp:Warp0EventCount, arena, int, def: level, mod: hs_transwarp
		 * Number of 'transwarp' events generated */
		sprintf(buf, "Warp%dEventCount", i);
		ad->warpList[i].eventCount = cfg->GetInt(a->cfg, "Transwarp", buf, ad->warpList[i].level);
		/* cfghelp: Transwarp:Warp0NoFlags, arena, int, def: 1, mod: hs_transwarp
		 * If flags are allowed in this warp */
		sprintf(buf, "Warp%dNoFlags", i);
		ad->warpList[i].noFlags = cfg->GetInt(a->cfg, "Transwarp", buf, 1);
	}

	for (i = 0; i < ad->warpCount; i++)
	{
		int id = ad->warpList[i].destID;
		if (id < ad->warpCount && 0 <= id)
		{
			ad->warpList[i].dest = &(ad->warpList[id]);
		}
		else
		{
			ad->warpList[i].dest = NULL;
		}
	}
}

local void unloadArenaConfig(Arena *a)
{
	struct adata *ad = P_ARENA_DATA(a, adkey);

	afree(ad->warpList);
}

EXPORT const char info_hs_transwarp[] = "v6.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_transwarp(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		kill = mm->GetInterface(I_KILL, ALLARENAS);

		if (!lm || !net || !aman || !cfg || !pd || !items || !fc || !selfpos || !kill) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
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
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(fc);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(kill);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 1;

		ad->killer = kill->LoadKiller(KILLER_NAME, arena, 0, 9999);

		loadArenaConfig(arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 0;

		kill->UnloadKiller(ad->killer);

		unloadArenaConfig(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
