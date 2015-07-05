#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "hs_util_energy.h"

// Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Imainloop *ml;
local Iobjects *objs;
local Iplayerdata *pd;

typedef struct ArenaData
{
    bool attached;
    pthread_mutex_t arenamtx;

    struct
    {
        //Whether or not configs are set up correctly
        bool setUp;

        //State
        int utilityBarOffID; //LVZ Object ID used when utility bar is "off"
        int utilityBarOnID; //LVZ Object ID used when utility bar is "on"

        //Segments
        int utilityBarStartSegmentID;
        int utilityBarSegmentCount;
    } LVZConfig;
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
    bool utilityOn;

    short visibleBarState; //-1 = "off" state, 0 = nothing visible, 1 = "on" state
    int visibleBarSegments;

    struct
    {
        bool utility;
        int maxUtilNrg;
        int utilNrgRecharge;
        int utilNrgDrain;
        int utilOnNrgDrain;
    } ItemProperties;

    bool hasRechargeTimer;
    double utilNrg;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Prototypes
local void Lock(Arena *arena);
local void Unlock(Arena *arena);
local int HasUtility(Player *p);
local double GetUtilityEnergy(Player *p);
local void ModUtilityEnergy(Player *p, double amount);
local void SetUtilityEnergy(Player *p, double energy);
local void SetUtilityState(Player *p, int state);

local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void playerActionCB(Player *p, int action, Arena *arena);

local int rechargeTimer(void* param);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB, bool resetUtilNrg);
local void updateUtilityBar(Player *p, PlayerData *pdata, ArenaData *adata);
local void setUtilityOn(Player *p, PlayerData *pdata, bool on, u8 reason);
local inline int max(int a, int b);
local void readConfig(ConfigHandle ch, ArenaData *adata);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Interface functions
local void Lock(Arena *arena)
{
    lock(getArenaData(arena));
}

local void Unlock(Arena *arena)
{
    unlock(getArenaData(arena));
}

local int HasUtility(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return -1;

    PlayerData *pdata = getPlayerData(p);
    return pdata->ItemProperties.utility;
}

local double GetUtilityEnergy(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return 0;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.utility) return 0;

    return pdata->utilNrg;
}

local void ModUtilityEnergy(Player *p, double amount)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.utility) return;

    pdata->utilNrg += amount;

    if (pdata->utilNrg > pdata->ItemProperties.maxUtilNrg)
    {
        pdata->utilNrg = pdata->ItemProperties.maxUtilNrg;
        setUtilityOn(p, pdata, true, UTILITY_ENERGY_MODIFIED);
    }
    if (pdata->utilNrg < 0)
    {
        setUtilityOn(p, pdata, false, UTILITY_ENERGY_MODIFIED);
    }

    updateUtilityBar(p, pdata, adata);
}

local void SetUtilityEnergy(Player *p, double energy)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.utility) return;

    pdata->utilNrg = energy;

    if (pdata->utilNrg > pdata->ItemProperties.maxUtilNrg)
    {
        pdata->utilNrg = pdata->ItemProperties.maxUtilNrg;
        setUtilityOn(p, pdata, true, UTILITY_ENERGY_SET);
    }
    if (pdata->utilNrg < 0)
    {
        setUtilityOn(p, pdata, false, UTILITY_ENERGY_SET);
    }

    updateUtilityBar(p, pdata, adata);
}

local void SetUtilityState(Player *p, int state)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.utility) return;

    setUtilityOn(p, pdata, (state > 0), UTILITY_ENERGY_STATE_SET);
    updateUtilityBar(p, pdata, adata);
}

local Ihsutilenergy hsutilenergy =
{
        INTERFACE_HEAD_INIT(I_HS_UTIL_ENERGY, "hs_util_energy")

        Lock,
        Unlock,
        HasUtility,
        GetUtilityEnergy,
        ModUtilityEnergy,
        SetUtilityEnergy,
        SetUtilityState
};


//Callbacks
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    ArenaData *adata = getArenaData(arena);
    PlayerData *pdata = getPlayerData(killed);

    if (pdata->ItemProperties.utility)
    {
        //<<<Lock>>>
        lock(adata);

        pdata->utilNrg = 0;

        setUtilityOn(killed, pdata, false, UTILITY_ENERGY_KILLED);
        updateUtilityBar(killed, pdata, adata);

        //<<<Unlock>>>
        unlock(adata);
    }
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, false, false);
}
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), true, true);
}

local void playerActionCB(Player *p, int action, Arena *arena)
{
    if (action == PA_LEAVEARENA)
	{
		ml->ClearTimer(rechargeTimer, p);
	}
}

//Timers
local int rechargeTimer(void* param)
{
	Player *p = (Player*)param;
	if (p->flags.is_dead) return 1;

	ArenaData *adata = getArenaData(p->arena);

	//<<<Lock>>>
    lock(adata);

	PlayerData *pdata = getPlayerData(p);

	if (!(p->position.status & STATUS_SAFEZONE)) //Only recharge/drain out of safe
    {
        if (p->position.status & STATUS_STEALTH) //Recharge
        {
            pdata->utilNrg += pdata->ItemProperties.utilNrgRecharge / 10.0;
        }
        if (pdata->utilityOn) //"On" drain
        {
            pdata->utilNrg -= pdata->ItemProperties.utilOnNrgDrain / 10.0;
        }
        pdata->utilNrg -= pdata->ItemProperties.utilNrgDrain / 10.0;
    }

    if (pdata->utilNrg > pdata->ItemProperties.maxUtilNrg)
    {
        pdata->utilNrg = pdata->ItemProperties.maxUtilNrg;
        setUtilityOn(p, pdata, true, UTILITY_ENERGY_RECHARGED);
    }
    if (pdata->utilNrg < 0)
    {
        setUtilityOn(p, pdata, false, UTILITY_ENERGY_DRAINED);
    }

    updateUtilityBar(p, pdata, adata);

	//<<<Unlock>>>
    unlock(adata);

    return 1;
}

//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB, bool resetUtilNrg)
{
    if (!IS_STANDARD(p))
       return;

	if (!hull || hull != db->getPlayerCurrentHull(p))
	  return;

    ArenaData *adata = getArenaData(p->arena);

    //<<<Lock>>>
    lock(adata);

    PlayerData *pdata = getPlayerData(p);

    if (p->p_ship == SHIP_SPEC)
    {
        if (pdata->hasRechargeTimer)
        {
            ml->ClearTimer(rechargeTimer, p);
            pdata->hasRechargeTimer = false;
        }

        pdata->ItemProperties.utility = false;
        updateUtilityBar(p, pdata, adata);

        //<<<Unlock>>>
        unlock(adata);
        return;
    }

    //Get item properties
    if (lockDB == true)
       db->lock();

    pdata->ItemProperties.utility = (items->getPropertySumOnHull(p, hull, "utility", 0) > 0);
    int maxUtilNrg = items->getPropertySumOnHull(p, hull, "maxutilnrg", 0);
    pdata->ItemProperties.utilNrgRecharge = items->getPropertySumOnHull(p, hull, "utilnrgrecharge", 0);
    pdata->ItemProperties.utilNrgDrain = items->getPropertySumOnHull(p, hull, "utilnrgdrain", 0);
    pdata->ItemProperties.utilOnNrgDrain = items->getPropertySumOnHull(p, hull, "utilnrgdrain_on", 0);

    if (lockDB == true)
       db->unlock();

    if (maxUtilNrg != pdata->ItemProperties.maxUtilNrg)
    {
        pdata->ItemProperties.maxUtilNrg = maxUtilNrg;
        resetUtilNrg = true;
    }

    if (pdata->ItemProperties.utility)
    {
        if (!pdata->hasRechargeTimer)
        {
            ml->SetTimer(rechargeTimer, 10, 10, p, p);
            pdata->hasRechargeTimer = true;
        }

        if (resetUtilNrg)
        {
            pdata->utilNrg = 0;
            setUtilityOn(p, pdata, false, UTILITY_ENERGY_SPAWNED);
        }
    }
    else
    {
        if (pdata->hasRechargeTimer)
        {
            ml->ClearTimer(rechargeTimer, p);
            pdata->hasRechargeTimer = false;
        }
    }

    updateUtilityBar(p, pdata, adata);

    //<<<Unlock>>>
    unlock(adata);
}

local void updateUtilityBar(Player *p, PlayerData *pdata, ArenaData *adata)
{
    if (!adata->LVZConfig.setUp) return; //Nothing to do here; configs are incomplete or unset

    short ids[adata->LVZConfig.utilityBarSegmentCount + 2];
    char ons[adata->LVZConfig.utilityBarSegmentCount + 2];
    int index = 0;

    int state;
    int barSegments;

    if (pdata->ItemProperties.utility)
    {
        if (pdata->utilityOn)
           state = 1;
        else state = -1;

        barSegments = max(0, adata->LVZConfig.utilityBarSegmentCount * pdata->utilNrg / pdata->ItemProperties.maxUtilNrg);
    }
    else
    {
        state = 0;
        barSegments = 0;
    }

    //Update state
    if (pdata->visibleBarState != state)
    {
        if (pdata->visibleBarState == -1)
        {
            ids[index] = adata->LVZConfig.utilityBarOffID;
            ons[index] = 0;
            index++;
        }
        if (pdata->visibleBarState == 1)
        {
            ids[index] = adata->LVZConfig.utilityBarOnID;
            ons[index] = 0;
            index++;
        }

        if (state == -1)
        {
            ids[index] = adata->LVZConfig.utilityBarOffID;
            ons[index] = 1;
            index++;
        }
        if (state == 1)
        {
            ids[index] = adata->LVZConfig.utilityBarOnID;
            ons[index] = 1;
            index++;
        }

        pdata->visibleBarState = state;
    }

    //Update visible segments
    if (barSegments != pdata->visibleBarSegments)
    {
        int i;

        if (barSegments < pdata->visibleBarSegments)
        {
            for (i = barSegments; i < pdata->visibleBarSegments; i++)
            {
                ids[index] = adata->LVZConfig.utilityBarStartSegmentID + i;
                ons[index] = 0;
                index++;
            }
        }
        else
        {
            for (i = pdata->visibleBarSegments; i < barSegments; i++)
            {
                ids[index] = adata->LVZConfig.utilityBarStartSegmentID + i;
                ons[index] = 1;
                index++;
            }
        }

        pdata->visibleBarSegments = barSegments;
    }

    //Send changes (if any)
    if (index > 0)
    {
        Target target;
	    target.type = T_PLAYER;
	    target.u.p = p;

        objs->ToggleSet(&target, ids, ons, index);
    }
}

local void setUtilityOn(Player *p, PlayerData *pdata, bool on, u8 reason)
{
    if (pdata->utilityOn != on)
    {
        pdata->utilityOn = on;

        DO_CBS(CB_UTILITY_STATE_CHANGED, p->arena, UtilityStateChanged, (p, on, reason));
    }
}

local inline int max(int a, int b)
{
	if (a > b)
	{
		return a;
	}
	else
	{
		return b;
	}
}

local void readConfig(ConfigHandle ch, ArenaData *adata)
{
    adata->LVZConfig.utilityBarOffID = cfg->GetInt(ch, "HS_Utility_Energy", "utilityBarOffID", -1);
    adata->LVZConfig.utilityBarOnID = cfg->GetInt(ch, "HS_Utility_Energy", "utilityBarOnID", -1);

    adata->LVZConfig.utilityBarStartSegmentID = cfg->GetInt(ch, "HS_Utility_Energy", "utilityBarStartSegmentID", -1);
    adata->LVZConfig.utilityBarSegmentCount = cfg->GetInt(ch, "HS_Utility_Energy", "utilityBarSegmentCount", -1);

    if (adata->LVZConfig.utilityBarOffID >= 0 && adata->LVZConfig.utilityBarOnID >= 0 &&
        adata->LVZConfig.utilityBarStartSegmentID >= 0 && adata->LVZConfig.utilityBarSegmentCount > 0)
    {
        adata->LVZConfig.setUp = 1;
    }
    else
    {
        adata->LVZConfig.setUp = 0;
    }
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
}
local bool checkInterfaces()
{
    if (aman && cfg && db && items && ml && objs && pd)
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
}

EXPORT const char info_hs_util_energy[] = "hs_util_energy v1.1 by Spidernl\n";
EXPORT int MM_hs_util_energy(int action, Imodman *mm_, Arena *arena)
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
        readConfig(arena->cfg, adata);

        pthread_mutex_init(&adata->arenamtx, NULL);

        mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_KILL, killCB, arena);

        mm->RegInterface(&hsutilenergy, arena);
        adata->attached = true;

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        if (mm->UnregInterface(&hsutilenergy, arena))
		{
			return MM_FAIL;
		}

        mm->UnregCallback(CB_KILL, killCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_PLAYERACTION, playerActionCB, arena);

        //Remove timers
        Player *p;
        Link *link;

        pd->Lock();
		FOR_EACH_PLAYER(p)
		{
			if (p->arena == arena)
			{
				ml->ClearTimer(rechargeTimer, p);
			}
		}
		pd->Unlock();

        ArenaData *adata = getArenaData(arena);
        pthread_mutex_destroy(&adata->arenamtx);
        adata->attached = false;

        return MM_OK;
    }

	return MM_FAIL;
}
