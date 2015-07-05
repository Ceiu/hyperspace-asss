#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "hs_util_energy.h"
#include "watchdamage.h"

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Iobjects *objs;
local Iplayerdata *pd;

#define PlayerData PlayerData_
typedef struct PlayerData
{
    bool utilityOn;

    struct
    {
        bool shield;
        double shieldDrainPct;
    } ItemProperties;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    return PPDATA(p, pdkey);
}

typedef struct ArenaData
{
    Ihsutilenergy *utilnrg;
    Ihscorespawner *spawner;

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
local void playerDamageCB(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB);
local void readConfig(Arena *arena);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Advisers
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->utilityOn) return init_value;

    if (strcmp(prop, "bulletdamage") == 0 || strcmp(prop, "bulletdamageup") == 0
       || strcmp(prop, "bombdamage") == 0 || strcmp(prop, "shrapdamage") == 00
       || strcmp(prop, "inactshrapdamage") == 0)
    {
        return init_value - (init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "shieldabsorbtion", 0) / 100.0));
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
    if (!pdata->ItemProperties.shield) return;

    ArenaData *adata = getArenaData(p->arena);
    pdata->utilityOn = (state > 0);

    lock(adata);
    if (adata->spawner)
       adata->spawner->resendOverrides(p);
    unlock(adata);
}

local void playerDamageCB(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count)
{
    if (p->flags.is_dead) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->utilityOn || !pdata->ItemProperties.shield) return;

    ArenaData *adata = getArenaData(p->arena);

    int damage = 0;
    double drain;
    int i;

    for (i = 0; i < count; i++)
    {
        damage += s2cdamage->damage[i].damage;
    }

    if (damage > 0) //Probably always true?
    {
        drain = pdata->ItemProperties.shieldDrainPct * damage;

        lock(adata);
        if (adata->utilnrg)
           adata->utilnrg->ModUtilityEnergy(p, -drain);
        unlock(adata);

        if (adata->Lvz.currentID >= 0)
        {
            Target t;
	        t.type = T_ARENA;
	        t.u.arena = arena;

	        objs->Move(&t, adata->Lvz.currentID, p->position.x + adata->Lvz.Config.xOffset,
                           p->position.y + adata->Lvz.Config.yOffset, 0, 0);
            objs->Toggle(&t,adata->Lvz.currentID,1);

	        adata->Lvz.currentID++;
	        if (adata->Lvz.currentID > adata->Lvz.Config.endID)
               adata->Lvz.currentID = adata->Lvz.Config.startID;
         }
    }
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

    pdata->ItemProperties.shield = (items->getPropertySumOnHull(p, hull, "shield", 0) > 0);
    if (pdata->ItemProperties.shield)
    {
        pdata->ItemProperties.shieldDrainPct = (items->getPropertySumOnHull(p, hull, "shielddrainpct", 0) / 100.0);
    }

    if (lockDB == true)
       db->unlock();
}

local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    adata->Lvz.Config.startID = cfg->GetInt(ch, "Shields2", "lvzStartID", -1);
    adata->Lvz.Config.endID = cfg->GetInt(ch, "Shields2", "lvzEndID", -1);
    adata->Lvz.Config.xOffset = cfg->GetInt(ch, "Shields2", "lvzXOffset", 0);
    adata->Lvz.Config.yOffset = cfg->GetInt(ch, "Shields2", "lvzYOffset", 0);

    if (adata->Lvz.Config.startID < 0 || adata->Lvz.Config.endID < 0)
       adata->Lvz.currentID = -1;
    else
       adata->Lvz.currentID = adata->Lvz.Config.startID;
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    objs = mm->GetInterface(I_OBJECTS, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}
local bool checkInterfaces()
{
    if (aman && cfg && db && items && objs && pd)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(objs);
    mm->ReleaseInterface(pd);
}

EXPORT const char info_hs_shields2[] = "hs_shields2 v1.0 by Spidernl\n";
EXPORT int MM_hs_shields2(int action, Imodman *mm_, Arena *arena)
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

        adata->utilnrg = mm->GetInterface(I_HS_UTIL_ENERGY, arena);
        if (!adata->utilnrg)
        {
            mm->ReleaseInterface(adata->spawner);

            unlock(adata);
            pthread_mutex_destroy(&adata->arenamtx);

            return MM_FAIL;
        }
        unlock(adata);

        readConfig(arena);

        mm->RegAdviser(&overrideAdviser, arena);

        mm->RegCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);

        lock(adata);
        mm->ReleaseInterface(adata->utilnrg);
        mm->ReleaseInterface(adata->spawner);
        unlock(adata);

        mm->UnregCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);

        mm->UnregAdviser(&overrideAdviser, arena);

        pthread_mutex_destroy(&adata->arenamtx);

        return MM_OK;
    }

	return MM_FAIL;
}
