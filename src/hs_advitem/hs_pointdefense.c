#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hscore_database.h"

#include "packets/kill.h"

#define PACKET_INTERVAL 50
#define DETACH_TIME 500

typedef struct TurretData
{
	Player *owner;
	Player *fake;

	int interval;
	int range;
	int aim; //Whether or not we use a proper aim algorithm.

	int weaponSpeed;

	ticks_t lastShot;
	ticks_t lastPacket;
	ticks_t detached;

	struct C2SPosition pos;
	struct C2SPosition outgoing;
} TurretData;

typedef struct adata
{
	int on;

	LinkedList turret_list;
} adata;

typedef struct pdata
{
	TurretData *turretData;
} pdata;

local Imodman *mm;
local Ilogman *lm;
local Ifake *fake;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmdman;
local Ichat *chat;
local Iconfig *cfg;
local Imainloop *ml;
local Iplayerdata *pd;
local Ihscoreitems *items;
local Inet *net;
local Imapdata *map;
local Ihscoredatabase *db;

local int adkey;
local int pdkey;

local pthread_mutex_t generalmtx = PTHREAD_MUTEX_INITIALIZER;
static inline void lock()
{
	pthread_mutex_lock(&generalmtx);
}

static inline void unlock()
{
	pthread_mutex_unlock(&generalmtx);
}

/////////////////////////////////////////////////
// Collision detection (adapted from hs_blink) //
/////////////////////////////////////////////////
local int fitsOnMap(Arena *arena, int x_, int y_)
{
    int x = x_ >> 4;
    int y = y_ >> 4;

    enum map_tile_t tile = map->GetTile(arena, x, y);

    if ((tile >= 1 && tile <= 161)
       || (tile >= 192 && tile <= 240)
       || (tile >= 243 && tile <= 251))
    {
        return 0;
    }

	return 1;
}

// Lower is more precise but takes longer to calculate
#define PATHCLEAR_INCREASE (8)
local int isClearPath(Arena *arena, int x1, int y1, int x2, int y2)
{
	int i, dx, dy, numpixels;
	int d, dinc1, dinc2;
	int x, xinc1, xinc2;
	int y, yinc1, yinc2;

	/* calculate deltax and deltay for initialisation */
	dx = x2 - x1;
	dy = y2 - y1;
	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initialize all vars based on which is the independent variable */
	if (dx > dy)
	{
		/* x is independent variable */
		numpixels = dx + 1;
		d = (2 * dy) - dx;
		dinc1 = dy << 1;
		dinc2 = (dy - dx) << 1;
		xinc1 = 1;
		xinc2 = 1;
		yinc1 = 0;
		yinc2 = 1;
	}
	else
	{
		/* y is independent variable */
		numpixels = dy + 1;
		d = (2 * dx) - dy;
		dinc1 = dx << 1;
		dinc2 = (dx - dy) << 1;
		xinc1 = 0;
		xinc2 = 1;
		yinc1 = 1;
		yinc2 = 1;
	}

	/* make sure x and y move in the right directions */
	if (x1 > x2)
	{
		xinc1 = - xinc1;
		xinc2 = - xinc2;
	}
	if (y1 > y2)
	{
		yinc1 = - yinc1;
		yinc2 = - yinc2;
	}

 	dinc1 *= PATHCLEAR_INCREASE;
	dinc2 *= PATHCLEAR_INCREASE;
	xinc1 *= PATHCLEAR_INCREASE;
	xinc2 *= PATHCLEAR_INCREASE;
	yinc1 *= PATHCLEAR_INCREASE;
	yinc2 *= PATHCLEAR_INCREASE;

	/* start drawing at */
	x = x1;
	y = y1;

	/* trace the line */
	for(i = 1; i < numpixels; i+= PATHCLEAR_INCREASE)
	{
		if (!fitsOnMap(arena, x, y))
			return 0;

		/* bresenham stuff */
		if (d < 0)
		{
			d = d + dinc1;
			x = x + xinc1;
			y = y + yinc1;
		}
		else
		{
			d = d + dinc2;
			x = x + xinc2;
			y = y + yinc2;
		}
	}

	return 1;
}

local Player *getBestTarget(TurretData *data)
{
	int bestDist;
	Player *best = NULL;

	Player *i;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING && i->arena == data->owner->arena && i->p_freq != data->owner->p_freq && i->p_ship != SHIP_SPEC && !i->flags.is_dead && IS_HUMAN(i))
		{
			if (!(i->position.status & STATUS_CLOAK) || (data->owner->position.status & STATUS_XRADAR))
			{
				int dist = (i->position.x - data->pos.x) * (i->position.x - data->pos.x) + (i->position.y - data->pos.y) * (i->position.y - data->pos.y);
				if (dist < data->range)
				{
					if (!best || dist < bestDist)
					{
                        if (isClearPath(data->owner->arena, data->pos.x, data->pos.y, i->position.x, i->position.y))
                        {
                            best = i;
						    bestDist = dist;
                        }
					}
				}
			}
		}
	pd->Unlock();

	return best;
}

/* quick integer square root */
local long lhypot (register long dx, register long dy)
{
	register unsigned long r, dd;

	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initial hypotenuse guess (from Gems) */
	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));

	if (r == 0) return (long)r;

	/* converge 3 times */
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}

local inline unsigned char fireAngle(double x, double y)
{
	/* [-Pi, Pi] + Pi -> [0, 2Pi] */
	double angle = atan2(y, x) + M_PI;

	int a = round(angle * 40.0 / (2.0 * M_PI) + 30);

	/* 0 degrees is +y-axis for us, 0 degrees is +x-axis for atan2 */
	return (unsigned char) a % 40;
}

local inline int findFiringAngle (double srcx, double srcy, double dstx, double dsty, double dxspeed, double dyspeed, double projspeed, int aim)
{
	double bestdx = dstx - srcx;
	double bestdy = dsty - srcy;

	if (aim)
	{
	    double dx, dy, err;
	    double besterr = 20000;
	    double tt = 10, pt;

	    do
	    {
		    dx = (dstx + dxspeed * tt / 1000) - srcx;
		    dy = (dsty + dyspeed * tt / 1000) - srcy;

            pt = lhypot(dx, dy) * 1000 / projspeed;
		    err = abs(pt - tt);

		    if (err < besterr)
		    {
			    besterr = err;
			    bestdx = dx;
			    bestdy = dy;
		    }
		    else if (err > besterr)
			    break;

		    tt += 10;
	    } while(pt > tt && tt <= 250);
    }

	// get angle
	return fireAngle(bestdx, bestdy);
}

local void attachPlayer(Player *p, Player *t)
{
	if(p && t)
	{
		// Attach to the target, or whoever the target is attached to...
		p->p_attached = (t->p_attached == -1 ? t->pid : t->p_attached);

		// Send packet...
		struct SimplePacket objPacket = { S2C_TURRET, p->pid, p->p_attached };
		net->SendToArena(p->arena, NULL, (byte*)&objPacket, 5, NET_RELIABLE);
	}
}

local void detachPlayer(Player *p)
{
	if(p)
	{
		p->p_attached = -1;

		struct SimplePacket objPacket = { S2C_TURRET, p->pid, -1};
		net->SendToArena(p->arena, NULL, (byte*)&objPacket, 5, NET_RELIABLE);
	}
}

local void sendPosPacket(TurretData *data)
{
	int wep = data->outgoing.weapon.type;

	int deltaTime = current_ticks() - data->pos.time;
	int v_x = data->pos.xspeed;
	int v_y = data->pos.yspeed;
	int x = data->pos.x + deltaTime * v_x / 1000;
	int y = data->pos.y + deltaTime * v_y / 1000;

	data->outgoing.time = current_ticks();
	data->outgoing.x = x;
	data->outgoing.y = y;
	data->outgoing.xspeed = v_x;
	data->outgoing.yspeed = v_y;
	data->outgoing.status = data->pos.status;

	data->outgoing.weapon.type = W_NULL;
	game->FakePosition(data->fake, &data->outgoing, sizeof(data->outgoing));
	data->outgoing.weapon.type = wep;
	data->lastPacket = current_ticks();
}

local void fireAtTarget(TurretData *data, Player *target)
{
	int rotation = -1;

	if (target)
	{
		int deltaTime = current_ticks() - data->pos.time;

		int v_x = data->pos.xspeed;
		int v_y = data->pos.yspeed;

		int x = data->pos.x + deltaTime * v_x / 1000;
		int y = data->pos.y + deltaTime * v_y / 1000;

		rotation = findFiringAngle(x, y,
                 target->position.x, target->position.y,
                 target->position.xspeed - v_x,
                 target->position.yspeed - v_y,
                 data->weaponSpeed, data->aim);

		data->outgoing.time = current_ticks();

		data->outgoing.x = x;
		data->outgoing.y = y;
		data->outgoing.xspeed = v_x;
		data->outgoing.yspeed = v_y;
		data->outgoing.status = data->pos.status;
	}

	if (rotation != -1)
	{
		data->outgoing.rotation = rotation;
		game->FakePosition(data->fake, &data->outgoing, sizeof(data->outgoing));
		data->lastShot = current_ticks();
		data->lastPacket = current_ticks();
	}
	else
	{
		if (data->lastPacket + PACKET_INTERVAL <= current_ticks())
		{
			sendPosPacket(data);
		}

		if (target)
			lm->LogP(L_DRIVEL, "hs_pointdefense", data->owner, "no firing solution for target=%s", target->name);
	}
}

local void addTurret(Player *p)
{
	pdata *pdata = PPDATA(p, pdkey);
	struct adata *ad = P_ARENA_DATA(p->arena, adkey);
	int weapon;

	TurretData *turretData = amalloc(sizeof(*turretData));
	turretData->owner = p;

	turretData->outgoing.type = C2S_POSITION;
	turretData->outgoing.bounty = 0;
	turretData->outgoing.energy = 31337;

	weapon = items->getPropertySum(p, p->p_ship, "pdweapon", 0);
	if (weapon == 0)
	{
		turretData->outgoing.weapon.type = W_BULLET;
		turretData->outgoing.weapon.level = 3;
		turretData->outgoing.weapon.shrapbouncing = 0;
		turretData->outgoing.weapon.shraplevel = 0;
		turretData->outgoing.weapon.shrap = 0;
		turretData->outgoing.weapon.alternate = 1;
		turretData->interval = 27;
		turretData->range = 160000; // 25 tiles;
		turretData->aim = 0;
	}
	else
	{
		turretData->outgoing.weapon.type = weapon;
		turretData->outgoing.weapon.level = items->getPropertySum(p, p->p_ship, "pdlevel", 0);
		turretData->outgoing.weapon.shrapbouncing = items->getPropertySum(p, p->p_ship, "pdshrapbounce", 0);
		turretData->outgoing.weapon.shraplevel = items->getPropertySum(p, p->p_ship, "pdshraplevel", 0);
		turretData->outgoing.weapon.shrap = items->getPropertySum(p, p->p_ship, "pdshrap", 0);
		turretData->outgoing.weapon.alternate = items->getPropertySum(p, p->p_ship, "pdalt", 0);
		turretData->interval = items->getPropertySum(p, p->p_ship, "pddelay", 0);
		turretData->range = items->getPropertySum(p, p->p_ship, "pdrange", 0);
		turretData->aim = (items->getPropertySum(p, p->p_ship, "pdaim", 0) > 0);
	}

	turretData->fake = fake->CreateFakePlayer("<pointdefense>", p->arena, SHIP_SHARK, p->p_freq);

	if (turretData->outgoing.weapon.type == W_BULLET || turretData->outgoing.weapon.type == W_BOUNCEBULLET)
	{
		turretData->weaponSpeed = cfg->GetInt(p->arena->cfg, "Shark", "BulletSpeed", 1);
	}
	else
	{
		turretData->weaponSpeed = cfg->GetInt(p->arena->cfg, "Shark", "BombSpeed", 1);
	}

	attachPlayer(turretData->fake, p);

	pdata->turretData = turretData;
	LLAdd(&ad->turret_list, turretData);
}

local void removeTurret(Player *p)
{
	pdata *pdata = PPDATA(p, pdkey);
	struct adata *ad = P_ARENA_DATA(p->arena, adkey);

	TurretData *turretData = pdata->turretData;

	if (!turretData)
	{
		return;
	}

	if (turretData->fake)
	{
		fake->EndFaked(turretData->fake);
	}

	LLRemove(&ad->turret_list, turretData);
	afree(turretData);

	pdata->turretData = NULL;
}

local helptext_t pd_help =
"Targets: player\n"
"Args: none\n"
"Adds a point defense turret to the targeted player.";

local void Cpd(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER && target->u.p->p_ship != SHIP_SPEC)
	{
		lock();
		pdata *pdata = PPDATA(target->u.p, pdkey);

		if (!pdata->turretData)
		{
			addTurret(target->u.p);
		}
		else
		{
			removeTurret(target->u.p);
		}

		unlock();
	}
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);

	if (len < 22)
	{
		return;
	}

	/* handle common errors */
	if (!arena || !ad->on)
	{
	   	return;
	}

	if (!data->turretData)
	{
		return;
	}

	lock();
	//copy the packet into the turret data
	memcpy(&data->turretData->pos, pos, sizeof(*pos));
	unlock();
}

local void handle_death(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	lock();
	pdata *data = PPDATA(killed, pdkey);

	if (!data->turretData)
	{
		unlock();
		return;
	}

	struct KillPacket objPacket;

	objPacket.type = S2C_KILL;
	objPacket.green = (u8) (*green & 0xFF);
	objPacket.killer = killer->pid;
	objPacket.killed = data->turretData->fake->pid;
	objPacket.bounty = 10; // fix me later
	objPacket.flags = 0; // we'll never have flags, I think.

	net->SendToArena(arena, NULL, (byte*)&objPacket, sizeof(objPacket), NET_RELIABLE);
	unlock();
}

local void handle_turretkickoff(Player *p, byte *pkt, int len)
{
	lock();
	pdata *data = PPDATA(p, pdkey);

	if (!data->turretData)
	{
		unlock();
		return;
	}

	detachPlayer(data->turretData->fake);

	data->turretData->detached = current_ticks();
	chat->SendMessage(p, "Point Defense temporarily disabled.");
	unlock();
}

local void handle_attachto(Player *p, byte *pkt, int len)
{
	lock();
	pdata *data = PPDATA(p, pdkey);
	i16 target = *((i16*)(pkt + 1));

	// Make sure the player has PD...
	if(!data->turretData)
	{
		unlock();
		return;
	}

	if(target == -1 && data->turretData->fake->p_attached != p->pid)
	{
		// The player is detaching from another player; take PD with them...
		detachPlayer(data->turretData->fake);

		// Don't attach immediately if PD is still disabled...
		if(!data->turretData->detached)
			attachPlayer(data->turretData->fake, p);

	} else if(data->turretData->detached) {

		// The player is attaching to another player. Reset disabled time.
		data->turretData->detached = current_ticks();
	}
	unlock();
}

local int timerCallback(void *clos)
{
	lock();
	Arena *arena = clos;
	adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;



	for (link = LLGetHead(&ad->turret_list); link; link = link->next)
	{
		TurretData *data = link->data;

		//sanity check and error adjustment. commands like ?attach do not behave well with this module, so adjust attachments as necessary.
		if (!data->detached &&	(((data->owner->p_attached != -1) && (data->fake->p_attached != data->owner->p_attached))
							||	((data->owner->p_attached == -1) && (data->fake->p_attached != data->owner->pid))))
		{
			data->detached = current_ticks();
			detachPlayer(data->fake);
		}

		if(data->detached)
		{
			// Hide turret while detached...
			if(current_ticks() - data->detached > DETACH_TIME)
			{
				// reattach and set detach time to 0.
				data->detached = 0;

				attachPlayer(data->fake, data->owner);
				//struct SimplePacket objPacket = { S2C_TURRET, data->fake->pid, data->owner->pid};
				//net->SendToArena(arena, NULL, (byte*)&objPacket, 5, NET_RELIABLE);
			}
		}

		else if (!data->lastShot || TICK_DIFF(current_ticks(), data->lastShot) >= data->interval)
		{
			Player *target = getBestTarget(data);

			if (target && !(data->owner->position.status & STATUS_CLOAK) && !(data->owner->position.status & STATUS_SAFEZONE))
			{
				fireAtTarget(data, target);
			}
			else if (data->lastPacket + PACKET_INTERVAL <= current_ticks())
			{
				sendPosPacket(data);
			}
		}
		else if (data->lastPacket + PACKET_INTERVAL <= current_ticks())
		{
			sendPosPacket(data);
		}
	}

	unlock();
	return TRUE;
}

local void shipFreqChangeCallback(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	lock();

	//get rid of the current one
	removeTurret(p);

	if (newship != SHIP_SPEC)
	{
		if (items->getPropertySum(p, newship, "pointdefense", 0))
		{
			addTurret(p);
		}
	}
	unlock();
}

local void itemsChangedCallback(Player *p, ShipHull *hull)
{
	if (db->getPlayerCurrentHull(p) != hull) {
			return;
	}


  if (!items->getPropertySumOnHull(p, hull, "pointdefense", 0))
  {
      removeTurret(p);
  }
	else
	{
		pdata *pdata = PPDATA(p, pdkey);
		if (!pdata->turretData)
		{
				addTurret(p);
		}
	}
}

local void playerActionCallback(Player *p, int action, Arena *arena)
{
	Link *link, *next;
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (action == PA_ENTERARENA)
	{
		lock();
		if (p->p_ship != SHIP_SPEC)
		{
			if (items->getPropertySum(p, p->p_ship, "pointdefense", 0))
			{
				addTurret(p);
			}
		}
		unlock();
	}
	else if (action == PA_LEAVEARENA)
	{
		lock();
		removeTurret(p);

		if (p->type == T_FAKE)
		{
			for (link = LLGetHead(&ad->turret_list); link; link = next)
			{
				next = link->next;

				TurretData *data = link->data;
				if (data->fake == p)
				{
					removeTurret(data->owner);
				}
			}
		}
		unlock();
	}

}

local void unloadTurrets(Arena *a)
{
	struct adata *ad = P_ARENA_DATA(a, adkey);
	Link *link;

	for (link = LLGetHead(&ad->turret_list); link; link = link->next)
	{
		TurretData *data = link->data;
		fake->EndFaked(data->fake);
		afree(data);
	}

	LLEmpty(&ad->turret_list);
}

EXPORT const char info_hs_pointdefense[] = "v1.2 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_pointdefense(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		map = mm->GetInterface(I_MAPDATA, ALLARENAS);
		db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !fake || !aman || !game || !cmdman || !cfg || !ml || !pd || !items || !net || !db) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);
		net->AddPacket(C2S_TURRETKICKOFF, handle_turretkickoff);
		net->AddPacket(C2S_ATTACHTO, handle_attachto);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		net->RemovePacket(C2S_TURRETKICKOFF, handle_turretkickoff);
		net->RemovePacket(C2S_ATTACHTO, handle_attachto);

		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(fake);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmdman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(map);
		mm->ReleaseInterface(db);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		lock();
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		LLInit(&ad->turret_list);
		ad->on = 1;

		mm->RegCallback(CB_PLAYERACTION, playerActionCallback, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCallback, arena);
		mm->RegCallback(CB_KILL, handle_death, arena);

		ml->SetTimer(timerCallback, 1, 1, arena, arena);

		cmdman->AddCommand("pd", Cpd, arena, pd_help);

		unlock();
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		lock();
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ad->on = 0;

		ml->ClearTimer(timerCallback, arena);

		cmdman->RemoveCommand("pd", Cpd, arena);

		mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCallback, arena);
		mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, arena);
		mm->UnregCallback(CB_KILL, handle_death, arena);

		unloadTurrets(arena);

		unlock();
		return MM_OK;
	}
	return MM_FAIL;
}
