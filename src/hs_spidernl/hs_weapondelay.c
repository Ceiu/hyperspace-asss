#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "hs_util_energy.h"

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Iplayerdata *pd;

#define PlayerData PlayerData_
typedef struct PlayerData
{
    bool utilityOn;

    struct
    {
        bool delayModifier;
    } ItemProperties;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    return PPDATA(p, pdkey);
}

typedef struct ArenaData
{
    Ihscorespawner *spawner;
    pthread_mutex_t arenamtx;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    return P_ARENA_DATA(arena, adkey);
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
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value);

local void utilityStateChangedCB(Player *p, int state, u8 reason);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Advisers
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->utilityOn) return init_value;

    if (strcmp(prop, "bulletdelay") == 0 || strcmp(prop, "multidelay") == 0)
    {
        init_value += items->getPropertySumOnShipSet(p, ship, shipset, "utilgundelay", 0);
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "utilgundelaypct", 100) / 100.0);
    }
    if (strcmp(prop, "bombdelay") == 0)
    {
        init_value += items->getPropertySumOnShipSet(p, ship, shipset, "utilbombdelay", 0);
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "utilbombdelaypct", 100) / 100.0);
    }

    return init_value;
}

local Ahscorespawner overrideAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_SPAWNER)

	getOverrideValueCB
};

//Callbacks
local void utilityStateChangedCB(Player *p, int state, u8 reason)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.delayModifier) return;

    ArenaData *adata = getArenaData(p->arena);
    pdata->utilityOn = (state > 0);

    lock(adata);
    if (adata->spawner)
       adata->spawner->resendOverrides(p);
    unlock(adata);
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, false);
}

local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
}

//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB)
{
    if (!IS_STANDARD(p)) return;
    if (!hull || hull != db->getPlayerCurrentHull(p)) return;

    PlayerData *pdata = getPlayerData(p);

    //Get item properties
    if (lockDB == true)
       db->lock();

    pdata->ItemProperties.delayModifier = (items->getPropertySumOnHull(p, hull, "delaymodifier", 0) > 0);

    if (lockDB == true)
       db->unlock();
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}
local bool checkInterfaces()
{
    if (aman && db && items && pd)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(pd);
}

EXPORT const char info_hs_weapondelay[] = "hs_weapondelay v1.0 by Spidernl\n";
EXPORT int MM_hs_weapondelay(int action, Imodman *mm_, Arena *arena)
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
        pthread_mutex_init(&adata->arenamtx, NULL);

        lock(adata);
        adata->spawner = mm->GetInterface(I_HSCORE_SPAWNER, arena);
        if (!adata->spawner)
        {
            unlock(adata);
            pthread_mutex_destroy(&adata->arenamtx);

            return MM_FAIL;
        }
        unlock(adata);

        mm->RegAdviser(&overrideAdviser, arena);

        mm->RegCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);

        lock(adata);
        mm->ReleaseInterface(adata->spawner);
        unlock(adata);

        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);

        mm->UnregAdviser(&overrideAdviser, arena);

        pthread_mutex_destroy(&adata->arenamtx);

        return MM_OK;
    }

	return MM_FAIL;
}
