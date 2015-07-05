#include <stdio.h>
#include <stdbool.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hs_util_energy.h"
#include "hs_util/selfpos.h"

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Imainloop *ml;
local Iobjects *objs;
local Iplayerdata *pd;
local Iselfpos *sp;

#define PlayerData PlayerData_
typedef struct PlayerData
{
    bool utilityOn;
    bool dropInverter;

    struct
    {
        bool inverter;
        int inverterDuration;
        int inverterDurReduc;
    } ItemProperties;

    ticks_t lastInverted; //Prevents constant "re-inverting"
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

typedef struct Inverter
{
    i16 minX;
    i16 maxX;
    i16 minY;
    i16 maxY;

    i16 freq;

    int lvzID;

    ticks_t created;
    int duration;
    int durReduc;
} Inverter;

typedef struct ArenaData
{
    Ihsutilenergy *utilnrg;

    LinkedList inverters;
    pthread_mutex_t arenamtx;

    struct
    {
        int currentID;

        struct
        {
            int startID;
            int endID;
            int xOffset;
            int yOffset;
        } Config;
    } Lvz;
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

//Prototypes
local void ppkCB(Player *p, struct C2SPosition *pos);
local void utilityStateChangedCB(Player *p, int state, u8 reason);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void arenaActionCB(Arena *arena, int action);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB);
local bool createInverter(Arena *arena, i16 freq, int duration, int durReduc, i16 x, i16 y);
local void removeInverter(Arena *arena, LinkedList *inverters, Inverter *inverter);
local void removeAllInverters(Arena *arena, LinkedList *inverters);
local void readConfig(Arena *arena);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Callbacks
local void ppkCB(Player *p, struct C2SPosition *pos)
{
    if (!IS_STANDARD(p)) return;

    PlayerData *pdata = getPlayerData(p);

	if (p->flags.is_dead) {
		pdata->dropInverter = false;
		return;
	}

	ArenaData *adata = getArenaData(p->arena);

	//<<Inverter effect code>>
	ticks_t now = current_ticks();
	if (!pdata->lastInverted || TICK_DIFF(now, pdata->lastInverted) >= 100)
	{
        bool inverted = false;
	    Inverter *inverter;
	    Link *link;

	    lock(adata);
	    FOR_EACH(&adata->inverters, inverter, link)
	    {
            if (inverter->freq == p->p_freq) continue;

            if (pos->x > inverter->minX && pos->x < inverter->maxX)
            {
                if (pos->y > inverter->minY && pos->y < inverter->maxY)
                {
                    inverter->duration -= inverter->durReduc;
                    inverted = true;
                    break;
                }
            }
        }
        unlock(adata);

        if (inverted)
        {
            pdata->lastInverted = now;
            int reverseRotation = (pos->rotation + 20) % 40;
            sp->WarpPlayer(p, pos->x, pos->y, -pos->xspeed, -pos->yspeed, reverseRotation, 0);
        }
    }

	//<<Inverter placement code>>
	if (pdata->utilityOn && !(pos->status & STATUS_STEALTH))
    {
        lock(adata);
        if (adata->utilnrg)
		   adata->utilnrg->SetUtilityEnergy(p, -1);
		unlock(adata);

		return;
	}

	if (!pdata->dropInverter) return;

	if (createInverter(p->arena, p->p_freq, pdata->ItemProperties.inverterDuration, pdata->ItemProperties.inverterDurReduc, pos->x, pos->y))
       pdata->dropInverter = false;
}

local void utilityStateChangedCB(Player *p, int state, u8 reason)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.inverter) return;

    if (pdata->utilityOn && state <= 0) //On->Off
    {
        if (reason != UTILITY_ENERGY_SPAWNED && reason != UTILITY_ENERGY_KILLED)
        {
            pdata->dropInverter = true;
        }
    }

    pdata->utilityOn = (state > 0);
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, false);
}
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
}

local void arenaActionCB(Arena *arena, int action)
{
	if (action == AA_CONFCHANGED)
	{
		readConfig(arena);
	}
}

//Misc/utility functions
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB)
{
    if (!IS_STANDARD(p)) return;
    if (!hull || hull != db->getPlayerCurrentHull(p)) return;

    PlayerData *pdata = getPlayerData(p);

    if (lockDB == true)
       db->lock();

    pdata->ItemProperties.inverter = (items->getPropertySumOnHull(p, hull, "inverter", 0) > 0);
    pdata->ItemProperties.inverterDuration = items->getPropertySumOnHull(p, hull, "inverterduration", 0);
    pdata->ItemProperties.inverterDurReduc = items->getPropertySumOnHull(p, hull, "inverterdurreduc", 0);

    if (lockDB == true)
       db->unlock();
}

local bool createInverter(Arena *arena, i16 freq, int duration, int durReduc, i16 x, i16 y)
{
    ArenaData *adata = getArenaData(arena);

    //First check if this inverter fits - as in, if no other inverter is in the way.

    int minX = x + adata->Lvz.Config.xOffset; //Left edge
    int maxX = x - adata->Lvz.Config.xOffset; //Right edge
    int minY = y + adata->Lvz.Config.yOffset; //Top
    int maxY = y - adata->Lvz.Config.yOffset; //Bottom

    int minX2; //Left edge
    int maxX2; //Right edge
    int minY2; //Top
    int maxY2; //Bottom

    Inverter *inverter;
    Link *link;

    lock(adata);
    FOR_EACH(&adata->inverters, inverter, link)
    {
        minX2 = inverter->minX;
        maxX2 = inverter->maxX;
        minY2 = inverter->minY;
        maxY2 = inverter->maxY;

        if (minX < maxX2 && maxX > minX2 && minY < maxY2 && maxY > minY2) {
           unlock(adata);
           return false; //Overlap
        }
    }
    unlock(adata);

    inverter = amalloc(sizeof(Inverter));

    inverter->minX = minX;
    inverter->maxX = maxX;
    inverter->minY = minY;
    inverter->maxY = maxY;

    inverter->freq = freq;

   	//Play LVZ if configs are "correct"
	if (adata->Lvz.currentID >= 0)
	{
        Target t;
	    t.type = T_ARENA;
	    t.u.arena = arena;

	    objs->Move(&t, adata->Lvz.currentID, x + adata->Lvz.Config.xOffset,
                       y + adata->Lvz.Config.yOffset, 0, 0);
        objs->Toggle(&t,adata->Lvz.currentID,1);

        inverter->lvzID = adata->Lvz.currentID;

	    adata->Lvz.currentID++;
	    if (adata->Lvz.currentID > adata->Lvz.Config.endID)
           adata->Lvz.currentID = adata->Lvz.Config.startID;
    }
    else
    {
        inverter->lvzID = -1;
    }

    inverter->duration = duration;
    inverter->durReduc = durReduc;

    inverter->created = current_ticks();

    lock(adata);
	LLAdd(&adata->inverters, inverter);
	unlock(adata);

    return true;
}
local void removeInverter(Arena *arena, LinkedList *inverters, Inverter *inverter)
{
    //Optionally, get rid of LVZ.
    if (inverter->lvzID >= 0)
    {
        Target t;
        t.type = T_ARENA;
        t.u.arena = arena;

        objs->Toggle(&t,inverter->lvzID,0);
    }

    LLRemove(inverters, inverter);
    afree(inverter);
}
local void removeAllInverters(Arena *arena, LinkedList *inverters)
{
    Inverter *inverter;
    Link *link;

    FOR_EACH(inverters, inverter, link)
    {
        //Optionally, get rid of LVZ.
        if (inverter->lvzID >= 0)
        {
            Target t;
            t.type = T_ARENA;
            t.u.arena = arena;

            objs->Toggle(&t,inverter->lvzID,0);
        }

        afree(inverter);
    }

    LLEmpty(inverters);
}

local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    adata->Lvz.Config.startID = cfg->GetInt(ch, "Inverter", "lvzStartID", -1);
    adata->Lvz.Config.endID = cfg->GetInt(ch, "Inverter", "lvzEndID", -1);
    adata->Lvz.Config.xOffset = cfg->GetInt(ch, "Inverter", "lvzXOffset", 0);
    adata->Lvz.Config.yOffset = cfg->GetInt(ch, "Inverter", "lvzYOffset", 0);

    if (adata->Lvz.Config.startID < 0 || adata->Lvz.Config.endID < 0)
       adata->Lvz.currentID = -1;
    else
       adata->Lvz.currentID = adata->Lvz.Config.startID;
}

//Timers
local int inverterDurationTimer(void *clos)
{
	Arena *arena = clos;
	ArenaData *adata = getArenaData(arena);

	ticks_t now = current_ticks();

	Inverter *inverter;
	Link *link;

	lock(adata);
    FOR_EACH(&adata->inverters, inverter, link)
    {
        if (TICK_DIFF(now, inverter->created) >= inverter->duration)
        {
            removeInverter(arena, &adata->inverters, inverter);
        }
    }
    unlock(adata);

	return TRUE;
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    objs = mm->GetInterface(I_OBJECTS, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    sp = mm->GetInterface(I_SELFPOS, ALLARENAS);
}
local bool checkInterfaces()
{
    if (aman && cfg && db && items && ml && objs && pd && sp)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(ml);
    mm->ReleaseInterface(objs);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(sp);
}

EXPORT const char info_hs_inverter[] = "hs_inverter v1.0 by Spidernl\n";
EXPORT int MM_hs_inverter(int action, Imodman *mm_, Arena *arena)
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
                pd->FreePlayerData(pdkey);

            releaseInterfaces();
            return MM_FAIL;
        }

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        aman->FreeArenaData(adkey);
        pd->FreePlayerData(pdkey);

        releaseInterfaces();

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena);
        adata->utilnrg = mm->GetInterface(I_HS_UTIL_ENERGY, arena);
        if (!adata->utilnrg)
        {
            return MM_FAIL;
        }

        pthread_mutex_init(&adata->arenamtx, NULL);

        readConfig(arena);
        LLInit(&adata->inverters);

        mm->RegCallback(CB_PPK, ppkCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);
        mm->RegCallback(CB_ARENAACTION, arenaActionCB, arena);

        ml->SetTimer(inverterDurationTimer, 10, 10, arena, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ml->ClearTimer(inverterDurationTimer, arena);

        mm->UnregCallback(CB_PPK, ppkCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);
        mm->UnregCallback(CB_ARENAACTION, arenaActionCB, arena);

        ArenaData *adata = getArenaData(arena);

        lock(adata);
        mm->ReleaseInterface(adata->utilnrg);
        removeAllInverters(arena, &adata->inverters);
        unlock(adata);

        pthread_mutex_destroy(&adata->arenamtx);

        return MM_OK;
    }

	return MM_FAIL;
}
