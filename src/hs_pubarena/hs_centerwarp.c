#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "objects.h"

local Imodman *mm;
local Ichat *chat;
local Igame *game;
local Inet *net;
local Iarenaman *aman;
local Iconfig *cfg;
local Iobjects *obj;
local Imainloop *ml;
local Imapdata *mapdata;
local Iprng *prng;

local HashTable *inCenterWarper;

local int spawnkey;

#define LVZID_SPAWN_BASE 4000

typedef struct SpawnData
{
	i8 nextspawnId;
	ticks_t nextRotation;
	Region *outRgn[4];

	Region *warper;

	int outX;
	int outY;

	int attached : 2;
} SpawnData;



local void Pppk(Player *p, byte *p2, int len)
{
    struct C2SPosition *pos = (struct C2SPosition *)p2;

	Arena *arena = p->arena;
	SpawnData *sd = P_ARENA_DATA(arena, spawnkey);
	Target t;
	t.type = T_PLAYER;
	t.u.p = p;

	/* handle common errors */
	if (!arena) return;

	/* speccers don't get their position sent to anyone */
	if (p->p_ship == SHIP_SPEC)
		return;

	//avoid unpleasant situations
	if (!sd->attached)
		return;

	if (mapdata->Contains(sd->warper, pos->x>>4, pos->y>>4))
	{
		int *last = HashGetOne(inCenterWarper, p->name);
		if (!last || *last < current_ticks())
		{
			int * time;
			int x, y;
			afree(last);
			time = amalloc(sizeof(*time));
			*time = current_ticks() + 100;
			HashReplace(inCenterWarper, p->name, time);

			x = sd->outX + prng->Number(-4, 4);
			y = sd->outY + prng->Number(-4, 4);

			sd->nextRotation = current_ticks() + (unsigned int)cfg->GetInt(arena->cfg, "hyperspace", "centerwarpintervaladd", 500);
			game->WarpTo(&t, x, y);
		}
	}
}

local int icwenum(const char *key, void *l, void *d)
{
	int *last = (int *)l;
	afree(last);
	return 1;
}

local int perSecond(void *arena)
{
	Arena *a = arena;
	SpawnData *sd = P_ARENA_DATA(a, spawnkey);
	ticks_t ct;
	if (!sd->attached)
		return 0;

	ct = current_ticks();
	if (ct > sd->nextRotation)
	{
		Target t;
		t.type = T_ARENA;
		t.u.arena = a;
		sd->nextRotation = ct + cfg->GetInt(a->cfg, "hyperspace", "centerwarpinterval", 200);
		obj->Toggle(&t, LVZID_SPAWN_BASE + 1 + ((sd->nextspawnId + 3)%4), 0);
		obj->Toggle(&t, LVZID_SPAWN_BASE + 1 + sd->nextspawnId, 1);
		obj->Toggle(&t, LVZID_SPAWN_BASE, 1);

		mapdata->FindRandomPoint(sd->outRgn[sd->nextspawnId], &sd->outX, &sd->outY);

		sd->nextspawnId = (sd->nextspawnId + 1) % 4;
	}

	return 1;
}

EXPORT int MM_hs_centerwarp(int action, Imodman *_mm, Arena *arena)
{
	if(action == MM_LOAD)
	{
		mm = _mm;

		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		obj = mm->GetInterface(I_OBJECTS, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);

		if(!chat || !game || !net || !aman || !cfg || !obj || !ml || !mapdata || !prng)
			return MM_FAIL;

		spawnkey = aman->AllocateArenaData(sizeof(SpawnData));
		if (spawnkey == -1)
            return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		inCenterWarper = HashAlloc();

		return MM_OK;
	}
	else if(action == MM_UNLOAD)
	{
		ml->ClearTimer(perSecond, 0);

		net->RemovePacket(C2S_POSITION, Pppk);

		HashEnum(inCenterWarper, icwenum, 0);
		HashFree(inCenterWarper);

		aman->FreeArenaData(spawnkey);

		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(obj);
		mm->ReleaseInterface(mapdata);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
    {
		Target t;
		int i;
		SpawnData *sd = P_ARENA_DATA(arena, spawnkey);
		t.type = T_ARENA;
		t.u.arena = arena;

		sd->warper = mapdata->FindRegionByName(arena, "centerwarp");

		sd->nextRotation = current_ticks() + cfg->GetInt(arena->cfg, "hyperspace", "centerwarpinterval", 200);

		sd->nextspawnId = 1;

		obj->Toggle(&t, LVZID_SPAWN_BASE, 1);
		for (i = 0; i < 4; ++i)
		{
			char buf[32];
			obj->Toggle(&t, LVZID_SPAWN_BASE + 1 + i, 0);
			sprintf(buf, "center%d", i);
			sd->outRgn[i] = mapdata->FindRegionByName(arena, buf);
			
			// TODO: cleanup
			if (sd->outRgn[i] == NULL)
				return MM_FAIL;
		}
		obj->Toggle(&t, LVZID_SPAWN_BASE + 1, 1);

		mapdata->FindRandomPoint(sd->outRgn[1], &sd->outX, &sd->outY);

		sd->attached = 1;
		ml->SetTimer(perSecond, 100, 100, arena, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
		Target t;
		int i;
		SpawnData *sd = P_ARENA_DATA(arena, spawnkey);
		t.type = T_ARENA;
		t.u.arena = arena;

		ml->ClearTimer(perSecond, arena);
		sd->attached = 0;

		for (i = 0; i < 5; ++i)
			obj->Toggle(&t, LVZID_SPAWN_BASE + i, 0);

        return MM_OK;
    }
	return MM_FAIL;
}
