#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "hs_chargeables.h"

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
        int unchargedID; //LVZ Object ID used when bar is "uncharged"
        int chargedID; //LVZ Object ID used when bar is "charged"

        //Segments
        int startSegmentID;
        int segmentCount;
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
    //Whether status is "uncharged" or "charged"
    bool charged;

    //Chargeable 'bar' variables
    short visibleState; //-1 = "off" state, 0 = nothing visible, 1 = "on" state
    int visibleSegments;

    bool hasChargeable;
    struct
    {
        int chargeRate;
        int drainRate;
        int drainRateWhileOn;
        int chargeEnergyCost;
    } ItemProperties;

    bool hasRechargeTimer;
    double charge; //Max charge = 1000, min charge = 0.
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
local int HasChargeable(Player *p);
local double GetCharge(Player *p);
local void ModCharge(Player *p, double amount);
local void SetCharge(Player *p, double charge);
local void SetIsCharged(Player *p, int charged);

local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value);

local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void playerActionCB(Player *p, int action, Arena *arena);

local int rechargeTimer(void* param);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB, bool resetCharge);
local void updateBar(Player *p, PlayerData *pdata, ArenaData *adata);
local void setIsCharged(Player *p, PlayerData *pdata, bool charged, u8 reason);
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

local int HasChargeable(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return -1;

    PlayerData *pdata = getPlayerData(p);
    return pdata->hasChargeable;
}

local double GetCharge(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return 0;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->hasChargeable) return 0;

    return pdata->charge;
}

local void ModCharge(Player *p, double amount)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->hasChargeable) return;

    pdata->charge += amount;

    if (pdata->charge > 1000)
    {
        pdata->charge = 1000;
        setIsCharged(p, pdata, true, CHARGEABLES_REASON_DEFAULT);
    }
    if (pdata->charge < 0)
    {
        setIsCharged(p, pdata, false, CHARGEABLES_REASON_DEFAULT);
    }

    updateBar(p, pdata, adata);
}

local void SetCharge(Player *p, double charge)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->hasChargeable) return;

    pdata->charge = charge;

    if (pdata->charge > 1000)
    {
        pdata->charge = 1000;
        setIsCharged(p, pdata, true, CHARGEABLES_REASON_DEFAULT);
    }
    if (pdata->charge < 0)
    {
        setIsCharged(p, pdata, false, CHARGEABLES_REASON_DEFAULT);
    }

    updateBar(p, pdata, adata);
}

local void SetIsCharged(Player *p, int charged)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->hasChargeable) return;

    setIsCharged(p, pdata, (charged > 0), CHARGEABLES_REASON_DEFAULT);
    updateBar(p, pdata, adata);
}

local Ihschargeables hschargeables =
{
        INTERFACE_HEAD_INIT(I_HS_CHARGEABLES, "hs_util_energy")

        Lock,
        Unlock,
        HasChargeable,
        GetCharge,
        ModCharge,
        SetCharge,
        SetIsCharged
};

//Advisers
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    if (items->getPropertySumOnShipSet(p, ship, shipset, "chargeable", 0) <= 0) return init_value;

    if (strcmp(prop, "stealth") == 0)
    {
        return 1;
    }
    if (strcmp(prop, "stealthenergy") == 0)
    {
        return items->getPropertySumOnShipSet(p, ship, shipset, "chargeenergycost", 0);
    }

    return init_value;
}

local Ahscorespawner overrideAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_SPAWNER)

	getOverrideValueCB
};

//Callbacks
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    ArenaData *adata = getArenaData(arena);
    PlayerData *pdata = getPlayerData(killed);

    if (pdata->hasChargeable)
    {
        //<<<Lock>>>
        lock(adata);

        pdata->charge = 0;

        setIsCharged(killed, pdata, false, CHARGEABLES_REASON_KILLED);
        updateBar(killed, pdata, adata);

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
            pdata->charge += pdata->ItemProperties.chargeRate / 10.0;
        }

        if (pdata->charged) //"On" drain
        {
            pdata->charge -= pdata->ItemProperties.drainRateWhileOn / 10.0;
        }
        else //"Off" drain
        {
            pdata->charge -= pdata->ItemProperties.drainRate / 10.0;
        }
    }

    if (pdata->charge > 1000)
    {
        pdata->charge = 1000;
        setIsCharged(p, pdata, true, CHARGEABLES_REASON_DEFAULT);
    }
    if (pdata->charge < 0)
    {
        setIsCharged(p, pdata, false, CHARGEABLES_REASON_DEFAULT);
    }

    updateBar(p, pdata, adata);

	//<<<Unlock>>>
    unlock(adata);

    return 1;
}

//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB, bool resetCharge)
{
    if (!IS_STANDARD(p))
       return;

	if (db->getPlayerCurrentHull(p) != hull)
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

        pdata->hasChargeable = false;
        updateBar(p, pdata, adata);

        //<<<Unlock>>>
        unlock(adata);
        return;
    }

    //Get item properties
    if (lockDB == true)
       db->lock();

    pdata->hasChargeable = (items->getPropertySumOnHull(p, hull, "chargeable", 0) > 0);
    if (pdata->hasChargeable)
    {
        pdata->ItemProperties.chargeRate = items->getPropertySumOnHull(p, hull, "chargerate", 0);
        pdata->ItemProperties.drainRate = items->getPropertySumOnHull(p, hull, "drainrate", 0);
        pdata->ItemProperties.drainRateWhileOn = items->getPropertySumOnHull(p, hull, "drainratewhileon", 0);
    }

    if (lockDB == true)
       db->unlock();

    if (pdata->hasChargeable)
    {
        if (!pdata->hasRechargeTimer)
        {
            ml->SetTimer(rechargeTimer, 10, 10, p, p);
            pdata->hasRechargeTimer = true;
        }

        if (resetCharge)
        {
            pdata->charge = 0;
            setIsCharged(p, pdata, false, CHARGEABLES_REASON_SPAWNED);
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

    updateBar(p, pdata, adata);

    //<<<Unlock>>>
    unlock(adata);
}

local void updateBar(Player *p, PlayerData *pdata, ArenaData *adata)
{
    if (!adata->LVZConfig.setUp) return; //Nothing to do here; configs are incomplete or unset

    short ids[adata->LVZConfig.segmentCount + 2];
    char ons[adata->LVZConfig.segmentCount + 2];
    int index = 0;

    int state;
    int barSegments;

    if (pdata->hasChargeable)
    {
        if (pdata->charged)
           state = 1;
        else state = -1;

        barSegments = max(0, adata->LVZConfig.segmentCount * pdata->charge / 1000);
    }
    else
    {
        state = 0;
        barSegments = 0;
    }

    //Update state
    if (pdata->visibleState != state)
    {
        if (pdata->visibleState == -1)
        {
            ids[index] = adata->LVZConfig.unchargedID;
            ons[index] = 0;
            index++;
        }
        if (pdata->visibleState == 1)
        {
            ids[index] = adata->LVZConfig.chargedID;
            ons[index] = 0;
            index++;
        }

        if (state == -1)
        {
            ids[index] = adata->LVZConfig.unchargedID;
            ons[index] = 1;
            index++;
        }
        if (state == 1)
        {
            ids[index] = adata->LVZConfig.chargedID;
            ons[index] = 1;
            index++;
        }

        pdata->visibleState = state;
    }

    //Update visible segments
    if (barSegments != pdata->visibleSegments)
    {
        int i;

        if (barSegments < pdata->visibleSegments)
        {
            for (i = barSegments; i < pdata->visibleSegments; i++)
            {
                ids[index] = adata->LVZConfig.startSegmentID + i;
                ons[index] = 0;
                index++;
            }
        }
        else
        {
            for (i = pdata->visibleSegments; i < barSegments; i++)
            {
                ids[index] = adata->LVZConfig.startSegmentID+ i;
                ons[index] = 1;
                index++;
            }
        }

        pdata->visibleSegments = barSegments;
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

local void setIsCharged(Player *p, PlayerData *pdata, bool charged, u8 reason)
{
    if (pdata->charged != charged)
    {
        pdata->charged = charged;

        DO_CBS(CB_CHARGEABLE_STATE_CHANGED, p->arena, ChargeableStateChanged, (p, charged, reason));
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
    adata->LVZConfig.chargedID = cfg->GetInt(ch, "HS_Chargeables", "chargedID", -1);
    adata->LVZConfig.unchargedID = cfg->GetInt(ch, "HS_Chargeables", "unchargedID", -1);

    adata->LVZConfig.startSegmentID = cfg->GetInt(ch, "HS_Chargeables", "startSegmentID", -1);
    adata->LVZConfig.segmentCount = cfg->GetInt(ch, "HS_Chargeables", "segmentCount", -1);

    if (adata->LVZConfig.chargedID >= 0 && adata->LVZConfig.unchargedID >= 0 &&
        adata->LVZConfig.startSegmentID >= 0 && adata->LVZConfig.segmentCount > 0)
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

EXPORT const char info_hs_chargeables[] = "hs_chargeables v1.2 by Spidernl\n";
EXPORT int MM_hs_chargeables(int action, Imodman *mm_, Arena *arena)
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

        mm->RegAdviser(&overrideAdviser, arena);

        mm->RegInterface(&hschargeables, arena);
        adata->attached = true;

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        if (mm->UnregInterface(&hschargeables, arena))
		{
			return MM_FAIL;
		}

        mm->UnregAdviser(&overrideAdviser, arena);

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
