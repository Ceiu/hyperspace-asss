#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "hs_util_energy.h"

// Interfaces
local Imodman *mm;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Iplayerdata *pd;

#define PlayerData PlayerData_
typedef struct PlayerData
{
    struct
    {
        int cloakMasking;
        int xRadarRange;

        bool areaCloak;
        int areaCloakMasking;
        int areaCloakRange;

        bool cloakBooster;
        int cloakBoostPower;
    } ItemProperties;

    int areaCloakedMasking;

    bool utilityState;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Prototypes
local void editPPK(Player *p, struct C2SPosition *pos);
local int editIndividualPPK(Player *p, Player *t, struct C2SPosition *pos, int *extralen);

local void utilityStateChangedCB(Player *p, int state, u8 reason);
local void playerActionCB(Player *p, int action, Arena *arena);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local void updatePlayerData(Player *p, ShipHull *hull, bool lock);
local long lhypot(register long dx, register long dy);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Advisers
local void editPPK(Player *p, struct C2SPosition *pos)
{
    if (!IS_STANDARD(p))
       return;

    if (pos->weapon.type == W_NULL)
    {
        Link *link;
        Player *i;

        PlayerData *pdata = getPlayerData(p);
        PlayerData *idata;

        long distance;
        int highestAreaCloakMasking = 0;

        FOR_EACH_PLAYER(i)
        {
            if (i->arena != p->arena)
               continue;
            if (i->p_freq != p->p_freq)
               continue;
            if (i->flags.is_dead)
               continue;
            if (i == p)
               continue;

            idata = getPlayerData(i);

            if (!idata->ItemProperties.areaCloak) continue;
            if (!idata->utilityState) continue;
            if (idata->ItemProperties.areaCloakMasking <= highestAreaCloakMasking) continue;

            distance = lhypot(abs(pos->x - i->position.x), abs(pos->y - i->position.y));
            if (idata->ItemProperties.areaCloakRange > distance)
            {
                highestAreaCloakMasking = idata->ItemProperties.areaCloakMasking;
            }
        }

        pdata->areaCloakedMasking = highestAreaCloakMasking;
    }
}

local int editIndividualPPK(Player *p, Player *t, struct C2SPosition *pos, int *extralen)
{
    if (p->p_freq == t->p_freq) //Why cloak teammates?
       return 0;
    if (t->p_ship == SHIP_SPEC)
       return 0; //Spectators see cloakers.
    if (p->position.status & STATUS_UFO)
       return 0; //Leave UFO players alone, there's probably a reason for it to be on.

    PlayerData *pdata = getPlayerData(p);
    PlayerData *tdata = getPlayerData(t);

    u8 oldStatus = pos->status;

    if (pos->weapon.type == W_NULL)
    {
        int cloakMasking = 0;

        if (pos->status & STATUS_CLOAK)
        {
            if (!IS_HUMAN(p)) //PD 'quickfix': a fake player that's cloaked. Check if it's a turret for someone.
            {
		        if (p->p_attached != -1)
		        {
                    //use the playerdata of the person we're attached to..
                    Player *owner = pd->PidToPlayer(p->p_attached);
                    if (owner)
                    {
                        pdata = getPlayerData(owner);
                    }
                }
            }

            cloakMasking = pdata->ItemProperties.cloakMasking;

            //Cloak boosting utility
            if (pdata->ItemProperties.cloakBooster && pdata->utilityState)
               cloakMasking += pdata->ItemProperties.cloakBoostPower;
        }

        cloakMasking += pdata->areaCloakedMasking;

        if (cloakMasking > 0)
        {
            if (!(t->position.status & STATUS_XRADAR))
            {
                pos->status |= STATUS_CLOAK;
            }
            else
            {
                double xRadarRange = (double) (tdata->ItemProperties.xRadarRange) / ((double) (cloakMasking) / 100);
                long distance = lhypot(abs(pos->x - t->position.x), abs(pos->y - t->position.y));

                if (xRadarRange < distance)
                {
                    pos->status |= STATUS_CLOAK;
                }
                else
                {
                    pos->status &= ~STATUS_CLOAK;
                }
            }
        }
        else
        {
            pos->status &= ~STATUS_CLOAK;
        }
    }

	//Make sure stealth/ufo are always on when cloaked,
	//and stealth is always off when not cloaked.
	if (pos->status & STATUS_CLOAK)
	{
		pos->status |= STATUS_STEALTH;
		pos->status |= STATUS_UFO;
	}
	else
	{
		pos->status &= ~STATUS_STEALTH;
	}

    return pos->status != oldStatus;
}
local Appk PPKAdviser =
{
	ADVISER_HEAD_INIT(A_PPK)

	editPPK,
	editIndividualPPK
};

//Callbacks
local void utilityStateChangedCB(Player *p, int state, u8 reason)
{
    PlayerData *pdata = getPlayerData(p);
    pdata->utilityState = (state > 0);
}

local void playerActionCB(Player *p, int action, Arena *arena)
{
    if (action == PA_ENTERARENA)
    {
        updatePlayerData(p, NULL, false);
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
local void updatePlayerData(Player *p, ShipHull *hull, bool lock)
{
    if (!IS_STANDARD(p) || !hull || hull != db->getPlayerCurrentHull(p))
       return;

    PlayerData *pdata = getPlayerData(p);

    if (lock == true)
       db->lock();
    //Regular cloak and xradar
    pdata->ItemProperties.cloakMasking = items->getPropertySumOnHull(p, hull, "cloakmasking", 0);
    pdata->ItemProperties.xRadarRange = items->getPropertySumOnHull(p, hull, "xradarrange", 0);

    //Area cloak
    pdata->ItemProperties.areaCloak = (items->getPropertySumOnHull(p, hull, "areacloak", 0) > 0);
    pdata->ItemProperties.areaCloakMasking= items->getPropertySumOnHull(p, hull, "areacloakmasking", 0);
    pdata->ItemProperties.areaCloakRange= items->getPropertySumOnHull(p, hull, "areacloakrange", 0);

    //Active cloak boosting
    pdata->ItemProperties.cloakBooster = (items->getPropertySumOnHull(p, hull, "cloakbooster", 0) > 0);
    pdata->ItemProperties.cloakBoostPower = items->getPropertySumOnHull(p, hull, "cloakboostpower", 0);
    if (lock == true)
       db->unlock();
}

local long lhypot(register long dx, register long dy)
{
	register unsigned long r, dd;
	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initial hypotenuse guess */
	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));
	if (r == 0) return (long)r;

	/* converge 3 times */
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}

//Interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
}
local bool checkInterfaces()
{
    if (items && pd && db)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(db);
}

EXPORT const char info_hs_cloak[] = "hs_cloak v1.0 by Spidernl\n";
EXPORT int MM_hs_cloak(int action, Imodman *mm_, Arena *arena)
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

        pdkey = pd->AllocatePlayerData(sizeof(struct PlayerData));

        if (pdkey == -1) //Memory check
        {
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
        mm->RegAdviser(&PPKAdviser, arena);

        mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        mm->UnregAdviser(&PPKAdviser, arena);

        mm->UnregCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);

        return MM_OK;
    }

	return MM_FAIL;
}
