#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"

#define BRICK_CAGE_NEW 4
#define BRICK_HOLES 5

local Iconfig *cfg;
local Ilogman *lm;
local Imodman *mm;
local Imapdata *mapdata;
local Ihscoreitems *items;
local Iprng *prng;

local void HandleBrick(Player *p, int x, int y, LinkedList *bricks)
{
	Arena *arena = p->arena;

	if (!items->getPropertySum(p, p->p_ship, "dontbrick", 0))
	{
		int defaultBrickmode = cfg->GetInt(arena->cfg, "Brick", "BrickMode", 1);
		int defaultBrickspan = cfg->GetInt(arena->cfg, "Brick", "BrickSpan", 10);
		int brickmode = items->getPropertySum(p, p->p_ship, "brickmode", defaultBrickmode);
		int brickspan = items->getPropertySum(p, p->p_ship, "brickspan", defaultBrickspan);

		DO_CBS(CB_DOBRICKMODE, arena, DoBrickModeFunction, (arena,
			brickmode, x, y, p->position.rotation, 
			brickspan, bricks));
	}

	items->triggerEvent(p, p->p_ship, "brick");
}

local void doBrickModeCallback(Arena *arena, int brickmode, int dropx,
		int dropy, int direction, int length, LinkedList *bricks)
{
	enum { up, right, down, left } dir, bestdir;
	int bestcount, x, y, destx = dropx, desty = dropy;

	if (brickmode == BRICK_CAGE_NEW)
	{
		Brick *cageBricks[4];
		
		int i = 0;

		int bx, ox, by, oy;

		int ax = bx = ox = dropx;
		int ay = by = oy = dropy;
		int dc[4] = {0, 0, 0, 0};
		
		/* allocate bricks and add them to the list*/
		for (i = 0; i < 4; i++)
		{
			cageBricks[i] = amalloc(sizeof(Brick));
			LLAdd(bricks, cageBricks[i]);
		}

		bestdir = ((direction + 15) % 40) / 10;

		for (i = 0; i < 5; ++i)
		{
			int count;

			if (i)
				count = length;
			else
				count = (length) / 2;

			count -= dc[i % 4];

			while (bx >= 0 && bx < 1024 &&
				by >= 0 && by < 1024 &&
				count > 0)
			{
				ox = bx;
				oy = by;

				switch (bestdir)
				{
					case down:  ++by; break;
					case right: ++bx; break;
					case up:    --by; break;
					case left:  --bx; break;
				}

				if (mapdata->GetTile(arena, bx, by) == 0)
				{
					--count;
				}
				else
				{
					break;
				}
			}

			if (count > 0)
			{
				dc[(i + 2) % 4] = count;

				by = oy;
				bx = ox;
			}

			if (((ax == bx) && (by > ay)) || ((ay == by) && (bx > ax)))
			{
				cageBricks[i % 4]->x2 = bx;
				cageBricks[i % 4]->y2 = by;
				cageBricks[i % 4]->x1 = ax;
				cageBricks[i % 4]->y1 = ay;
			}
			else
			{
				cageBricks[i % 4]->x1 = bx;
				cageBricks[i % 4]->y1 = by;
				cageBricks[i % 4]->x2 = ax;
				cageBricks[i % 4]->y2 = ay;
			}

			ax = bx;
			ay = by;

			bestdir = (bestdir + 1) % 4;
		}
	}
	else if (brickmode == BRICK_HOLES)
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
			return;
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

		/* enter first coordinate */
		dropx = x = destx; dropy = y = desty;

		/* go from closest point */
		switch (bestdir)
		{
			case down:
				while (mapdata->GetTile(arena, x, y) == 0 && (dropy - y) < length && y >= 0)
				desty = y--;
				break;
			case right:
				while (mapdata->GetTile(arena, x, y) == 0 && (dropx - x) < length && x >= 0)
				destx = x--;
				break;
			case up:
				while (mapdata->GetTile(arena, x, y) == 0 && (y - dropy) < length && y < 1024)
				desty = y++;
				break;
			case left:
				while (mapdata->GetTile(arena, x, y) == 0 && (x - dropx) < length && x < 1024)
				destx = x++;
				break;
		}
		
		/* coords are now in dropx, dropy, destx, desty */
		/* put them in high/low order */
		if (dropx > destx)
		{
			x = dropx;
			dropx = destx;
			destx = x;
		}
		if (dropx > destx)
		{
			x = dropx;
			dropx = destx;
			destx = y;
		}
		
		/*iterate over the distance, dropping a tile every other block */
		x = dropx;
		y = dropy;
		while (x <= destx && y <= desty)
		{
			Brick *brick = amalloc(sizeof(*brick));
			LLAdd(bricks, brick);
			
			brick->x1 = brick->x2 = x;
			brick->y1 = brick->y2 = y;
			
			if (destx != dropx)
				x+=2;
			else
				y+=2;
		}
	}
}

local Ibrickhandler interface =
{
	INTERFACE_HEAD_INIT(I_BRICK_HANDLER, "hs_brick")
	HandleBrick
};

EXPORT const char info_hs_bricks[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_bricks(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		if (!cfg || !lm || !mapdata || !items || !prng)
			return MM_FAIL;

		mm->RegInterface(&interface, ALLARENAS);
		
		mm->RegCallback(CB_DOBRICKMODE, doBrickModeCallback, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
			return MM_FAIL;
			
		mm->UnregCallback(CB_DOBRICKMODE, doBrickModeCallback, ALLARENAS);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(prng);
		return MM_OK;
	}
	return MM_FAIL;
}


