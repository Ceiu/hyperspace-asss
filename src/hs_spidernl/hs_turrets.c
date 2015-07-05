#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_shipnames.h"
#include "hscore_buysell.h"
#include "hscore_items.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "fake.h"
#include "game.h"
#include "packets/kill.h"
#include "watchdamage.h"

// Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Ichat *chat;
local Iconfig *cfg;
local Ihscoredatabase *db;
local Ifake *fake;
local Igame *game;
local Ihscoreitems *items;
local Imapdata *map;
local Imainloop *ml;
local Inet *net;
local Iplayerdata *pd;

typedef struct TurretProperties
{
    char name[20];
    byte ship;

    struct Weapons weapon;
    int minRange;
    int maxRange;
    bool syncGuns;
    bool syncBombs;
    int delay;

	int energy;
	int recharge;

	int rotFreedom; // How many "rotation ticks" the turret can differ from the owner when firing.
} TurretProperties;

typedef struct Turret
{
    TurretProperties properties;
    Player *fake;
    Player *owner;

    int weaponSpeed;
    ticks_t lastShot;
    ticks_t lastPosition;

    bool attached;
    bool detached;
    ticks_t lastDetached;

	int energy;
	bool damaged;

	bool emped;
	int empTicks;
} Turret;

typedef struct ArenaData
{
    pthread_mutex_t arenamtx;
    LinkedList turrets;

    struct
    {
        bool useTurretLimit;
        bool killsToOwner;

        struct
	    {
            bool empBomb;
        } perShip[8];

        int empBombDamage;
        int empShutdownTime;
    } Config;

    bool attached;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

local void lock(ArenaData *adata)
{
    pthread_mutex_lock(&adata->arenamtx);
}
local void unlock(ArenaData *adata)
{
    pthread_mutex_unlock(&adata->arenamtx);
}

#define PlayerData PlayerData_
typedef struct PlayerData
{
    HashTable *turrets; //<ItemName, *Turret(s)>
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Prototypes
local bool withinLimit(Player *p, Item *item, int count, int ship, int shipset);
local int canBuy(Player *p, Item *item, int count, int ship);
local int canSell(Player *p, Item *item, int count, int ship);
local int canGrantItem(Player *p, Player *t, Item *item, int ship, int shipset, int count);
local void editDeath(Arena *arena, Player **killer, Player **killed, int *bounty);

local void pktHandler(Player *p, byte *pkt, int len);

local void ppkCB(Player *p, struct C2SPosition *pos);
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void playerDamageCB(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void shipsetChangedCB(Player *p, int oldshipset, int newshipset);
local void playerActionCB(Player *p, int action, Arena *arena);

local void updatePlayerData(Player *p, ShipHull *hull, bool resetTurrets);
local void appendTurrets(Player *p, LinkedList *list);
local void addTurret(Player *p, Item *item);
local void destroyTurret(Turret *turret, Arena *arena);
local void removeAllTurrets(Player *p);
local void attach(Player *p, Player *t);
local void detach(Player *p);
local void sendPositionPacket(Turret *turret, bool withWeapon, int rotation);
local void readConfig(ConfigHandle ch, ArenaData *adata);

local int fitsOnMap(Arena *arena, int x_, int y_);
local int isClearPath(Arena *arena, int x1, int y1, int x2, int y2);
local long lhypot(register long dx, register long dy);
local inline int getBestFiringAngle(Turret *turret);
local inline unsigned char fireAngle(double x, double y);
local inline int findFiringAngle (double srcx, double srcy, double dstx, double dsty, double dxspeed, double dyspeed, double projspeed);

local int turretUpdateTimer(void *clos);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Advisers - turret limit check
local bool withinLimit(Player *p, Item *item, int count, int ship, int shipset)
{
    db->lock();
    int turretLimit = items->getPropertySumOnShipSet(p, ship, shipset, "turretlimit", 0);
    bool affectsTurretLimit = false;

    Property *prop;
    Link *link;
    FOR_EACH(&item->propertyList, prop, link)
    {
        if (!strcmp(prop->name, "turretlimit"))
        {
            if (prop->value != 0)
            {
                turretLimit += (prop->value * count);
                affectsTurretLimit = true;
            }
        }
    }
    db->unlock();

    if (affectsTurretLimit)
       return turretLimit >= 0;
    return true;
}

local int canBuy(Player *p, Item *item, int count, int ship)
{
    if (!withinLimit(p, item, count, ship, db->getPlayerShipSet(p)))
    {
        chat->SendMessage(p, "Buying %i of item %s would cause your turret limit to fall below 0.", count, item->name);
        return 0;
    }

    return 1;
}
local int canSell(Player *p, Item *item, int count, int ship)
{
    if (!withinLimit(p, item, -count, ship, db->getPlayerShipSet(p)))
    {
        chat->SendMessage(p, "Selling %i of item %s would cause your turret limit to fall below 0.", count, item->name);
        return 0;
    }

    return 1;
}

local Ahscorebuysell buysellAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_BUYSELL)

	canBuy,
	canSell
};

local int canGrantItem(Player *p, Player *t, Item *item, int ship, int shipset, int count)
{
    if (!withinLimit(p, item, count, ship, shipset))
    {
        chat->SendMessage(p, "Cannot grant %i of item %s to player %s: turretlimit would be below 0.", count, item->name, t->name);
        return 0;
    }

    return 1;
}

local Ahscoreitems itemsAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_ITEMS)

    canGrantItem
};

local void editDeath(Arena *arena, Player **killer, Player **killed, int *bounty)
{
    ArenaData *adata = getArenaData(arena);

	Turret *turret;
	Link *link;

	FOR_EACH(&adata->turrets, turret, link)
	{
        if (*killer == turret->fake)
        {
            *killer = turret->owner;
        }
    }
}

local Akill killAdviser =
{
	ADVISER_HEAD_INIT(A_KILL)

	NULL,
	editDeath
};

//Packet handler
local void pktHandler(Player *p, byte *pkt, int len)
{
    if (!IS_HUMAN(p)) return;

    ArenaData *adata = getArenaData(p->arena);
    lock(adata);

    if (!adata->attached) {
       unlock(adata);
       return;
    }

    LinkedList turrets = LL_INITIALIZER;
    appendTurrets(p, &turrets);

    Turret *turret;
    Link *link;

    FOR_EACH(&turrets, turret, link)
    {
        if (turret->detached) {
           turret->lastDetached = current_ticks();
           continue;
        }

        if (turret->fake)
        {
            detach(turret->fake);
            turret->attached = false;

            turret->detached = true;
            turret->lastDetached = current_ticks();
        }
    }

    unlock(adata);
}

//Callbacks
local void ppkCB(Player *p, struct C2SPosition *pos)
{
    if (!IS_HUMAN(p)) return;
    if (p->flags.is_dead) return;

    bool playerGunned = pos->weapon.type == W_BULLET || pos->weapon.type == W_BOUNCEBULLET;
    bool playerBombed = pos->weapon.type == W_BOMB || pos->weapon.type == W_PROXBOMB;

    ArenaData *adata = getArenaData(p->arena);
    lock(adata);

    LinkedList turrets = LL_INITIALIZER;
    appendTurrets(p, &turrets);

    if (LLCount(&turrets))
    {
        Turret *turret;
        Link *link;

        FOR_EACH(&turrets, turret, link)
        {
            if (!turret->fake) continue;
            if (turret->detached) continue;
            if (turret->attached)
            {
                if (turret->properties.weapon.type != W_NULL)
                {
                    if ((turret->properties.syncGuns && playerGunned) ||
                       (turret->properties.syncBombs && playerBombed))
                    {
                        if (TICK_DIFF(current_ticks(), turret->lastShot) >= turret->properties.delay)
                        {
                            sendPositionPacket(turret, true, pos->rotation);
                            turret->lastShot = current_ticks();
                        }
                    }
                }
                continue;
            }

            if (p->p_attached != -1) continue; // Only attach if the player isn't attached to anything himself.

            attach(turret->fake, p);
            turret->attached = true;
        }
    }

    unlock(adata);
}

local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    ArenaData *adata = getArenaData(arena);
    lock(adata);

	LinkedList turrets = LL_INITIALIZER;
    appendTurrets(killed, &turrets);

    Turret *turret;
    Link *link;

    FOR_EACH(&turrets, turret, link)
    {
        if (!turret->fake) continue;

        struct KillPacket kill;

        kill.type = S2C_KILL;
	    kill.green = (u8) (*green & 0xFF);
	    kill.killer = killer->pid;
	    kill.killed = turret->fake->pid;
	    kill.bounty = 10;
	    kill.flags = 0;

	    turret->attached = false;
	    turret->detached = false;

	    turret->energy = turret->properties.energy;
	    turret->damaged = false;
	    turret->emped = false;

	    net->SendToArena(arena, NULL, (byte*)&kill, sizeof(kill), NET_RELIABLE);
    }

	unlock(adata);
}

local void playerDamageCB(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count)
{
    if (!IS_STANDARD(p)) return;
    if (p->flags.is_dead) return;

    ArenaData *adata = getArenaData(arena);

    int damage;
    int level;
    int type;

    bool emp;
    double damageFraction;

    LinkedList turrets = LL_INITIALIZER;
    appendTurrets(p, &turrets);

    int i;
    for (i = 0; i < count; i++)
    {
        type = s2cdamage->damage[i].weapon.type;
        if (type != W_BOMB && type != W_PROXBOMB && type != W_THOR) continue;

        level = s2cdamage->damage[i].weapon.level;
        damage = s2cdamage->damage[i].damage;

        Player *shooter = pd->PidToPlayer(s2cdamage->damage[i].shooteruid);
		if (!shooter) continue;

        emp = adata->Config.perShip[shooter->p_ship].empBomb;

        Turret *turret;
        Link *link;

        lock(adata);

        FOR_EACH(&turrets, turret, link)
        {
            if (turret->attached)
            {
                turret->energy -= damage;
                if (turret->energy < 0)
                {
                    turret->energy = 0;
                    turret->damaged = true;
                }

                if (emp)
                {
                    damageFraction = ((double) damage / adata->Config.empBombDamage);

                    turret->emped = true;
                    turret->empTicks = damageFraction * adata->Config.empShutdownTime;
                }
            }
        }

        unlock(adata);
    }
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    if (!IS_STANDARD(p) || !hull)
	  return;

    if (hull == db->getPlayerCurrentHull(p)) {
	  ArenaData *adata = getArenaData(p->arena);

	  lock(adata);
	  updatePlayerData(p, hull, false);
	  unlock(adata);
	}
}

local void shipsetChangedCB(Player *p, int oldshipset, int newshipset)
{
  if (!IS_STANDARD(p))
	return;

  ArenaData *adata = getArenaData(p->arena);

  if (p->p_ship != SHIP_SPEC) {
	lock(adata);
	updatePlayerData(p, db->getPlayerHull(p, p->p_ship, newshipset), true);
	unlock(adata);
  }
}

local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
  if (!IS_STANDARD(p))
	return;

  ArenaData *adata = getArenaData(p->arena);

  lock(adata);
  updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
  unlock(adata);
}

local void playerActionCB(Player *p, int action, Arena *arena)
{
  if (!IS_STANDARD(p)) return;

  if (action == PA_ENTERARENA)
  {
	  ArenaData *adata = getArenaData(arena);
	  lock(adata);

	  PlayerData *pdata = getPlayerData(p);
	  pdata->turrets = HashAlloc();

	  if (p->p_ship != SHIP_SPEC)
	  {
		  updatePlayerData(p, db->getPlayerShipHull(p, p->p_ship), false);
	  }

	  unlock(adata);
  }
  if (action == PA_LEAVEARENA)
  {
	  ArenaData *adata = getArenaData(arena);

	  lock(adata);

	  removeAllTurrets(p);
	  PlayerData *pdata = getPlayerData(p);
	  HashFree(pdata->turrets);

	  unlock(adata);
  }
}

//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, bool resetTurrets)
{
    if (!hull || hull != db->getPlayerCurrentHull(p)) {
       removeAllTurrets(p);
       return;
    }

    if (resetTurrets)
    {
        removeAllTurrets(p);
    }

	//** Note that the below code isn't quite the best solution to the problem. But hey, it works. **
	Link *link, *link2;

	InventoryEntry *entry;
	ItemTypeEntry *type;

	LinkedList inventoryTurrets = LL_INITIALIZER;
	int turretCount = 0;

	//Iterate through our inventory and look for turrets. If a turret is found,
	//add its InventoryEntry to our LinkedList.
	FOR_EACH(&hull->inventoryEntryList, entry, link)
	{
		FOR_EACH(&entry->item->itemTypeEntries, type, link2)
		{
			if (!strcmp(type->itemType->name, "Turret"))
			{
				turretCount += entry->count;
				LLAdd(&inventoryTurrets, entry);
				break;
			}
		}
	}

	LinkedList turrets = LL_INITIALIZER;
	appendTurrets(p, &turrets);

	if (LLCount(&turrets) == turretCount) return; //Nothing changed.

	//Something did change, so reset all turrets and re-add everything.
	if (!resetTurrets)
	{
		removeAllTurrets(p);
	}

	int i;

	FOR_EACH(&inventoryTurrets, entry, link)
	{
		for (i = 0; i < entry->count; i++)
		{
			addTurret(p, entry->item);
		}
	}
}

local void addTurret(Player *p, Item *item)
{
    Turret *turret = amalloc(sizeof(Turret));
    snprintf(turret->properties.name, 20, "<%s>", item->name);

    LinkedList propertyList = item->propertyList;
    Property *property;
    Link *link;

    turret->properties.rotFreedom = 40;

    FOR_EACH(&propertyList, property, link)
    {
        //General stats
        if (!strcmp(property->name, "turretship")) {
            turret->properties.ship = property->value - 1;
            continue;
        }
        if (!strcmp(property->name, "turretminrange")) {
            turret->properties.minRange = property->value;
            continue;
        }
        if (!strcmp(property->name, "turretmaxrange")) {
            turret->properties.maxRange = property->value;
            continue;
        }
        if (!strcmp(property->name, "turretsyncguns")) {
            turret->properties.syncGuns = (property->value > 0);
            continue;
        }
        if (!strcmp(property->name, "turretsyncbombs")) {
            turret->properties.syncBombs = (property->value > 0);
            continue;
        }
        if (!strcmp(property->name, "turretdelay")) {
            turret->properties.delay = property->value;
            continue;
        }

        //Turret weapon stats
        if (!strcmp(property->name, "turretweapon"))
        {
            turret->properties.weapon.type = property->value;

            //Make sure it makes sense. Otherwise set to W_NULL.
            if (turret->properties.weapon.type <= 0 || turret->properties.weapon.type > 8)
               turret->properties.weapon.type = W_NULL;

            continue;
        }
        if (!strcmp(property->name, "turretlevel")) {
            turret->properties.weapon.level = property->value - 1;
            continue;
        }
        if (!strcmp(property->name, "turretshrpbounce")) {
            turret->properties.weapon.shrapbouncing = property->value;
            continue;
        }
        if (!strcmp(property->name, "turretshraplevel")) {
            turret->properties.weapon.shraplevel = property->value - 1;
            continue;
        }
        if (!strcmp(property->name, "turretshrap")) {
            turret->properties.weapon.shrap = property->value;
            continue;
        }
        if (!strcmp(property->name, "turretalt")) {
            turret->properties.weapon.alternate = property->value;
            continue;
        }

		//Turret energy/recharge
		if (!strcmp(property->name, "turretenergy")) {
            turret->properties.energy = property->value;
            continue;
        }
		if (!strcmp(property->name, "turretrecharge")) {
            turret->properties.recharge = (int) (property->value / 100.0);
			// ^Divide by 100 as the value represents per-second recharge, and we add this value to energy every tick.

            continue;
        }

        if (!strcmp(property->name, "turretrotfreedom")) {
            turret->properties.rotFreedom = property->value;
            continue;
        }
    }

    turret->fake = fake->CreateFakePlayer(turret->properties.name, p->arena, turret->properties.ship, p->p_freq);
    turret->attached = false;
    turret->owner = p;

    turret->energy = turret->properties.energy;
    turret->damaged = false;
    turret->emped = false;

    turret->lastShot = current_ticks();
	turret->lastPosition = current_ticks();

	if (turret->properties.weapon.type == W_BOMB || turret->properties.weapon.type == W_PROXBOMB || turret->properties.weapon.type == W_THOR)
	{
        turret->weaponSpeed = cfg->GetInt(p->arena->cfg, shipNames[turret->properties.ship], "BombSpeed", 1);
    }
	else
	{
        turret->weaponSpeed = cfg->GetInt(p->arena->cfg, shipNames[turret->properties.ship], "BulletSpeed", 1);
    }

    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

    HashAdd(pdata->turrets, item->name, turret);
    LLAdd(&adata->turrets, turret);
}

local void destroyTurret(Turret *turret, Arena *arena)
{
    ArenaData *adata = getArenaData(arena);

    LLRemove(&adata->turrets, turret);

    if (turret->fake) fake->EndFaked(turret->fake);
    afree(turret);
}

local int removeAllTurrets_enum(const char *key, void *val, void *clos)
{
    destroyTurret(val, clos);
    return 1;
}
local void removeAllTurrets(Player *p)
{
    PlayerData *pdata = getPlayerData(p);
    HashEnum(pdata->turrets, removeAllTurrets_enum, p->arena);
}

local int do_appendTurrets(const char *key, void *val, void *clos)
{
    LinkedList *list = (LinkedList*)clos;
    LLAdd(list, val);

    return 0;
}
local void appendTurrets(Player *p, LinkedList *list)
{
    PlayerData *pdata = getPlayerData(p);
    HashEnum(pdata->turrets, do_appendTurrets, list);
}

local void attach(Player *p, Player *t)
{
	if (p && t)
	{
		p->p_attached = (t->p_attached == -1 ? t->pid : t->p_attached);

		struct SimplePacket objPacket = { S2C_TURRET, p->pid, p->p_attached };
		net->SendToArena(p->arena, NULL, (byte*)&objPacket, 5, NET_RELIABLE);
	}
}

local void detach(Player *p)
{
	if (p)
	{
		p->p_attached = -1;

		struct SimplePacket objPacket = { S2C_TURRET, p->pid, -1};
		net->SendToArena(p->arena, NULL, (byte*)&objPacket, 5, NET_RELIABLE);
	}
}

local void sendPositionPacket(Turret *turret, bool withWeapon, int rotation)
{
    Player *p = turret->owner;
    if (!p) return;

    struct C2SPosition ppk;
    memset(&ppk, 0, sizeof(ppk));

    ppk.type = C2S_POSITION;

    if (withWeapon)
    {
        ppk.rotation = rotation;
        memcpy(&ppk.weapon, &turret->properties.weapon, sizeof(struct Weapons));
    }
    else
    {
        ppk.rotation = p->position.rotation;
        ppk.weapon.type = W_NULL;
    }

    ppk.y = p->position.y;
    ppk.x = p->position.x;
    ppk.time = current_ticks();

    ppk.xspeed = p->position.xspeed;
    ppk.yspeed = p->position.yspeed;
    ppk.energy = turret->energy;
    ppk.status = p->position.status & ~STATUS_ANTIWARP;

    game->FakePosition(turret->fake, &ppk, sizeof(ppk));
    turret->lastPosition = current_ticks();
}

local void readConfig(ConfigHandle ch, ArenaData *adata)
{
    adata->Config.useTurretLimit = (cfg->GetInt(ch, "HS_Turrets", "EnforceTurretLimit", 1) > 0);
    adata->Config.killsToOwner = (cfg->GetInt(ch, "HS_Turrets", "KillsToOwner", 1) > 0);

    int bombDamage = cfg->GetInt(ch, "Bomb", "BombDamageLevel", 770);
    bool percent = cfg->GetInt(ch, "Bomb", "EBombDamagePercent", 260) * 0.001;
    adata->Config.empBombDamage = bombDamage * percent;
    adata->Config.empShutdownTime = cfg->GetInt(ch, "Bomb", "EBombShutdownTime", 260);

    int i;
    for (i = 0; i < 8; i++)
    {
        adata->Config.perShip[i].empBomb = !!cfg->GetInt(ch, shipNames[i], "EmpBomb", 0);
    }
}

//Shooting/targeting code
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

local long lhypot(register long dx, register long dy)
{
	register unsigned long r, dd;
	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));
	if (r == 0) return (long)r;

	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}

local inline int getBestFiringAngle(Turret *turret)
{
    long bestDistance;
    int bestAngle = -1;
    int angle;

    int deltaTime = current_ticks() - turret->owner->position.time;

    int v_x = turret->owner->position.xspeed;
    int v_y = turret->owner->position.yspeed;

    int x = turret->owner->position.x + deltaTime * v_x / 1000;
    int y = turret->owner->position.y + deltaTime * v_y / 1000;

	Player *i;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		if (i->status == S_PLAYING && i->arena == turret->owner->arena && i->p_freq != turret->owner->p_freq && i->p_ship != SHIP_SPEC && !i->flags.is_dead && IS_HUMAN(i))
		{
			if (!(i->position.status & STATUS_CLOAK) || (turret->owner->position.status & STATUS_XRADAR))
			{
				long distance = lhypot(i->position.x - turret->owner->position.x, i->position.y - turret->owner->position.y);
				if (distance < turret->properties.maxRange && distance > turret->properties.minRange)
				{
					if (bestAngle == -1 || distance < bestDistance)
					{
                        if (turret->properties.weapon.type != W_THOR)
                        {
                            if (!isClearPath(turret->owner->arena, turret->owner->position.x, turret->owner->position.y, i->position.x, i->position.y))
                               continue;
                        }

                        angle = findFiringAngle(x, y, i->position.x, i->position.y,
                                 i->position.xspeed - v_x, i->position.yspeed - v_y,
                                 turret->weaponSpeed);

                        if (abs(angle - turret->owner->position.rotation) > turret->properties.rotFreedom)
                           continue;

                        bestAngle = angle;
	                    bestDistance = distance;
					}
				}
			}
		}
    }
	pd->Unlock();

	return bestAngle;
}

local inline unsigned char fireAngle(double x, double y)
{
	double angle = atan2(y, x) + M_PI;
	int a = round(angle * 40.0 / (2.0 * M_PI) + 30);
	return (unsigned char) a % 40;
}

local inline int findFiringAngle (double srcx, double srcy, double dstx, double dsty, double dxspeed, double dyspeed, double projspeed)
{
	double bestdx = dstx - srcx;
	double bestdy = dsty - srcy;

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

	return fireAngle(bestdx, bestdy);
}

//Timers
local int turretUpdateTimer(void *clos)
{
	Arena *arena = clos;
	ArenaData *adata = getArenaData(arena);

	Turret *turret;
	Link *link;

	lock(adata);

	FOR_EACH(&adata->turrets, turret, link)
	{
        if (!turret->fake) continue;
        if (turret->detached)
        {
            if (TICK_DIFF(current_ticks(), turret->lastDetached) >= 300)
               turret->detached = false;

            continue;
        }
        if (!turret->attached) continue;

        if (turret->emped)
        {
            turret->empTicks--;
            if (turret->empTicks <= 0) turret->emped = false;
        }
        else //Recharge
        {
            turret->energy += turret->properties.recharge;
		    if (turret->energy > turret->properties.energy)
		    {
			    turret->energy = turret->properties.energy;
			    if (turret->damaged) turret->damaged = false;
            }
        }

        //Fix for ?attach
        if (turret->fake->p_attached == -1)
        {
             turret->lastDetached = current_ticks();
             turret->attached = false;
             turret->detached = true;
             continue;
        }

        if (turret->properties.weapon.type != W_NULL && !turret->properties.syncGuns
           && !turret->properties.syncBombs)
        {
            if (TICK_DIFF(current_ticks(), turret->lastShot) >= turret->properties.delay && !turret->damaged)
            {
                 int angle = getBestFiringAngle(turret);

                 if (angle != -1 && !(turret->owner->position.status & STATUS_CLOAK) && !(turret->owner->position.status & STATUS_SAFEZONE))
                 {
                    unlock(adata);

                    turret->lastShot = current_ticks();
                    sendPositionPacket(turret, true, angle);

                    lock(adata);
                 }
            }
        }

        if (TICK_DIFF(current_ticks(), turret->lastPosition) >= 50)
        {
            unlock(adata);
            sendPositionPacket(turret, false, 0);
            lock(adata);
        }
    }

	unlock(adata);

	return TRUE;
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    fake = mm->GetInterface(I_FAKE, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    map = mm->GetInterface(I_MAPDATA, ALLARENAS);
    ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    net = mm->GetInterface(I_NET, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}
local bool checkInterfaces()
{
    if (aman && chat && cfg && db && fake && game && items && map && ml && net
       && pd)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(chat);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(fake);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(map);
    mm->ReleaseInterface(ml);
    mm->ReleaseInterface(net);
    mm->ReleaseInterface(pd);
}

EXPORT const char info_hs_turrets[] = "hs_turrets v1.1 by Spidernl\n";
EXPORT int MM_hs_turrets(int action, Imodman *mm_, Arena *arena)
{
    if (action == MM_LOAD)
	{
		mm = mm_;

		getInterfaces();
		if (!checkInterfaces())
		{
            releaseInterfaces();
            return MM_FAIL;
        }

        adkey = aman->AllocateArenaData(sizeof(struct ArenaData));
        pdkey = pd->AllocatePlayerData(sizeof(struct PlayerData));

        if (adkey == -1 || pdkey == -1) //Memory check
        {
            if (adkey  != -1) //free data if it was allocated
                aman->FreeArenaData(adkey);

            if (pdkey != -1) //free data if it was allocated
                pd->FreePlayerData (pdkey);

            releaseInterfaces();
            return MM_FAIL;
        }

        net->AddPacket(C2S_ATTACHTO, pktHandler);
        net->AddPacket(C2S_TURRETKICKOFF, pktHandler);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        net->RemovePacket(C2S_TURRETKICKOFF, pktHandler);
        net->RemovePacket(C2S_ATTACHTO, pktHandler);

        aman->FreeArenaData(adkey);
        pd->FreePlayerData(pdkey);

        releaseInterfaces();

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena);
        pthread_mutex_init(&adata->arenamtx, NULL);

        lock(adata);

        readConfig(arena->cfg, adata);

        if (adata->Config.useTurretLimit)
        {
            mm->RegAdviser(&buysellAdviser, arena);
            mm->RegAdviser(&itemsAdviser, arena);
        }

        if (adata->Config.killsToOwner)
        {
            mm->RegAdviser(&killAdviser, arena);
        }

        //Initialize arena-wide turret linkedlist
        LLInit(&adata->turrets);

        //Initialize all turret hashtables
        PlayerData *pdata;
        Player *p;
        Link *link;

        pd->Lock();
        FOR_EACH_PLAYER_IN_ARENA(p, arena)
        {
            if (!IS_HUMAN(p)) continue;

            pdata = getPlayerData(p);
            pdata->turrets = HashAlloc();
        }
        pd->Unlock();

		mm->RegCallback(CB_SHIPSET_CHANGED, shipsetChangedCB, arena);
        mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);
        mm->RegCallback(CB_KILL, killCB, arena);
        mm->RegCallback(CB_PPK, ppkCB, arena);

        ml->SetTimer(turretUpdateTimer, 1, 1, arena, arena);

        adata->attached = true;

        unlock(adata);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);

        lock(adata);

        adata->attached = false;

        ml->ClearTimer(turretUpdateTimer, arena);

        mm->UnregCallback(CB_PPK, ppkCB, arena);
        mm->UnregCallback(CB_KILL, killCB, arena);
        mm->UnregCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_PLAYERACTION, playerActionCB, arena);
		mm->UnregCallback(CB_SHIPSET_CHANGED, shipsetChangedCB, arena);

        //Clean up all turret hashtables
        PlayerData *pdata;
        Player *p;
        Link *link;

        pd->Lock();
        FOR_EACH_PLAYER_IN_ARENA(p, arena)
        {
            if (!IS_HUMAN(p)) continue;

            pdata = getPlayerData(p);
            if (!pdata->turrets) continue;

            removeAllTurrets(p);
            HashFree(pdata->turrets);
        }
        pd->Unlock();

        //Clean up arena-wide turret linkedlist
        LLEmpty(&adata->turrets);

        if (adata->Config.killsToOwner)
        {
            mm->UnregAdviser(&killAdviser, arena);
        }

        if (adata->Config.useTurretLimit)
        {
            mm->UnregAdviser(&buysellAdviser, arena);
            mm->UnregAdviser(&itemsAdviser, arena);
        }

        unlock(adata);

        pthread_mutex_destroy(&adata->arenamtx);

        return MM_OK;
    }

	return MM_FAIL;
}
