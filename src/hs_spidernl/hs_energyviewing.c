#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"

local Iarenaman *aman;
local Ihscoredatabase *db;
local Imodman *mm;
local Iplayerdata *pd;
local Ihscoreitems *items;
local Iconfig *cfg;
local Ilogman *lm;

//Structs
typedef struct ArenaData
{
	bool attached;
	struct
	{
        int hideEnergyDuration; //How long do we prevent energy viewing for?
    } config;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

#define PlayerData PlayerData_
typedef struct PlayerData
{
    int energyScrambling; /* Energy scrambling 'level'. If more than or equal
                             to another player's energy viewing 'level',
                             don't show energy to them. */
    int energyViewing;    /* Energy viewing 'level'. See above. */
    bool hideEnergy;      /* Whether or not we're hiding energy through
                             an item or event or whatever. */
    int hideEnergyTick;   /* The tick we started hiding energy */
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Prototypes
local void PPKCB(Player *p, struct C2SPosition *pos);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void arenaActionCB(Arena *arena, int action);

local void updatePlayerData(Player *p, ShipHull *hull, bool lock);
local int editPPK(Player *p, Player *t, struct C2SPosition *pos, int *extralen);
local void readConfig(Arena *arena);

local void getInterfaces();
local int checkInterfaces();
local void releaseInterfaces();

//Callbacks
local void PPKCB(Player *p, struct C2SPosition *pos)
{
    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

    if (!IS_STANDARD(p)) return;
    if (pos->weapon.type == W_DECOY && adata->config.hideEnergyDuration > 0)
    {
        pdata->hideEnergy = true;
        pdata->hideEnergyTick = current_ticks();
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
local void arenaActionCB(Arena *arena, int action)
{
	if (action == AA_CONFCHANGED)
	{
		readConfig(arena);
	}
}

//Misc/'actual' functionality of module
local void updatePlayerData(Player *p, ShipHull *hull, bool lock)
{
	if (!hull || hull != db->getPlayerCurrentHull(p)) {
	  return;
	}


    PlayerData *pdata = getPlayerData(p);
    if (p->p_ship == SHIP_SPEC)
    {
        pdata->energyScrambling = 0;
        pdata->energyViewing = 0;
        return;
    }

    if (lock) db->lock();
    pdata->energyScrambling = items->getPropertySumOnHull(p, hull, "energyscrambling", 0);
    pdata->energyViewing = items->getPropertySumOnHull(p, hull, "energyviewing", 0);
    if (lock) db->unlock();
}

local int editPPK(Player *p, Player *t, struct C2SPosition *pos, int *extralen)
{
	if (IS_HUMAN(p) && IS_HUMAN(t))
	{
        if (p->p_ship == SHIP_SPEC || t->p_ship == SHIP_SPEC) return FALSE;
        if (p->p_freq == t->p_freq) return FALSE;

        PlayerData *pdata = getPlayerData(p);
        ArenaData *adata = getArenaData(p->arena);

        if (pdata->hideEnergy)
        {
            if (TICK_DIFF(current_ticks(), pdata->hideEnergyTick) >
               adata->config.hideEnergyDuration) //Stop hiding energy?
            {
               pdata->hideEnergy = false;
            }
        }

        /* Hide energy if there's a special reason to, or if the player
               sending the position packet has a powerful enough scrambler */
        PlayerData *targetpdata = getPlayerData(t);
        if (pdata->hideEnergy || (pdata->energyScrambling > 0 &&
            pdata->energyScrambling >= targetpdata->energyViewing))
        {
            *extralen = 0;
            return TRUE;
        }
	}
	return FALSE;
}

local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    adata->config.hideEnergyDuration = cfg->GetInt(arena->cfg, "EnergyBlanker", "DecoyBlankTime", 0);
}

local Appk PPKAdviser =
{
	ADVISER_HEAD_INIT(A_PPK)

	NULL,
	editPPK
};

//Module/asss stuff
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
}
local int checkInterfaces()
{
    if (aman && pd && db && items && cfg && lm)
       return 1;
    return 0;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(lm);
}

EXPORT int MM_hs_energyviewing(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		getInterfaces();
		if (checkInterfaces())
		{
            adkey = aman->AllocateArenaData(sizeof(struct ArenaData));
            pdkey = pd->AllocatePlayerData(sizeof(struct PlayerData));

            //Memory check
            if (adkey == -1 || pdkey == -1)
            {
                //Free allocated data
                if (adkey  != -1)
                   aman->FreeArenaData(adkey);
                if (pdkey != -1)
                   pd->FreePlayerData(pdkey);

                releaseInterfaces();
                return MM_FAIL;
            }
            return MM_OK;
        }

        releaseInterfaces();
        return MM_FAIL;
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
        if (!adata->attached)
           adata->attached = true;
        else
            return MM_FAIL;

        readConfig(arena);

        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ARENAACTION, arenaActionCB, arena);
        mm->RegCallback(CB_PPK, PPKCB, arena);
        mm->RegAdviser(&PPKAdviser, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);
        if (adata->attached)
           adata->attached = false;
        else
            return MM_FAIL;

        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ARENAACTION, arenaActionCB, arena);
        mm->UnregCallback(CB_PPK, PPKCB, arena);
        mm->UnregAdviser(&PPKAdviser, arena);

        return MM_OK;
    }
	return MM_FAIL;
}
