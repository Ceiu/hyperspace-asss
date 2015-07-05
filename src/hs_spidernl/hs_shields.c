#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_items.h"
#include "hscore_spawner.h"
#include "hscore_database.h"
#include "watchdamage.h"
#include "clientset.h"

typedef struct ArenaData
{
    int currentEffectID;

    struct
    {
        //The "Bubble" shield effect:
        int effectStartID;
        int effectEndID;
        int effectXOffset;
        int effectYOffset;

        //The shield strength (two way!) bar:
        int shieldBarUpID; //Enabled while shield is not recharging
        int shieldBarDownID;
        int shieldSegmentStartID;
        int shieldSegmentEndID;

        //These are derived from the actual configs:
        int shieldSegmentLeftStartID;
        int shieldSegmentRightStartID;
        int shieldSegmentsPerSide;
        int shieldBarEnabled;
    } lvzConfig;

    //hscore_spawner interface
    Ihscorespawner *spawner;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}
#define SHIELDBAR_MAXSEGMENTS 200

#define PlayerData PlayerData_
typedef struct PlayerData
{
    struct
    {
        int shieldstrength;
        int shieldrecharge; //Shield strength recharge per 100 ticks (1 second)

        //Drain per damage type
        int bulletdrain;
        int bulletupdrain;
        int bombdrain;
        int ebombdrain;
        int burstdrain;
        int shrapdrain;
    } ItemProperties;

    int recharging;
    double strength;

    ticks_t lastPosPacket;
    int hasRechargeTimer;

    //Shield bar
    int visibleState; //0 = Up, 1 = Cooldown
    int visibleSegments; //Per side!
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Interfaces
local Imodman *mm;
local Ihscoreitems *items;
local Iwatchdamage *watchdamage;
local Iplayerdata *pd;
local Iclientset *clientset;
local Iconfig *cfg;
local Imainloop *ml;
local Ihscoredatabase *db;
local Iarenaman *aman;
local Iobjects *objs;
local Ilogman *lm;

//Prototypes
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void ppkCB(Player *p, struct C2SPosition *pos);
local void playerDamageCB(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count);
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value);
local void playerActionCB(Player *p, int action, Arena *arena);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local int shieldRechargeTimer(void* param);

local void updatePlayerData(Player *p, ShipHull *hull, int lock, int forceRecharge);
local void readConfig(Arena *arena);
local void updateShieldBar(Player *p, int action);
local inline int max(int a, int b);

local void getInterfaces();
local int checkInterfaces();
local void releaseInterfaces();

//Callbacks
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    PlayerData *pdata = getPlayerData(killed);

    if (pdata->recharging)
    {
        pdata->recharging = 0;

        ArenaData *adata = getArenaData(killed->arena);
        if (adata->spawner) //This should be true, but just in case..
           adata->spawner->resendOverrides(killed);
    }
    pdata->strength = pdata->ItemProperties.shieldstrength;
}

local void ppkCB(Player *p, struct C2SPosition *pos)
{
    //Used for extrapolation of the shield effect
    PlayerData *pdata = getPlayerData(p);
    pdata->lastPosPacket = current_ticks();
}

local void playerDamageCB(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count)
{
    //Don't bother if they're dead
    if (p->flags.is_dead) return;

    PlayerData *pdata = getPlayerData(p);
    if (pdata->ItemProperties.shieldstrength <= 0) return; //No shield
    if (pdata->recharging) return; //Recharging shield

    int showShield = 0;

    int i;
    for (i = 0; i < count; i++)
    {
        int level = s2cdamage->damage[i].weapon.level;
        int type = s2cdamage->damage[i].weapon.type;

        if (type == W_BULLET || type == W_BOUNCEBULLET)
        {
            if (pdata->ItemProperties.bulletdrain > 0)
            {
                pdata->strength -= pdata->ItemProperties.bulletdrain;
                showShield = 1;
            }
            if (level > 0 && pdata->ItemProperties.bulletupdrain > 0)
            {
                pdata->strength -= pdata->ItemProperties.bulletupdrain * level;
                showShield = 1;
            }
        }
        if (type == W_BOMB || type == W_PROXBOMB || type == W_THOR)
        {
            if (pdata->ItemProperties.bombdrain > 0)
            {
                pdata->strength -= pdata->ItemProperties.bombdrain;
                showShield = 1;
            }
        }
        if (type == W_BURST)
        {
            if (pdata->ItemProperties.burstdrain > 0)
            {
                pdata->strength -= pdata->ItemProperties.burstdrain;
                showShield = 1;
            }
        }
        if (type == 15) //Active shrapnel; inactive shrapnel can't be detected.
        {               //Level is always 0 here, and can't be reliably
                        //calculated (damage might be 0) :(
            if (pdata->ItemProperties.shrapdrain > 0)
            {
                pdata->strength -= pdata->ItemProperties.shrapdrain;
                showShield = 1;
            }
        }

        if (pdata->strength < 0)
        {
           //Disable shields
           pdata->recharging = 1;

           //Resend damage values
           ArenaData *adata = getArenaData(p->arena);
           if (adata->spawner) //Just in case
              adata->spawner->resendOverrides(p);

           break;
        }
    }

    if (showShield)
    {
        //Show shield effect at (extrapolated) player location
        ticks_t lastPosPacket = pdata->lastPosPacket;
        if (lastPosPacket == 0) lastPosPacket = current_ticks();

        int x = p->position.x + ((double)(TICK_DIFF(current_ticks(), lastPosPacket) * p->position.xspeed) / 1000);
        int y = p->position.y + ((double)(TICK_DIFF(current_ticks(), lastPosPacket) * p->position.yspeed) / 1000);

        ArenaData *adata = getArenaData(arena);

        if (adata->lvzConfig.effectStartID < 0 || adata->lvzConfig.effectEndID < 0)
           return;

        Target t;
        t.type = T_ARENA;
        t.u.arena = arena;

        objs->Move(&t, adata->currentEffectID, x + adata->lvzConfig.effectXOffset,
           y + adata->lvzConfig.effectYOffset, 0, 0);
        objs->Toggle(&t, adata->currentEffectID, 1);

        adata->currentEffectID++;
        if (adata->currentEffectID > adata->lvzConfig.effectEndID)
           adata->currentEffectID = adata->lvzConfig.effectStartID;
    }
}

local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    PlayerData *pdata = getPlayerData(p);

    //No shields
    if (items->getPropertySumOnShipSet(p, ship, shipset, "shieldstrength", 0) <= 0) return init_value;
    if (pdata->recharging) return init_value; //Recharging shields

    if (strcmp(prop, "bulletdamage") == 0)
    {
        int bulletshield = items->getPropertySumOnShipSet(p, ship, shipset, "bulletshield", 0);
        return max(1, init_value - bulletshield);
    }
    if (strcmp(prop, "bulletdamageup") == 0)
    {
        int bulletupshield = items->getPropertySumOnShipSet(p, ship, shipset, "bulletupshield", 0);
        return max(1, init_value - bulletupshield);
    }
    if (strcmp(prop, "bombdamage") == 0)
    {
        int bombshield = items->getPropertySumOnShipSet(p, ship, shipset, "bombshield", 0);
        return max(1, init_value - bombshield);
    }
    if (strcmp(prop, "ebombdamage") == 0)
    {
        int ebombshield = items->getPropertySumOnShipSet(p, ship, shipset, "ebombshield", 0);
        return max(1, init_value - ebombshield);
    }
    if (strcmp(prop, "burstdamage") == 0)
    {
        int burstshield = items->getPropertySumOnShipSet(p, ship, shipset, "burstshield", 0);
        return max(1, init_value - burstshield);
    }
    if (strcmp(prop, "shrapdamage") == 0)
    {
        int shrapshield = items->getPropertySumOnShipSet(p, ship, shipset, "shrapshield", 0);
        return max(1, init_value - shrapshield);
    }

    return init_value;
}

local Ahscorespawner spawnAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_SPAWNER)

	getOverrideValueCB
};

local void playerActionCB(Player *p, int action, Arena *arena)
{
    if (action == PA_ENTERARENA)
    {
        watchdamage->ModuleWatch(p, 1);
    }
    if (action == PA_LEAVEARENA)
	{
		ml->ClearTimer(shieldRechargeTimer, p);
		//updateShieldBar(p, 2); //Do LVZs get auto disabled?
        //ModuleWatch is automatically disabled.
	}
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, 0, 0);
}

local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), 1, 1);
}

//Timers
local int shieldRechargeTimer(void* param) //every 10 centiseconds/ticks.
{
    Player *p = (Player*)param;

    //Some safety checks..
    if (!p) return 0;
    if (!IS_STANDARD(p)) return 0;

    PlayerData *pdata = getPlayerData(p);
    if (pdata->strength >= pdata->ItemProperties.shieldstrength)
    {
        updateShieldBar(p, 0);
        return 1;
    }

    pdata->strength += (pdata->ItemProperties.shieldrecharge / 10.0);
    if (pdata->strength >= pdata->ItemProperties.shieldstrength)
    {
       //Make sure it's not higher than the maximum shieldstrength
       pdata->strength = pdata->ItemProperties.shieldstrength;

       //If the shield was disabled, re-enable it.
       if (pdata->recharging)
       {
           pdata->recharging = 0;

           //Resend damage values
           ArenaData *adata = getArenaData(p->arena);
           if (adata->spawner) //Just in case
              adata->spawner->resendOverrides(p);
       }
    }

    updateShieldBar(p, 0);

    return 1;
}

//Misc
local void updatePlayerData(Player *p, ShipHull *hull, int lock, int forceRecharge)
{
    PlayerData *pdata = getPlayerData(p);


    if(!hull || hull != db->getPlayerCurrentHull(p))
    {
        if (pdata->hasRechargeTimer)
        {
            //"De-init" the shield bar
            updateShieldBar(p, 2);

             //Clear timer
            ml->ClearTimer(shieldRechargeTimer, p);
            pdata->hasRechargeTimer = 0;
        }
        return;
    }

    int oldShieldStrength = pdata->ItemProperties.shieldstrength;

    if (lock == 1) db->lock();
    pdata->ItemProperties.shieldstrength = items->getPropertySumOnHull(p, hull, "shieldstrength", 0);
    pdata->ItemProperties.shieldrecharge = items->getPropertySumOnHull(p, hull, "shieldrecharge", 0);

    pdata->ItemProperties.bulletdrain   = items->getPropertySumOnHull(p, hull, "bulletdrain", 0);
    pdata->ItemProperties.bulletupdrain = items->getPropertySumOnHull(p, hull, "bulletupdrain", 0);
    pdata->ItemProperties.bombdrain     = items->getPropertySumOnHull(p, hull, "bombdrain", 0);
    pdata->ItemProperties.ebombdrain    = items->getPropertySumOnHull(p, hull, "ebombdrain", 0);
    pdata->ItemProperties.burstdrain    = items->getPropertySumOnHull(p, hull, "burstdrain", 0);
    pdata->ItemProperties.shrapdrain    = items->getPropertySumOnHull(p, hull, "shrapdrain", 0);
    if (lock == 1) db->unlock();

    if (pdata->ItemProperties.shieldstrength > 0)
    {
        if (forceRecharge || pdata->ItemProperties.shieldstrength != oldShieldStrength)
        {
            pdata->strength = pdata->ItemProperties.shieldstrength;

            if (pdata->recharging)
            {
                pdata->recharging = 0;
                ArenaData *adata = getArenaData(p->arena);
                if (adata->spawner)
                   adata->spawner->resendOverrides(p);
            }
        }

        if (!pdata->hasRechargeTimer)
        {
            //"Init" the shield bar
            updateShieldBar(p, 1);

            //Create a timer
            ml->SetTimer(shieldRechargeTimer, 10, 10, p, p);
            pdata->hasRechargeTimer = 1;
        }
    }
    else
    {
        if (pdata->hasRechargeTimer)
        {
            //"De-init" the shield bar
            updateShieldBar(p, 2);

            //Clear timer
            ml->ClearTimer(shieldRechargeTimer, p);
            pdata->hasRechargeTimer = 0;
        }
    }
}

local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    adata->lvzConfig.effectStartID = cfg->GetInt(ch, "HS_Shields", "effectLvzStartID", -1);
    adata->lvzConfig.effectEndID   = cfg->GetInt(ch, "HS_Shields", "effectLvzEndID", -1);
    adata->lvzConfig.effectXOffset = cfg->GetInt(ch, "HS_Shields", "effectLvzXOffset", 0);
    adata->lvzConfig.effectYOffset = cfg->GetInt(ch, "HS_Shields", "effectLvzYOffset", 0);
    adata->currentEffectID         = adata->lvzConfig.effectStartID;

    adata->lvzConfig.shieldBarUpID        = cfg->GetInt(ch, "HS_Shields", "shieldBarUpID", -1);
    adata->lvzConfig.shieldBarDownID      = cfg->GetInt(ch, "HS_Shields", "shieldBarDownID", -1);
    adata->lvzConfig.shieldSegmentStartID = cfg->GetInt(ch, "HS_Shields", "shieldSegmentStartID", -1);
    adata->lvzConfig.shieldSegmentEndID   = cfg->GetInt(ch, "HS_Shields", "shieldSegmentEndID", -1);

    if (adata->lvzConfig.shieldBarUpID > 0 &&
       adata->lvzConfig.shieldBarDownID > 0 &&
       adata->lvzConfig.shieldSegmentStartID > 0 &&
       adata->lvzConfig.shieldSegmentEndID > 0)
    {
       int dif = adata->lvzConfig.shieldSegmentEndID - adata->lvzConfig.shieldSegmentStartID + 1;
       if (dif > 0 && dif <= SHIELDBAR_MAXSEGMENTS && !(dif & 1))
       {
           adata->lvzConfig.shieldBarEnabled = 1;
           adata->lvzConfig.shieldSegmentsPerSide = dif / 2;

           adata->lvzConfig.shieldSegmentRightStartID = adata->lvzConfig.shieldSegmentStartID;
           adata->lvzConfig.shieldSegmentRightStartID += adata->lvzConfig.shieldSegmentsPerSide;
           adata->lvzConfig.shieldSegmentLeftStartID = adata->lvzConfig.shieldSegmentRightStartID - 1;
           return;
       }
    }
    adata->lvzConfig.shieldBarEnabled = 0;
}

local void updateShieldBar(Player *p, int action) //Called by the recharge timer
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->lvzConfig.shieldBarEnabled)
       return;

    short ids[SHIELDBAR_MAXSEGMENTS + 2]; //+2 for visibleState
    char ons[SHIELDBAR_MAXSEGMENTS + 2];
    int size = 0;

    PlayerData *pdata = getPlayerData(p);

    int visibleSegments = 0;
    if (action == 0)
    {
        visibleSegments = max(0, adata->lvzConfig.shieldSegmentsPerSide *
            pdata->strength / pdata->ItemProperties.shieldstrength);
    }
    if (action == 1) //Start
    {
        visibleSegments = adata->lvzConfig.shieldSegmentsPerSide;
    }
    if (action == 2) //Stop
    {
        visibleSegments = 0;
    }

    if (visibleSegments != pdata->visibleSegments)
    {
        int i;

        if (visibleSegments < pdata->visibleSegments)
        {
            //Disable the 'extra' segments
            for (i = visibleSegments; i < pdata->visibleSegments; i++)
            {
                ids[size] = adata->lvzConfig.shieldSegmentLeftStartID - i;
                ids[size+1] = adata->lvzConfig.shieldSegmentRightStartID + i;

                ons[size] = 0;
                ons[size+1] = 0;

                size += 2;
            }
        }
        else
        {
            //Enable the segments that aren't on yet
            for (i = pdata->visibleSegments; i < visibleSegments; i++)
            {
                ids[size] = adata->lvzConfig.shieldSegmentLeftStartID - i;
                ids[size+1] = adata->lvzConfig.shieldSegmentRightStartID + i;

                ons[size] = 1;
                ons[size+1] = 1;

                size += 2;
            }
        }

        pdata->visibleSegments = visibleSegments;
    }

    //Update state
    if (action == 1) //Start
    {
        if (pdata->recharging)
        {
            ids[size] = adata->lvzConfig.shieldBarDownID;
            ons[size] = 1;
            ++size;
        }
        else
        {
            ids[size] = adata->lvzConfig.shieldBarUpID;
            ons[size] = 1;
            ++size;
        }

        pdata->visibleState = pdata->recharging;
    }
    else if (action == 2) //Stop
    {
        //Just disable both even if they're not both on. Better safe than sorry.
        ids[size] = adata->lvzConfig.shieldBarUpID;
        ids[size+1] = adata->lvzConfig.shieldBarDownID;

        ons[size] = 0;
        ons[size+1] = 0;

        size += 2;
    }
    else if (pdata->recharging != pdata->visibleState)
    {
        pdata->visibleState = pdata->recharging;

        ids[size] = adata->lvzConfig.shieldBarUpID;
        ids[size+1] = adata->lvzConfig.shieldBarDownID;

        if (pdata->visibleState) //Just started cooldown
        {
            ons[size] = 0;
            ons[size+1] = 1;
        }
        else //Just ended cooldown
        {
            ons[size] = 1;
            ons[size+1] = 0;
        }
        size += 2;
    }

    if (size > 0)
    {
        Target target;
	    target.type = T_PLAYER;
	    target.u.p = p;

        objs->ToggleSet(&target, ids, ons, size);
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

//Module/asss stuff
local void getInterfaces()
{
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    watchdamage = mm->GetInterface(I_WATCHDAMAGE, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    clientset = mm->GetInterface(I_CLIENTSET, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    objs = mm->GetInterface(I_OBJECTS, ALLARENAS);
    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
}
local int checkInterfaces()
{
    if (items && watchdamage && pd && clientset && cfg && ml && db &&
       aman && objs && lm)
       return 1;
    return 0;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(watchdamage);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(clientset);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(ml);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(objs);
    mm->ReleaseInterface(lm);
}

EXPORT const char info_hs_shields[] = "v2.0 by Spidernl";

EXPORT int MM_hs_shields(int action, Imodman *mm_, Arena *arena)
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
	else if (action == MM_UNLOAD) //Note that players will keep their shielded
	{	              //Damage values until shipchange if the module is unloaded.
		//Get rid of any timers
		ml->ClearTimer(shieldRechargeTimer, NULL);

        //Free data
        aman->FreeArenaData(adkey);
        pd->FreePlayerData(pdkey);

        //Release interfaces
        releaseInterfaces();

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena);
        adata->spawner = mm->GetInterface(I_HSCORE_SPAWNER, arena);
        if (!adata->spawner)
           return MM_FAIL;

        readConfig(arena);

        mm->RegCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);
        mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
		//mm->RegCallback(CB_SHIPSET_CHANGED, shipsetChangedCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_PPK, ppkCB, arena);
        mm->RegCallback(CB_KILL, killCB, arena);

        mm->RegAdviser(&spawnAdviser, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        mm->UnregAdviser(&spawnAdviser, arena);

        mm->UnregCallback(CB_PPK, ppkCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
		//mm->UnregCallback(CB_SHIPSET_CHANGED, shipsetChangedCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);
        mm->UnregCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->UnregCallback(CB_KILL, killCB, arena);

        //Get rid of any shield recharge timers in this arena
        Player *p;
        Link *link;

        pd->Lock();
		FOR_EACH_PLAYER(p)
		{
			if (p->arena == arena)
			{
				ml->ClearTimer(shieldRechargeTimer, p);
			}
		}
		pd->Unlock();

		ArenaData *adata = getArenaData(arena);
        mm->ReleaseInterface(adata->spawner);

        return MM_OK;
    }

    return MM_FAIL;
}
