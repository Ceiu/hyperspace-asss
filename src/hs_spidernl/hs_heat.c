#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "asss.h"
#include "hs_heat.h"
#include "hscore.h"
#include "hscore_shipnames.h"
#include "hscore_database.h"
#include "hscore_spawner.h"

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
    Ihscorespawner *spawner;

    bool attached;

    struct
    {
        struct
        {
            //The 'box' for the entire hud
            int HUDID;

            //Digits. The '0' digit is the start ID, the '9' digit is the start ID plus 9.
            int leftDigitStartID;
            int rightDigitStartID;

            int overheatIndicatorID;

            //** If any of the above values are -1, that specific 'part' is disabled. **
        } LVZ;

        struct
	    {
            int defaultMaxHeat;
		    int defaultHeatDissipation;
		    int defaultHeatShutdownTime;
	    } PerShip[8];

	    struct
	    {
            int defaultGunHeat;
            int defaultBombHeat;
        } Global;
    } Config;
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
    //LVZ - if the 'updated' left digit/right digit aren't the same, update graphics.
    int leftDigit;
    int rightDigit;

    struct
    {
        int maxHeat; //Maximum heat. If current heat >= this value, we overheat.

        double heatDissipation; //Reduction of heat per second.
        int heatShutdownTime;

        double gunHeat; //Heat generated per shot by guns.
        double bombHeat; //Heat generated per shot by bombs.
    } ItemProperties;

    bool hasTimer;

    double heat;

    bool overheated;
    ticks_t overheated_time;

    struct
    {
        double heatDissipation;
        int heatShutdownTime;

        double gunHeat;
        double bombHeat;

        int super;
    } Modifiers; //Set by other modules
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Lock, use around PlayerData access.
local pthread_mutex_t pdata_mtx;

local void lock()
{
    pthread_mutex_lock(&pdata_mtx);
}
local void unlock()
{
    pthread_mutex_unlock(&pdata_mtx);
}

//Prototypes
local double GetHeat(Player *p);
local int IsOverheated(Player *p);
local void ModHeat(Player *p, double amount);
local void ModHeatNoLock(Player *p, double amount);
local void SetHeat(Player *p, double heat);
local void SetHeatNoLock(Player *p, double heat);
local void do_SetHeat(Player *p, PlayerData *pdata, double heat);
local void SetOverheated(Player *p, int overheated);
local void SetOverheatedNoLock(Player *p, int overheated);
local void ModHeatDissipationModifier(Player *p, double amount);
local void ModHeatShutdownTimeModifier(Player *p, int amount);
local void ModGunHeatModifier(Player *p, double amount);
local void ModBombHeatModifier(Player *p, double amount);
local void SetSuper(Player *p, int on);

local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value);

local void ppkCB(Player *p, struct C2SPosition *pos);
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local int heatTimer(void* param);

local void setHUDOn(Player *p, int on);
local void setHUDDigitsOn(Player *p, int on);
local void updateOverheatIndicator(Player *p);
local void updateHUDDigits(Player *p);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB, bool resetHeat);
local void readConfig(ConfigHandle ch, ArenaData *adata);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Interface functions
local double GetHeat(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return 0;

    lock();
    PlayerData *pdata = getPlayerData(p);
    int heat = pdata->heat;
    unlock();

    return heat;
}

local int IsOverheated(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return -1;

    lock();
    PlayerData *pdata = getPlayerData(p);
    int overheated = pdata->overheated;
    unlock();

    return overheated;
}

local void ModHeat(Player *p, double amount)
{
    lock();
    ModHeatNoLock(p, amount);
    unlock();
}
local void ModHeatNoLock(Player *p, double amount)
{
    PlayerData *pdata = getPlayerData(p);
    do_SetHeat(p, pdata, pdata->heat + amount);
}

local void SetHeat(Player *p, double heat)
{
    lock();
    SetHeatNoLock(p, heat);
    unlock();
}
local void SetHeatNoLock(Player *p, double heat)
{
    PlayerData *pdata = getPlayerData(p);
    do_SetHeat(p, pdata, heat);
}
local void do_SetHeat(Player *p, PlayerData *pdata, double heat)
{
    pdata->heat = heat;

    if (pdata->heat < 0) pdata->heat = 0;
    else if (pdata->heat > pdata->ItemProperties.maxHeat)
    {
        SetOverheatedNoLock(p, 1);
        pdata->heat = pdata->ItemProperties.maxHeat;
    }

    updateHUDDigits(p);
}

local void SetOverheated(Player *p, int overheated)
{
    lock();
    SetOverheatedNoLock(p, overheated);
    unlock();
}
local void SetOverheatedNoLock(Player *p, int overheated)
{
    PlayerData *pdata = getPlayerData(p);
    if (pdata->overheated == overheated) return;

    pdata->overheated = overheated;
    if (overheated)
    {
        pdata->overheated_time = current_ticks();
    }

    DO_CBS(CB_HEAT_STATE_CHANGED, p->arena, HeatStateChanged, (p, overheated));

    ArenaData *adata = getArenaData(p->arena);

    if (adata->spawner)
       adata->spawner->resendOverrides(p);

    updateOverheatIndicator(p);
}

local void ModHeatDissipationModifier(Player *p, double amount)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    pdata->Modifiers.heatDissipation += amount;
}

local void ModHeatShutdownTimeModifier(Player *p, int amount)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    pdata->Modifiers.heatShutdownTime += amount;
}

local void ModGunHeatModifier(Player *p, double amount)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    pdata->Modifiers.gunHeat += amount;
}

local void ModBombHeatModifier(Player *p, double amount)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    pdata->Modifiers.bombHeat += amount;
}

local void SetSuper(Player *p, int on)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    int add = -1;
    if (on > 0) add = 1;

    PlayerData *pdata = getPlayerData(p);
    pdata->Modifiers.super += add;
}

local Ihsheat hsheat =
{
    INTERFACE_HEAD_INIT(I_HS_HEAT, "hs_heat")

    GetHeat,
    IsOverheated,
    ModHeat,
    ModHeatNoLock,
    SetHeat,
    SetHeatNoLock,
    SetOverheated,
    SetOverheatedNoLock,
    ModHeatDissipationModifier,
    ModHeatShutdownTimeModifier,
    ModGunHeatModifier,
    ModBombHeatModifier,
    SetSuper,
};


//Advisers
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->overheated) return init_value;

    //Disable guns and bombs.
    if (strcmp(prop, "bulletenergy") == 0)
    {
        return 10000;
    }
    if (strcmp(prop, "multienergy") == 0)
    {
        return 10000;
    }
    if (strcmp(prop, "bombenergy") == 0)
    {
        return 10000;
    }

    return init_value;
}

local Ahscorespawner overrideAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_SPAWNER)

	getOverrideValueCB
};

//Callbacks
local void ppkCB(Player *p, struct C2SPosition *pos)
{
    if (!IS_STANDARD(p)) return;
    if (p->flags.is_dead) return;
    if (pos->weapon.type == W_NULL) return;

    if (pos->weapon.type == W_BULLET || pos->weapon.type == W_BOUNCEBULLET)
    {
        lock();

        PlayerData *pdata = getPlayerData(p);
        if (pdata->Modifiers.super <= 0)
        {
            ModHeatNoLock(p, pdata->ItemProperties.gunHeat + pdata->Modifiers.gunHeat);
        }

        unlock();
    }
    else if (pos->weapon.type == W_BOMB || pos->weapon.type == W_PROXBOMB)
    {
        lock();

        PlayerData *pdata = getPlayerData(p);
        if (pdata->Modifiers.super <= 0)
        {
            ModHeatNoLock(p, pdata->ItemProperties.bombHeat + pdata->Modifiers.bombHeat);
        }

        unlock();
    }
}

local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    if (!killed) return;
    if (!IS_STANDARD(killed)) return;

    SetHeat(killed, 0);
    SetOverheated(killed, 0);
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, false, false);
}

local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    if (newship == SHIP_SPEC)
    {
        if (oldship != SHIP_SPEC)
        {
            setHUDOn(p, 0);

            lock();
            setHUDDigitsOn(p, 0);
            unlock();

            SetOverheated(p, 0);
            updateOverheatIndicator(p);
        }
    }
    else
    {
        if (oldship == SHIP_SPEC)
        {
            setHUDOn(p, 1);

            lock();
            setHUDDigitsOn(p, 1);
            unlock();

            SetOverheated(p, 0);
            updateOverheatIndicator(p);
        }
    }

    updatePlayerData(p, db->getPlayerShipHull(p, newship), true, true);
}

//Timers
local int heatTimer(void* param)
{
	Arena *arena = (Arena*)param;
	if (!arena) return 0;

	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(p, arena)
	{
        if (!IS_STANDARD(p)) continue;
        if (p->p_ship == SHIP_SPEC) continue;
        if (p->flags.is_dead) continue;

        lock();
        PlayerData *pdata = getPlayerData(p);

        int dissipation = pdata->ItemProperties.heatDissipation + pdata->Modifiers.heatDissipation;

        //Subtract heat dissipation.
        ModHeatNoLock(p, -dissipation);

        //If overheated, check if we should reset back to non-overheated status.
        if (pdata->overheated)
        {
            if (TICK_DIFF(current_ticks(), pdata->overheated_time) >= pdata->ItemProperties.heatShutdownTime + pdata->Modifiers.heatShutdownTime)
            {
                SetHeatNoLock(p, 0);
                SetOverheatedNoLock(p, 0);
            }
        }
        unlock();
    }
	pd->Unlock();

    return 1;
}

//HUD display code
local void setHUDOn(Player *p, int on)
{
    ArenaData *adata = getArenaData(p->arena);
    if (adata->Config.LVZ.HUDID == -1) return;

    Target t;
    t.type = T_PLAYER;
    t.u.p = p;

    objs->Toggle(&t, adata->Config.LVZ.HUDID, on);
}

local void setHUDDigitsOn(Player *p, int on)
{
    ArenaData *adata = getArenaData(p->arena);
    if (adata->Config.LVZ.leftDigitStartID == -1 || adata->Config.LVZ.rightDigitStartID == -1) return;

    Target t;
    t.type = T_PLAYER;
    t.u.p = p;

    if (on)
    {
        objs->Toggle(&t, adata->Config.LVZ.leftDigitStartID, 1);
        objs->Toggle(&t, adata->Config.LVZ.rightDigitStartID, 1);
    }
    else
    {
        PlayerData *pdata = getPlayerData(p);

        objs->Toggle(&t, adata->Config.LVZ.leftDigitStartID + pdata->leftDigit, 0);
        objs->Toggle(&t, adata->Config.LVZ.rightDigitStartID + pdata->rightDigit, 0);
    }
}

local void updateOverheatIndicator(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (adata->Config.LVZ.overheatIndicatorID == -1) return;

    Target t;
    t.type = T_PLAYER;
    t.u.p = p;

    PlayerData *pdata = getPlayerData(p);
    if (pdata->overheated)
    {
        objs->Toggle(&t, adata->Config.LVZ.overheatIndicatorID, 1);
    }
    else
    {
        objs->Toggle(&t, adata->Config.LVZ.overheatIndicatorID, 0);
    }
}

local void updateHUDDigits(Player *p)
{
    ArenaData *adata = getArenaData(p->arena);
    if (adata->Config.LVZ.leftDigitStartID == -1 || adata->Config.LVZ.rightDigitStartID == -1) return;

    PlayerData *pdata = getPlayerData(p);

    int leftDigit;
    int rightDigit;

	if (pdata->overheated) { leftDigit = 9; rightDigit = 9; }
	else
	{
		int maxheat = pdata->ItemProperties.maxHeat;
		if (maxheat <= 0) maxheat = 1; //Avoid negative max heat & divide by zero.

		double heatDivided = ((double)(pdata->heat) / maxheat) * 10; //No div0 \o/
		int whole = (int) heatDivided;

		if (whole >= 10) { leftDigit = 9; rightDigit = 9; } //Special case
		else
		{
			leftDigit = whole;

			heatDivided -= whole;
			heatDivided *= 10;

			rightDigit = heatDivided;
		}
	}

    if (leftDigit == pdata->leftDigit && rightDigit == pdata->rightDigit) return; //Nothing to do here.

    //Arrays are size 4 because at most we "turn off" the existing two IDs, and "turn on" the new two IDs.
    short ids[4];
    char ons[4];
    int index = 0;

    if (leftDigit != pdata->leftDigit)
    {
        ids[index] = adata->Config.LVZ.leftDigitStartID + pdata->leftDigit;
        ons[index] = 0;
        index++;

        ids[index] = adata->Config.LVZ.leftDigitStartID + leftDigit;
        ons[index] = 1;
        index++;
    }

    if (rightDigit != pdata->rightDigit)
    {
        ids[index] = adata->Config.LVZ.rightDigitStartID + pdata->rightDigit;
        ons[index] = 0;
        index++;

        ids[index] = adata->Config.LVZ.rightDigitStartID + rightDigit;
        ons[index] = 1;
        index++;
    }

    pdata->leftDigit = leftDigit;
    pdata->rightDigit = rightDigit;

    Target t;
    t.type = T_PLAYER;
    t.u.p = p;

    objs->ToggleSet(&t, ids, ons, index);
}

//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB, bool resetHeat)
{
    if (!IS_STANDARD(p))
       return;

    if (!hull || hull != db->getPlayerCurrentHull(p))
	  return;

    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

    //Get item properties
    if (lockDB == true)
       db->lock();

    pdata->ItemProperties.maxHeat = items->getPropertySumOnHull(p, hull, "maxheat", adata->Config.PerShip[hull->ship].defaultMaxHeat);
    pdata->ItemProperties.heatDissipation = (items->getPropertySumOnHull(p, hull, "heatdissipation", adata->Config.PerShip[hull->ship].defaultHeatDissipation) / 100.0);
    pdata->ItemProperties.heatShutdownTime= items->getPropertySumOnHull(p, hull, "heatshutdowntime", adata->Config.PerShip[hull->ship].defaultHeatShutdownTime);

    double gunHeatPercent = 0.01 * items->getPropertySumOnHull(p, hull, "gunheatpercent", 100);
    double bombHeatPercent = 0.01 * items->getPropertySumOnHull(p, hull, "bombheatpercent", 100);

    pdata->ItemProperties.gunHeat = items->getPropertySumOnHull(p, hull, "gunheat", adata->Config.Global.defaultGunHeat) * gunHeatPercent;
    pdata->ItemProperties.bombHeat = items->getPropertySumOnHull(p, hull, "bombheat", adata->Config.Global.defaultBombHeat) * bombHeatPercent;

    if (lockDB == true)
       db->unlock();
}

local void readConfig(ConfigHandle ch, ArenaData *adata)
{
    adata->Config.LVZ.HUDID = cfg->GetInt(ch, "HS_Heat", "hudID", -1);

    adata->Config.LVZ.leftDigitStartID = cfg->GetInt(ch, "HS_Heat", "leftDigitStartID", -1);
    adata->Config.LVZ.rightDigitStartID = cfg->GetInt(ch, "HS_Heat", "rightDigitStartID", -1);

    adata->Config.LVZ.overheatIndicatorID = cfg->GetInt(ch, "HS_Heat", "overheatIndicatorID", -1);

    int ship;
    for (ship = 0; ship < 8; ship++)
    {
        adata->Config.PerShip[ship].defaultMaxHeat = cfg->GetInt(ch, shipNames[ship], "defaultMaxHeat", -1);
        adata->Config.PerShip[ship].defaultHeatDissipation= cfg->GetInt(ch, shipNames[ship], "defaultHeatDissipation", -1);
        adata->Config.PerShip[ship].defaultHeatShutdownTime = cfg->GetInt(ch, shipNames[ship], "defaultHeatShutdownTime", -1);
    }

    adata->Config.Global.defaultGunHeat = cfg->GetInt(ch, "HS_Heat", "DefaultGunHeat", 60);
    adata->Config.Global.defaultBombHeat = cfg->GetInt(ch, "HS_Heat", "DefaultBombHeat", 150);
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

EXPORT const char info_hs_heat[] = "heat_heat v1.1 by Spidernl\n";
EXPORT int MM_hs_heat(int action, Imodman *mm_, Arena *arena)
{
    if (action == MM_LOAD)
	{
		mm = mm_;

	    pthread_mutex_init(&pdata_mtx, NULL);

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

        pthread_mutex_destroy(&pdata_mtx);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena);

        adata->spawner = mm->GetInterface(I_HSCORE_SPAWNER, arena);
        if (!adata->spawner) return MM_FAIL;

        mm->RegAdviser(&overrideAdviser, arena);

        readConfig(arena->cfg, adata);

        mm->RegCallback(CB_PPK, ppkCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_KILL, killCB, arena);

        mm->RegInterface(&hsheat, arena);

        ml->SetTimer(heatTimer, 1, 1, arena, arena);

        adata->attached = true;

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        mm->UnregInterface(&hsheat, arena);

        ml->ClearTimer(heatTimer, arena);

        mm->UnregCallback(CB_PPK, ppkCB, arena);
        mm->UnregCallback(CB_KILL, killCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);

        ArenaData *adata = getArenaData(arena);

        mm->UnregAdviser(&overrideAdviser, arena);
        mm->ReleaseInterface(adata->spawner);

        adata->attached = false;

        return MM_OK;
    }

	return MM_FAIL;
}
