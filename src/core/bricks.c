
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "clientset.h"

/* structs */
#include "packets/brick.h"

typedef struct
{
	u16 cbrickid;
	ticks_t lasttime;
	LinkedList list;
	pthread_mutex_t mtx;
	int brickspan, brickmode, countbricksaswalls, bricktime;
	/*  int        int        boolean             int */
	int antibrickwarpdistance;
	int shipradius[8];
} brickdata;


/* prototypes */
local void PlayerAction(Player *p, int action, Arena *arena);
local void ArenaAction(Arena *arena, int action);

local void SendOldBricks(Player *p);

local void expire_bricks(Arena *arena);

local void DoBrickModeCallback(Arena *arena, int brickmode, int dropx,
                int dropy, int direction, int length, LinkedList *bricks);


/* packet funcs */
local void PBrick(Player *, byte *, int);


/* interface */
local void DropBrick(Arena *arena, int freq, int x1, int y1, int x2, int y2);
local void HandleBrick(Player *p, int x, int y, LinkedList *bricks);

local Ibricks _myint =
{
	INTERFACE_HEAD_INIT(I_BRICKS, "brick")
	DropBrick
};

local Ibrickhandler _myhandler =
{
	INTERFACE_HEAD_INIT(I_BRICK_HANDLER, "brick")
	HandleBrick
};

/* global data */

local Iconfig *cfg;
local Inet *net;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;
local Imapdata *mapdata;
local Imainloop *ml;
local Iprng *prng;
local Iplayerdata *pd;

/* big arrays */
local int brickkey;


DEFINE_FROM_STRING(brickmode_val, BRICK_MODE_MAP)

EXPORT const char info_bricks[] = CORE_MOD_INFO("bricks");

EXPORT int MM_bricks(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!net || !cfg || !lm || !aman || !ml || !mapdata || !prng || !pd)
			return MM_FAIL;

		brickkey = aman->AllocateArenaData(sizeof(brickdata));
		if (brickkey == -1)
			return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		mm->RegCallback(CB_DOBRICKMODE, DoBrickModeCallback, ALLARENAS);

		net->AddPacket(C2S_BRICK, PBrick);

		mm->RegInterface(&_myint, ALLARENAS);
		mm->RegInterface(&_myhandler, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;

		if (mm->UnregInterface(&_myhandler, ALLARENAS))
			return MM_FAIL;

		net->RemovePacket(C2S_BRICK, PBrick);

		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		mm->UnregCallback(CB_DOBRICKMODE, DoBrickModeCallback, ALLARENAS);

		aman->FreeArenaData(brickkey);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


void ArenaAction(Arena *arena, int action)
{
	brickdata *bd = P_ARENA_DATA(arena, brickkey);

	if (action == AA_PRECREATE)
	{
		pthread_mutex_init(&bd->mtx, NULL);

		LLInit(&bd->list);
		bd->cbrickid = 0;
		bd->lasttime = current_ticks();
	}

	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		int i;

		/* cfghelp: Brick:CountBricksAsWalls, arena, bool, def: 1
		 * Whether bricks snap to the edges of other bricks (as opposed
		 * to only snapping to walls) */
		bd->countbricksaswalls = cfg->GetInt(arena->cfg, "Brick", "CountBricksAsWalls", 1);
		/* cfghelp: Brick:BrickSpan, arena, int, def: 10
		 * The maximum length of a dropped brick. */
		bd->brickspan = cfg->GetInt(arena->cfg, "Brick", "BrickSpan", 10);
		/* cfghelp: Brick:BrickMode, arena, enum, def: BRICK_VIE
		 * How bricks behave when they are dropped (BRICK_VIE=improved
		 * vie style, BRICK_AHEAD=drop in a line ahead of player,
		 * BRICK_LATERAL=drop laterally across player,
		 * BRICK_CAGE=drop 4 bricks simultaneously to create a cage) */
		bd->brickmode =
			brickmode_val(cfg->GetStr(arena->cfg, "Brick", "BrickMode"), BRICK_VIE);
		/* cfghelp: Brick:BrickTime, arena, int, def: 6000
		 * How long bricks last (in ticks). */
		bd->bricktime = cfg->GetInt(arena->cfg, "Brick", "BrickTime", 6000) + 10;
		/* 100 ms added for lag */

		/* cfghelp: Brick:AntibrickwarpDistance, arena, int, def: 0
		 * Squared smallest distance allowed between players and new bricks
		 * before new bricks are cancelled to prevent brickwarping.
		 * 0 disables antibrickwarp feature. */
		bd->antibrickwarpdistance = cfg->GetInt(arena->cfg, "Brick", "AntibrickwarpDistance", 0);

		for (i = SHIP_WARBIRD; i <= SHIP_SHARK; ++i)
		{
			bd->shipradius[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "Radius", 14);
			if (bd->shipradius[i] == 0)
				bd->shipradius[i] = 14;
		}
	}
	else if (action == AA_DESTROY)
	{
		LLEnum(&bd->list, afree);
		LLEmpty(&bd->list);
	}
	else if (action == AA_POSTDESTROY)
	{
		pthread_mutex_destroy(&bd->mtx);
	}
}


void PlayerAction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
		SendOldBricks(p);
}


/* call with mutex */
local void expire_bricks(Arena *arena)
{
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	LinkedList *list = &bd->list;
	ticks_t gtc = current_ticks(), timeout = bd->bricktime;
	Link *l, *next;

	for (l = LLGetHead(list); l; l = next)
	{
		struct S2CBrickPacket *pkt = l->data;
		next = l->next;

		if (TICK_GT(gtc, pkt->starttime + timeout))
		{
			if (bd->countbricksaswalls)
				mapdata->DoBrick(arena, 0, pkt->x1, pkt->y1, pkt->x2, pkt->y2);
			LLRemove(list, pkt);
			afree(pkt);
		}
	}
}


/* call with mutex */
local void drop_brick(Arena *arena, int freq, int x1, int y1, int x2, int y2)
{
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	struct S2CBrickPacket *pkt = amalloc(sizeof(*pkt));

	pkt->x1 = x1; pkt->y1 = y1;
	pkt->x2 = x2; pkt->y2 = y2;
	pkt->type = S2C_BRICK;
	pkt->freq = freq;
	pkt->brickid = bd->cbrickid++;
	pkt->starttime = current_ticks();
	/* workaround for stupid priitk */
	if (pkt->starttime <= bd->lasttime)
		pkt->starttime = ++bd->lasttime;
	else
		bd->lasttime = pkt->starttime;
	LLAdd(&bd->list, pkt);

	net->SendToArena(arena, NULL, (byte*)pkt, sizeof(*pkt), NET_RELIABLE | NET_URGENT);
	lm->Log(L_DRIVEL, "<game> {%s} brick dropped (%d,%d)-(%d,%d) (freq=%d) (id=%d)",
	        arena->name,
	        x1, y1, x2, y2, freq,
	        pkt->brickid);

	if (bd->countbricksaswalls)
		mapdata->DoBrick(arena, 1, x1, y1, x2, y2);
}


void DropBrick(Arena *arena, int freq, int x1, int y1, int x2, int y2)
{
	brickdata *bd = P_ARENA_DATA(arena, brickkey);

	pthread_mutex_lock(&bd->mtx);

	expire_bricks(arena);
	drop_brick(arena, freq, x1, y1, x2, y2);

	pthread_mutex_unlock(&bd->mtx);
}


void SendOldBricks(Player *p)
{
	Arena *arena = p->arena;
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	LinkedList *list = &bd->list;
	Link *l;

	pthread_mutex_lock(&bd->mtx);

	expire_bricks(arena);

	for (l = LLGetHead(list); l; l = l->next)
	{
		struct S2CBrickPacket *pkt = (struct S2CBrickPacket*)l->data;
		net->SendToOne(p, (byte*)pkt, sizeof(*pkt), NET_RELIABLE);
	}

	pthread_mutex_unlock(&bd->mtx);
}


void PBrick(Player *p, byte *pkt, int len)
{
	Arena *arena = p->arena;
	brickdata *bd = P_ARENA_DATA(arena, brickkey);
	int dx, dy;

	if (len != 5)
	{
		lm->LogP(L_MALICIOUS, "bricks", p, "bad packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || p->p_ship == SHIP_SPEC || !arena)
	{
		lm->LogP(L_WARN, "bricks", p, "ignored request from bad state");
		return;
	}


	dx = ((struct SimplePacket*)pkt)->d1;
	dy = ((struct SimplePacket*)pkt)->d2;

	pthread_mutex_lock(&bd->mtx);

	expire_bricks(arena);

	Ibrickhandler *bh = mm->GetInterface(I_BRICK_HANDLER, arena);
	if (bh == NULL)
	{
		lm->LogP(L_ERROR, "bricks", p, "No brick handler found.");
	}
	else
	{
		LinkedList brick_list;
		Link *bricklink;
		Brick *brick;

		LLInit(&brick_list);

		bh->HandleBrick(p, dx, dy, &brick_list);

		if (bd->antibrickwarpdistance > 0)
		{
			/* grab the pd lock only once and iterate through players only once
			 * doing it this way is more important when there is more than one brick
			 * in the list.
			 */
			Link *link;
			Player *q;
			int ship;
			pd->Lock();
			FOR_EACH_PLAYER(q)
			{
				if (q->arena != p->arena)
					continue;
				if (q->p_ship == SHIP_SPEC)
					continue;
				if (q->p_freq == p->p_freq)
					continue;
				if (q->flags.is_dead)
					continue;
				ship = q->p_ship;

				FOR_EACH(&brick_list, brick, bricklink)
				{
					int x, y, pxa, pya, pxb, pyb;
					int cancel = 0;

					if (brick->x1 == brick->x2)
					{
						//moving vertically
						x = brick->x1;
						for (y = brick->y1; y <= brick->y2; ++y)
						{
							pxa = (q->position.x - bd->shipradius[ship]) - (x*16+8);
							pxb = (q->position.x + bd->shipradius[ship]) - (x*16+8);
							pya = (q->position.y - bd->shipradius[ship]) - (y*16+8);
							pyb = (q->position.y + bd->shipradius[ship]) - (y*16+8);
							if (((pxa*pxa+pya*pya) < bd->antibrickwarpdistance)
							 || ((pxb*pxb+pya*pya) < bd->antibrickwarpdistance)
							 || ((pxa*pxa+pyb*pyb) < bd->antibrickwarpdistance)
							 || ((pxb*pxb+pyb*pyb) < bd->antibrickwarpdistance))
							{
								cancel = 1;
								break;
							}
						}
					}
					else
					{
						y = brick->y1;
						for (x = brick->x1; x <= brick->x2; ++x)
						{
							pxa = (q->position.x - bd->shipradius[ship]) - (x*16+8);
							pxb = (q->position.x + bd->shipradius[ship]) - (x*16+8);
							pya = (q->position.y - bd->shipradius[ship]) - (y*16+8);
							pyb = (q->position.y + bd->shipradius[ship]) - (y*16+8);
							if (((pxa*pxa+pya*pya) < bd->antibrickwarpdistance)
							 || ((pxb*pxb+pya*pya) < bd->antibrickwarpdistance)
							 || ((pxa*pxa+pyb*pyb) < bd->antibrickwarpdistance)
							 || ((pxb*pxb+pyb*pyb) < bd->antibrickwarpdistance))
							{
								cancel = 1;
								break;
							}
						}
					}

					if (cancel)
					{
						LLRemove(&brick_list, brick);
						afree(brick);
					}
				}
			}
			pd->Unlock();
		}

		FOR_EACH(&brick_list, brick, bricklink)
		{
			drop_brick(arena, p->p_freq, brick->x1, brick->y1, brick->x2, brick->y2);
			afree(brick);
		}

		mm->ReleaseInterface(bh);

		LLEmpty(&brick_list);
	}


	pthread_mutex_unlock(&bd->mtx);
}

local void HandleBrick(Player *p, int x, int y, LinkedList *bricks)
{
	Arena *arena = p->arena;
        brickdata *bd = P_ARENA_DATA(arena, brickkey);

	DO_CBS(CB_DOBRICKMODE, arena, DoBrickModeFunction, (arena,
		bd->brickmode, x, y, p->position.rotation, bd->brickspan,
		bricks));
}

local void DoBrickModeCallback(Arena *arena, int brickmode, int dropx,
		int dropy, int direction, int length, LinkedList *bricks)
{
	/* This code has been moved from mapdata for greater flexibility. It
	 * has been modified to be called as a callback instead of a module
	 * function. This will allow new modules to add additional brick modes.
	 *
	 * Though some might say it's less efficient here, brick code really
	 * should be in the brick module. The few indirections are very minor
	 * computation wise.
	 * -Dr Brain
	 */

	enum { up, right, down, left } dir;
	int bestcount, bestdir, x, y, destx = dropx, desty = dropy;
	Brick *brick;

	if (mapdata->GetTile(arena, dropx, dropy))
	{
		return;
	}

	/* in the worst case, we can always just drop a single block */

	if (brickmode == BRICK_VIE)
	{
		/* find closest wall and the point next to it */
		bestcount = 3000;
		bestdir = -1;
		for (dir = 0; dir < 4; dir++)
		{
			int count = 0, oldx = dropx, oldy = dropy;
			x = dropx; y = dropy;

			while (x >= 0 && x < 1024 &&
			       y >= 0 && y < 1024 &&
			       mapdata->GetTile(arena, x, y) == 0 &&
			       count < length)
			{
				switch (dir)
				{
					case down:  oldy = y++; break;
					case right: oldx = x++; break;
					case up:    oldy = y--; break;
					case left:  oldx = x--; break;
				}
				count++;
			}

			if (count < bestcount)
			{
				bestcount = count;
				bestdir = dir;
				destx = oldx; desty = oldy;
			}
		}

		if (bestdir == -1)
		{
			/* shouldn't happen */
			goto failed;
		}

		if (bestcount == length)
		{
			/* no closest wall */
			if (prng->Get32() & 1)
			{
				destx = dropx - length / 2;
				desty = dropy;
				bestdir = left;
			}
			else
			{
				destx = dropx;
				desty = dropy - length / 2;
				bestdir = up;
			}
		}
	}
	else if (brickmode == BRICK_AHEAD)
	{
		int count = 0, oldx = dropx, oldy = dropy;

		x = dropx; y = dropy;

		bestdir = ((direction + 5) % 40) / 10;

		while (x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       mapdata->GetTile(arena, x, y) == 0 &&
		       count < length)
		{
			switch (bestdir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		destx = oldx; desty = oldy;
	}
	else if (brickmode == BRICK_LATERAL)
	{
		int count = 0, oldx = dropx, oldy = dropy;

		x = dropx; y = dropy;

		bestdir = ((direction + 15) % 40) / 10;

		while (x >= 0 && x < 1024 &&
		       y >= 0 && y < 1024 &&
		       mapdata->GetTile(arena, x, y) == 0 &&
		       count <= length / 2)
		{
			switch (bestdir)
			{
				case down:  oldy = y++; break;
				case right: oldx = x++; break;
				case up:    oldy = y--; break;
				case left:  oldx = x--; break;
			}
			count++;
		}

		destx = oldx; desty = oldy;
	}
	else if (brickmode == BRICK_CAGE)
	{
		Brick *cage_bricks[4];

		/* generate drop coords inside map */
		int sx = dropx - (length) / 2,
			sy = dropy - (length) / 2,
			ex = dropx + (length + 1) / 2,
			ey = dropy + (length + 1) / 2;

		if (sx < 0) sx = 0;
		if (sy < 0) sy = 0;
		if (ex > 1023) ex = 1023;
		if (ey > 1023) ey = 1023;

		/* top */
		x = sx;
		y = sy;

		/* allocate bricks and add them to the list*/
		cage_bricks[0] = amalloc(sizeof(Brick));
		cage_bricks[1] = amalloc(sizeof(Brick));
		cage_bricks[2] = amalloc(sizeof(Brick));
		cage_bricks[3] = amalloc(sizeof(Brick));
		LLAdd(bricks, cage_bricks[0]);
		LLAdd(bricks, cage_bricks[1]);
		LLAdd(bricks, cage_bricks[2]);
		LLAdd(bricks, cage_bricks[3]);


		while (x <= ex && mapdata->GetTile(arena, x, y))
			++x;

		if (x > ex)
		{
			cage_bricks[0]->x1 = dropx;
			cage_bricks[0]->x2 = dropx;
			cage_bricks[0]->y1 = dropy;
			cage_bricks[0]->y2 = dropy;
		}
		else
		{
			cage_bricks[0]->x1 = x++;
			while (x <= ex && !mapdata->GetTile(arena, x, y))
				++x;
			cage_bricks[0]->x2 = x-1;
			cage_bricks[0]->y1 = y;
			cage_bricks[0]->y2 = y;
		}

		/* bottom */
		x = sx;
		y = ey;

		while (x <= ex && mapdata->GetTile(arena, x, y))
			++x;

		if (x > ex)
		{
			cage_bricks[1]->x1 = dropx;
			cage_bricks[1]->x2 = dropx;
			cage_bricks[1]->y1 = dropy;
			cage_bricks[1]->y2 = dropy;
		}
		else
		{
			cage_bricks[1]->x1 = x++;
			while (x <= ex && !mapdata->GetTile(arena, x, y))
				++x;
			cage_bricks[1]->x2 = x-1;
			cage_bricks[1]->y1 = y;
			cage_bricks[1]->y2 = y;
		}

		/* left */
		x = sx;
		y = sy;

		while (y <= ey && mapdata->GetTile(arena, x, y))
			++y;

		if (y > ey)
		{
			cage_bricks[2]->x1 = dropx;
			cage_bricks[2]->x2 = dropx;
			cage_bricks[2]->y1 = dropy;
			cage_bricks[2]->y2 = dropy;
		}
		else
		{
			cage_bricks[2]->y1 = y++;
			while (y <= ey && !mapdata->GetTile(arena, x, y))
				++y;
			cage_bricks[2]->y2 = y-1;
			cage_bricks[2]->x1 = x;
			cage_bricks[2]->x2 = x;
		}

		/* right */
		x = ex;
		y = sy;

		while (y <= ey && mapdata->GetTile(arena, x, y))
			++y;

		if (y > ey)
		{
			cage_bricks[3]->x1 = dropx;
			cage_bricks[3]->x2 = dropx;
			cage_bricks[3]->y1 = dropy;
			cage_bricks[3]->y2 = dropy;
		}
		else
		{
			cage_bricks[3]->y1 = y++;
			while (y <= ey && !mapdata->GetTile(arena, x, y))
				++y;
			cage_bricks[3]->y2 = y-1;
			cage_bricks[3]->x1 = x;
			cage_bricks[3]->x2 = x;
		}

		goto failed;
	}
	else
		goto failed;

	/* allocate the brick */
	brick = amalloc(sizeof(*brick));
	LLAdd(bricks, brick);

	/* enter first coordinate */
	dropx = x = brick->x1 = destx; dropy = y = brick->y1 = desty;

	/* go from closest point */
	switch (bestdir)
	{
		case down:
			while (mapdata->GetTile(arena, x, y) == 0 &&
			       (dropy - y) < length &&
			       y >= 0)
				desty = y--;
			break;

		case right:
			while (mapdata->GetTile(arena, x, y) == 0 &&
			       (dropx - x) < length &&
			       x >= 0)
				destx = x--;
			break;

		case up:
			while (mapdata->GetTile(arena, x, y) == 0 &&
			       (y - dropy) < length &&
			       y < 1024)
				desty = y++;
			break;

		case left:
			while (mapdata->GetTile(arena, x, y) == 0 &&
			       (x - dropx) < length &&
			       x < 1024)
				destx = x++;
			break;
	}

	/* enter second coordinate */
	brick->x2 = destx; brick->y2 = desty;

	/* swap if necessary */
	if (brick->x1 > brick->x2)
	{
		x = brick->x1;
		brick->x1 = brick->x2;
		brick->x2 = x;
	}
	if (brick->y1 > brick->y2)
	{
		y = brick->y1;
		brick->y1 = brick->y2;
		brick->y2 = y;
	}

failed:
	return;
}
