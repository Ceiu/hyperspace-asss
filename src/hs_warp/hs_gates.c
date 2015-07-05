#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_shipnames.h"
#include "selfpos.h"

typedef enum JumpGateStatus
{
	OFF,
	POWER_UP_OUTGOING, ACTIVE_OUTGOING, POWER_DOWN_OUTGOING,
	POWER_UP_INCOMING, ACTIVE_INCOMING, POWER_DOWN_INCOMING
} JumpGateStatus;

typedef struct JumpGate
{
	int x1, y1, x2, y2;

	JumpGateStatus status;

	ticks_t lastChange;

	struct JumpGate *dest;
	int destID;

	int centerOutput;

	int imageID;

	char rotationCount;
	char rotations[40];
} JumpGate;

typedef struct adata
{
	int on;

	Target target;

	int gateCount;
	JumpGate *gateList;

	int activeTime;
	int powerUpTime;
	int powerDownTime;

	int offID;
	int powerUpOutgoingID;
	int activeOutgoingID;
	int powerDownOutgoingID;
	int powerUpIncomingID;
	int activeIncomingID;
	int powerDownIncomingID;

	int restartActiveOnWarp;

	int cfg_AllowPriv;
} adata;

typedef struct pdata
{
	ticks_t nextWarp;
} pdata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Imainloop *ml;
local Inet *net;
local Iarenaman *aman;
local Iconfig *cfg;
local Iplayerdata *pd;
local Iselfpos *selfpos;
local Iobjects *objects;
local Iprng *prng;

local int pdkey;
local int adkey;

local void powerUpGate(Arena *a, JumpGate *gate)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	gate->status = POWER_UP_OUTGOING;
	gate->dest->status = POWER_UP_INCOMING;
	gate->lastChange = current_ticks();

	objects->Image(&(ad->target), gate->imageID, ad->powerUpOutgoingID);
	objects->Image(&(ad->target), gate->dest->imageID, ad->powerUpIncomingID);
	objects->Toggle(&(ad->target), gate->imageID, 1);
	objects->Toggle(&(ad->target), gate->dest->imageID, 1);
}

local void activateGate(Arena *a, JumpGate *gate)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	gate->status = ACTIVE_OUTGOING;
	gate->dest->status = ACTIVE_INCOMING;
	gate->lastChange = current_ticks();

	objects->Image(&(ad->target), gate->imageID, ad->activeOutgoingID);
	objects->Image(&(ad->target), gate->dest->imageID, ad->activeIncomingID);
	objects->Toggle(&(ad->target), gate->imageID, 1);
	objects->Toggle(&(ad->target), gate->dest->imageID, 1);
}

local void powerDownGate(Arena *a, JumpGate *gate)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	gate->status = POWER_DOWN_OUTGOING;
	gate->dest->status = POWER_DOWN_INCOMING;
	gate->lastChange = current_ticks();

	objects->Image(&(ad->target), gate->imageID, ad->powerDownOutgoingID);
	objects->Image(&(ad->target), gate->dest->imageID, ad->powerDownIncomingID);
	objects->Toggle(&(ad->target), gate->imageID, 1);
	objects->Toggle(&(ad->target), gate->dest->imageID, 1);
}

local void deactivateGate(Arena *a, JumpGate *gate)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	gate->status = OFF;
	gate->dest->status = OFF;
	gate->lastChange = current_ticks();

	objects->Image(&(ad->target), gate->imageID, ad->offID);
	objects->Image(&(ad->target), gate->dest->imageID, ad->offID);
	objects->Toggle(&(ad->target), gate->imageID, 1);
	objects->Toggle(&(ad->target), gate->dest->imageID, 1);
}

local void warpPlayer(Player *p, JumpGate *gate, struct C2SPosition *pos, int radius)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);

	int newx, newy, v_x, v_y;
	int angleID, newAngle;

	int srcCenterX = ((gate->x1 + gate->x2 + 1) << 3); //average from tiles to pixels (<<4 >>1)
	int srcCenterY = ((gate->y1 + gate->y2 + 1) << 3);
	int destCenterX = ((gate->dest->x1 + gate->dest->x2 + 1) << 3);
	int destCenterY = ((gate->dest->y1 + gate->dest->y2 + 1) << 3);

	//find new x&y
	if (gate->dest->centerOutput)
	{
		newx = destCenterX;
		newy = destCenterY;
	}
	else
	{
		newx = (pos->x - srcCenterX) + destCenterX;
		newy = (pos->y - srcCenterY) + destCenterY;
	}

	//find the new angle
	if (gate->rotationCount)
	{
		angleID = prng->Number(0, gate->rotationCount);
		newAngle = (gate->rotations[angleID]);
		double angle = (-(pos->rotation - newAngle)) * M_PI / 20;

		v_x = (int)((pos->xspeed) * cos(angle) - (pos->yspeed) * sin(angle));
		v_y = (int)((pos->xspeed) * sin(angle) + (pos->yspeed) * cos(angle));
	}
	else
	{
		newAngle = pos->rotation;

		v_x = pos->xspeed;
		v_y = pos->yspeed;
	}



	//make sure the new position is inside the gate.
	if (newx - radius - 2 < gate->dest->x1 << 4)
	{
		newx = (gate->dest->x1 << 4) + radius + 2;
	}
	else if (((gate->dest->x2 + 2) << 4) - 1 < newx + radius + 2)
	{
		newx = (((gate->dest->x2 + 1) << 4) - 1) - radius - 2;
	}

	if (newy - radius - 2 < gate->dest->y1 << 4)
	{
		newy = (gate->dest->y1 << 4) + radius + 2;
	}
	else if (((gate->dest->y2) << 4) - 1 < newy + radius + 2)
	{
		newy = (((gate->dest->y2 + 1) << 4) - 1) - radius - 2;
	}

	selfpos->WarpPlayer(p, newx, newy, v_x, v_y, newAngle, 0);
	data->nextWarp = current_ticks() + 100;

	if (ad->restartActiveOnWarp)
	{
		gate->lastChange = current_ticks();
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

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if(!ad->cfg_AllowPriv && p->p_freq >= 100)
		return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (current_ticks() < data->nextWarp)
		return; //cooldown hasn't expired

	radius = cfg->GetInt(arena->cfg, shipNames[p->p_ship], "Radius", 14);
	if (radius == 0) radius = 14;

	for (i = 0; i < ad->gateCount; i++)
	{
		JumpGate *gate = &(ad->gateList[i]);

		if (gate->dest == NULL)
			continue;

		if ((gate->x1 << 4) - radius <= pos->x && pos->x < ((gate->x2 + 1) << 4) + radius)
		{
			if ((gate->y1 << 4) - radius <= pos->y && pos->y < ((gate->y2 + 1) << 4) + radius)
			{
				if (gate->status == ACTIVE_OUTGOING)
				{
					//warp them
					warpPlayer(p, gate, pos, radius);
					return; //don't warp them more than once
				}
				else if (gate->status == OFF && gate->dest->status == OFF)
				{
					powerUpGate(arena, gate);
				}
			}
		}
	}
}

local int timerCallback(void *clos)
{
	Arena *a = clos;
	adata *ad = P_ARENA_DATA(a, adkey);
	int i;

	for (i = 0; i < ad->gateCount; i++)
	{
		JumpGate *gate = &ad->gateList[i];

		ticks_t diff = current_ticks() - gate->lastChange;

		switch (gate->status)
		{
			case POWER_UP_OUTGOING:
				if (diff >= ad->powerUpTime)
					activateGate(a, gate);
				break;
			case ACTIVE_OUTGOING:
				if (diff >= ad->activeTime)
					powerDownGate(a, gate);
				break;
			case POWER_DOWN_OUTGOING:
				if (diff >= ad->powerDownTime)
					deactivateGate(a, gate);
				break;
			default:
				//do nothing
				break;
		}
	}

	return TRUE;
}

local void loadArenaConfig(Arena *a)
{
	char buf[256];
	int i, j;
	struct adata *ad = P_ARENA_DATA(a, adkey);
	const char *rotations;
	const char *tmp = NULL;

	/* cfghelp: JumpGate:GateCount, arena, int, def: 0, mod: hs_jumpgate
	 * The number of JumpGates to load from the config file */
	ad->gateCount = cfg->GetInt(a->cfg, "Jumpgate", "GateCount", 0);

	//init the array
	ad->gateList = amalloc(sizeof(JumpGate) * ad->gateCount);

	/* cfghelp: JumpGate:OffID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the off gate image */
	ad->offID = cfg->GetInt(a->cfg, "Jumpgate", "OffID", 0);
	/* cfghelp: JumpGate:PowerUpOutID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the powering up outgoing gate */
	ad->powerUpOutgoingID = cfg->GetInt(a->cfg, "Jumpgate", "PowerUpOutID", 0);
	/* cfghelp: JumpGate:ActiveOutID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the active outgoing gate */
	ad->activeOutgoingID = cfg->GetInt(a->cfg, "Jumpgate", "ActiveOutID", 0);
	/* cfghelp: JumpGate:PowerDownOutID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the powering down outgoing gate */
	ad->powerDownOutgoingID = cfg->GetInt(a->cfg, "Jumpgate", "PowerDownOutID", 0);
	/* cfghelp: JumpGate:PowerUpInID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the powering up incoming gate */
	ad->powerUpIncomingID = cfg->GetInt(a->cfg, "Jumpgate", "PowerUpInID", 0);
	/* cfghelp: JumpGate:ActiveInID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the active incoming gate */
	ad->activeIncomingID = cfg->GetInt(a->cfg, "Jumpgate", "ActiveInID", 0);
	/* cfghelp: JumpGate:PowerDownInID, arena, int, def: 0, mod: hs_jumpgate
	 * ID of the powering down incoming gate */
	ad->powerDownIncomingID = cfg->GetInt(a->cfg, "Jumpgate", "PowerDownInID", 0);

	/* cfghelp: JumpGate:RestartOnActiveWarp, arena, int, def: 1, mod: hs_jumpgate
	 * Whether the timer is restarted when a player passes through */
	ad->restartActiveOnWarp = cfg->GetInt(a->cfg, "Jumpgate", "RestartOnActiveWarp", 1);

	/* cfghelp: JumpGate:PowerUpTime, arena, int, def: 300, mod: hs_jumpgate
	 * How long gates stay in power up */
	ad->powerUpTime = cfg->GetInt(a->cfg, "Jumpgate", "PowerUpTime", 300);
	/* cfghelp: JumpGate:ActiveTime, arena, int, def: 1000, mod: hs_jumpgate
	 * How long gates stay active */
	ad->activeTime = cfg->GetInt(a->cfg, "Jumpgate", "ActiveTime", 1000);
	/* cfghelp: JumpGate:PowerDownTime, arena, int, def: 200, mod: hs_jumpgate
	 * How long gates stay in power down */
	ad->powerDownTime = cfg->GetInt(a->cfg, "Jumpgate", "PowerDownTime", 200);

	/* cfghelp: Hyperspace:PrivFTL, arena, int, def: 1, mod: hs_warp
	 * Whether private freqs can use faster than light travel */
	ad->cfg_AllowPriv = cfg->GetInt(a->cfg, "Hyperspace", "PrivFTL", 1);

	for (i = 0; i < ad->gateCount; i++)
	{
		ad->gateList[i].status = OFF;
		ad->gateList[i].lastChange = 0;

		/* cfghelp: JumpPoint:Gate0x1, arena, int, def: 0, mod: hs_jumpgate
		 * Top left corner of gate */
		sprintf(buf, "Gate%dx1", i);
		ad->gateList[i].x1 = cfg->GetInt(a->cfg, "JumpGate", buf, 1);
		/* cfghelp: JumpGate:Gate0y1, arena, int, def: 0, mod: hs_jumpgate
		 * Top left corner of gate */
		sprintf(buf, "Gate%dy1", i);
		ad->gateList[i].y1 = cfg->GetInt(a->cfg, "JumpGate", buf, 1);
		/* cfghelp: JumpGate:Gate0x2, arena, int, def: 0, mod: hs_jumpgate
		 * Bottom right corner of gate */
		sprintf(buf, "Gate%dx2", i);
		ad->gateList[i].x2 = cfg->GetInt(a->cfg, "JumpGate", buf, 2);
		/* cfghelp: JumpGate:Gate0y2, arena, int, def: 0, mod: hs_jumpgate
		 * Bottom right corner of gate */
		sprintf(buf, "Gate%dy2", i);
		ad->gateList[i].y2 = cfg->GetInt(a->cfg, "JumpGate", buf, 2);
		/* cfghelp: JumpGate:Gate0DestGate, arena, int, def: 0, mod: hs_jumpgate
		 * The ID of the JumpGate to warp players into */
		sprintf(buf, "Gate%dDestGate", i);
		ad->gateList[i].destID = cfg->GetInt(a->cfg, "JumpGate", buf, 0);
		/* cfghelp: JumpGate:Gate0ImageID, arena, int, def: 0, mod: hs_jumpgate
		 * The ID of the JumpGate to warp players into */
		sprintf(buf, "Gate%dImageID", i);
		ad->gateList[i].imageID = cfg->GetInt(a->cfg, "JumpGate", buf, 0);
		/* cfghelp: JumpGate:Gate0CenterOutput, arena, int, def: 0, mod: hs_jumpgate
		 * If players should warp out from the center of this gate */
		sprintf(buf, "Gate%dCenterOutput", i);
		ad->gateList[i].centerOutput = cfg->GetInt(a->cfg, "JumpGate", buf, 0);


		/* cfghelp: JumpGate:Gate0Rotations, arena, int, def: 0-39, mod: hs_jumpgate
		 * Comma seperated list, no duplicates, no spaces.
		 * Can have ranges like 0-2,8-12,18-22,28-32,38-39 */
		sprintf(buf, "Gate%dRotations", i);
		rotations = cfg->GetStr(a->cfg, "JumpGate", buf);

		j = 0;
		while (strsplit(rotations, ",", buf, sizeof(buf), &tmp))
		{
			char *dashLoc = strchr(buf, '-');
			if (dashLoc == NULL)
			{
				//no range
				int direction = atoi(buf);
				ad->gateList[i].rotations[j] = direction;
				j++;
			}
			else
			{
				//null terminate the first number
				*dashLoc = '\0';

				//make sure a number follows the dash
				dashLoc++;
				if (*dashLoc != '\0')
				{
					int rangeStart = atoi(buf);
					int rangeEnd = atoi(dashLoc);
					if (rangeEnd > rangeStart)
					{
						int k;
						for (k = 0; k <= rangeEnd - rangeStart; k++)
						{
							ad->gateList[i].rotations[j + k] = rangeStart + k;
						}

						j += rangeEnd - rangeStart + 1;
					}
					else
					{
						lm->LogA(L_ERROR, "<hs_gates>", a, "bad range on gate %d", i);
					}
				}
				else
				{
					lm->LogA(L_ERROR, "<hs_gates>", a, "bad parse on gate %d", i);
				}
			}
		}
		ad->gateList[i].rotationCount = j;
	}

	for (i = 0; i < ad->gateCount; i++)
	{
		int id = ad->gateList[i].destID;
		if (id < ad->gateCount && 0 <= id)
		{
			ad->gateList[i].dest = &(ad->gateList[id]);
		}
		else
		{
			ad->gateList[i].dest = NULL;
		}
	}
}

local void unloadArenaConfig(Arena *a)
{
	struct adata *ad = P_ARENA_DATA(a, adkey);

	afree(ad->gateList);
}

EXPORT const char info_hs_gates[] = "v3.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_gates(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		objects = mm->GetInterface(I_OBJECTS, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);

		if (!lm || !ml || !net || !aman || !cfg || !pd || !selfpos || !objects || !prng) return MM_FAIL;

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
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(objects);
		mm->ReleaseInterface(prng);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 1;
		ad->target.type = T_ARENA;
		ad->target.u.arena = arena;

		ml->SetTimer(timerCallback, 5, 5, arena, arena);

		loadArenaConfig(arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 0;

		ml->ClearTimer(timerCallback, arena);

		unloadArenaConfig(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
