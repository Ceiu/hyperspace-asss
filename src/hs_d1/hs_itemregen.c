/*
 * Hyperspace Item Regeneration
 * 09.05.28 D1st0rt
 * TODO:
 * Proper clock timing
 * check multi-item generators
 */

#define MODULENAME hs_itemregen
#define SZMODULENAME "hs_itemregen"
#define INTERFACENAME I_HS_ITEMREGEN

///////////////////////////////////////////////////////////////////////
// Includes
///////////////////////////////////////////////////////////////////////

#include "stdio.h"
#include "string.h"
#include "akd_asss.h"
#include "hs_itemregen.h"
#include "hscore.h"
#include "hscore_items.h"
#include "hscore_database.h"
#include "clocks.h"

///////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////

DEF_PARENA_TYPE
    int cfg_RespawnTime;

ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
    Clock *Timer;
    LinkedList *RegenList;

ENDDEF_PPLAYER_TYPE;

typedef struct RegenInfo
{
    Player *Player;
    int Ship;
    InventoryEntry *Generator;
    int Count;
} RegenInfo;

///////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////

EXPORT const char info_hs_itemregen[] = "09.06.11 by D1st0rt <d1st0rter@gmail.com>";

local pthread_mutex_t globalmutex;

// Non-Standard Interfaces
local Ihscoreitems *items;
local Ihscoredatabase *hsdb;
local Iclocks *clocks;

///////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////
local void InitPlayer(Player *p);
local void CleanupPlayer(Player *p);
local void ResetTimers(Player *p);
local void StartAllGenerators(Player *p);
local RegenInfo *GetRegenInfo(Player *p, Item *gen);
local void StartGenerator(RegenInfo *info);
local void StopGenerator(RegenInfo *info);

///////////////////////////////////////////////////////////////////////
// Callbacks
///////////////////////////////////////////////////////////////////////
local void ItemsChanged(Player *p, ShipHull *hull);
local void PlayerAction(Player *p, int action, Arena *a);
local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void ArenaAction(Arena *a, int action);
local void PlayerKill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);

///////////////////////////////////////////////////////////////////////
// Timers
///////////////////////////////////////////////////////////////////////
local int RespawnPlayer(void *clos);
local int RegenItem(Clock *clock, int time, void *gen);

///////////////////////////////////////////////////////////////////////
// Commands
///////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////
// Entry Point
///////////////////////////////////////////////////////////////////////
EXPORT int MM_hs_itemregen(int action, Imodman *mm_, Arena *arena)
{
    MM_FUNC_HEADER();

    if (action == MM_LOAD)
    {
        mm = mm_;

        GET_USUAL_INTERFACES();

        // Non-standard interfaces
        GETINT(items, I_HSCORE_ITEMS);
        GETINT(hsdb, I_HSCORE_DATABASE);
        GETINT(clocks, I_CLOCKS);

        // Data
        REG_PARENA_DATA();
        BREG_PPLAYER_DATA();

        INIT_MUTEX(globalmutex);

        return MM_OK;
    }
    else if (action == MM_UNLOAD)
    {
        DESTROY_MUTEX(globalmutex);

        UNREG_PARENA_DATA();
        UNREG_PPLAYER_DATA();

Lfailload:
        //Non-standard interfaces
        RELEASEINT(items);
        RELEASEINT(hsdb);
        RELEASEINT(clocks);

        RELEASE_USUAL_INTERFACES();

        DO_RETURN();
    }

    else if (action == MM_ATTACH)
    {
        // Data
        ALLOC_ARENA_DATA(ad);
        ad->cfg_RespawnTime = cfg->GetInt(arena->cfg, "Kill", "EnterDelay", 200);
        // Attached interfaces

        // Callbacks
        mm->RegCallback(CB_PLAYERACTION, PlayerAction, arena);
        mm->RegCallback(CB_ARENAACTION, ArenaAction, arena);
        mm->RegCallback(CB_KILL, PlayerKill, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, ItemsChanged, arena);

        // Timers

        // Commands

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        // Timers
        ml->ClearTimer(RespawnPlayer, arena);

        // Callbacks
        mm->UnregCallback(CB_PLAYERACTION, PlayerAction, arena);
        mm->UnregCallback(CB_ARENAACTION, ArenaAction, arena);
        mm->UnregCallback(CB_KILL, PlayerKill, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, ItemsChanged, arena);

        // Commands

        // Attached interfaces

        // Data
        Player *p;
        Link *link;

        pd->Lock();
        FOR_EACH_PLAYER(p)
        {
            if(p->arena == arena)
            {
                CleanupPlayer(p);
            }
        }
        pd->Unlock();
        FREE_ARENA_DATA(ad);
        DO_RETURN();
    }

    return MM_FAIL;
}

///////////////////////////////////////////////////////////////////////
// Functions
///////////////////////////////////////////////////////////////////////
local void InitPlayer(Player *p)
{
    BDEF_PD(p);

    MYGLOCK;
    clocks->HoldForSynchronization();
    pdat->Timer = clocks->NewClock(CLOCK_COUNTSUP, current_millis());
    lm->LogP(L_ERROR, "hs_itemregen", p, "Starting clock");
    clocks->StartClock(pdat->Timer);
    clocks->DoneHolding();
    pdat->RegenList = LLAlloc();

    MYGUNLOCK;
}

local void CleanupPlayer(Player *p)
{
    BDEF_PD(p);

    MYGLOCK;
    clocks->HoldForSynchronization();
    clocks->StopClock(pdat->Timer);
    clocks->FreeClock(pdat->Timer);
    clocks->DoneHolding();

    LLEmpty(pdat->RegenList);
    LLFree(pdat->RegenList);
    MYGUNLOCK;
}

local RegenInfo *GetRegenInfo(Player *p, Item *gen)
{
    BDEF_PD(p);
    RegenInfo *ret = NULL;
    Link *link;
    RegenInfo *info;

    MYGLOCK;
    FOR_EACH(pdat->RegenList, info, link)
    {
        if(info->Generator->item == gen)
        {
            ret = info;
            break;
        }
    }
    MYGUNLOCK;

    return ret;
}

local void ResetTimers(Player *p)
{
    BDEF_PD(p);
    RegenInfo *info;
    Link *link;

    MYGLOCK;
    clocks->HoldForSynchronization();
    FOR_EACH(pdat->RegenList, info, link)
    {
        StopGenerator(info);
        StartGenerator(info);
    }
    clocks->SetClock(pdat->Timer, 1);
    clocks->DoneHolding();
    MYGUNLOCK;
}

local void StartAllGenerators(Player *p)
{
    BDEF_PD(p);

	// Patch for multiple ship sets
	ShipHull *hull = hsdb->getPlayerCurrentHull(p);

	if (hull) {
      LinkedList *inv = &hull->inventoryEntryList;

	  Link *link, *link2;
	  InventoryEntry *entry;
	  ItemTypeEntry *type;

	  lm->LogP(L_ERROR, "hs_itemregen", p, "Starting all generators");

	  MYGLOCK;
	  FOR_EACH(inv, entry, link)
	  {
		  RegenInfo *info = GetRegenInfo(p, entry->item);
		  if(!info)
		  {
			  FOR_EACH(&entry->item->itemTypeEntries, type, link2)
			  {
				  if(!strcmp(type->itemType->name, "Generator"))
				  {
					  info = amalloc(sizeof(RegenInfo));
					  info->Player = p;
					  info->Ship = p->p_ship;
					  info->Generator = entry;
					  info->Count = items->getItemCountOnHull(info->Player, info->Generator->item, hull);
					  LLAdd(pdat->RegenList, info);

					  StartGenerator(info);
					  break;
				  }
			  }
		  }
	  }
	  MYGUNLOCK;
	}
}

local void StartGenerator(RegenInfo *info)
{
    BDEF_PD(info->Player);
    Link *link;
    Property *prop;
    int interval = 60000;

    MYGLOCK;
    FOR_EACH(&info->Generator->item->propertyList, prop, link)
    {
        if(!strcmp(prop->name, "regen_time"))
        {
            interval = prop->value * 1000;
        }
    }
    MYGUNLOCK;

    interval /= info->Count;

    clocks->HoldForSynchronization();
    clocks->RegisterClockEvent(pdat->Timer, CLOCKEVENT_INTERVAL, interval, RegenItem, info);
    clocks->DoneHolding();

    info->Generator->data = 0;

    lm->LogP(L_ERROR, "hs_itemregen", info->Player, "Started generator for item %s with interval %d", info->Generator->item->name, interval);
}

local void StopGenerator(RegenInfo *info)
{
    BDEF_PD(info->Player);
    Item *item;
    Link *link;
    Player *p = info->Player;

    if(info)
    {
        clocks->HoldForSynchronization();
        clocks->ClearEventsWithClos(pdat->Timer, info);
        clocks->DoneHolding();

        MYGLOCK;
        FOR_EACH(&info->Generator->item->ammoUsers, item, link)
        {
            int count = items->getItemCount(p, item, info->Ship);
            if(count)
            {
                items->addItemCheckLimits(p, item, info->Ship, -count);
            }
        }
        MYGUNLOCK;

        lm->LogP(L_ERROR, "hs_itemregen", info->Player, "Stopped generator for item %s", info->Generator->item->name);
    }
    else
    {
        lm->LogP(L_WARN, "hs_itemregen", info->Player, "Tried to stop generator that is not in use.");
    }
}

///////////////////////////////////////////////////////////////////////
// Callbacks
///////////////////////////////////////////////////////////////////////
local void ItemsChanged(Player *p, ShipHull *hull)
{
    if (hsdb->getPlayerCurrentHull(p) != hull)
    {
        return; //don't really care in this case
    }

    BDEF_PD(p);
    Link *link;
    RegenInfo *info;

    MYGLOCK;
    FOR_EACH(pdat->RegenList, info, link)
    {
        int count = items->getItemCountOnHull(p, info->Generator->item, hull);
        if(count < 1)
        {
            StopGenerator(info);

            link = link->next;
            LLRemove(pdat->RegenList, info);
            afree(info);
        }
    }
    MYGUNLOCK;

    StartAllGenerators(p);
}

local void PlayerAction(Player *p, int action, Arena *a)
{
    if(action == PA_ENTERARENA)
    {
        InitPlayer(p);
    }
    else if(action == PA_LEAVEARENA)
    {
        CleanupPlayer(p);
    }
}

local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    BDEF_PD(p);
    RegenInfo *info;
    Link *link;

    MYGLOCK;
    FOR_EACH(pdat->RegenList, info, link)
    {
        StopGenerator(info);
        afree(info);
    }

    LLEmpty(pdat->RegenList);
    MYGUNLOCK;

    ResetTimers(p);

    if(newship != SHIP_SPEC && newfreq != p->arena->specfreq)
    {
        StartAllGenerators(p);
    }
}

local void ArenaAction(Arena *a, int action)
{
    if(action == AA_CONFCHANGED)
    {
        DEF_AD(a);
        ad->cfg_RespawnTime = cfg->GetInt(a->cfg, "Kill", "EnterDelay", ad->cfg_RespawnTime);
    }
}

local void PlayerKill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    DEF_AD(a);
    BDEF_PD(killed);

    clocks->HoldForSynchronization();
    clocks->StopClock(pdat->Timer);
    clocks->DoneHolding();

    ml->SetTimer(RespawnPlayer, ad->cfg_RespawnTime, 0, killed, a);
    lm->LogP(L_ERROR, "hs_itemregen", killed, "Pausing player clock");
}

///////////////////////////////////////////////////////////////////////
// Timers
///////////////////////////////////////////////////////////////////////
local int RespawnPlayer(void *clos)
{
    Player *p = (Player *)clos;
    lm->LogP(L_ERROR, "hs_itemregen", p, "Restarting player clock");
    BDEF_PD(p);
    clocks->HoldForSynchronization();
    clocks->StartClock(pdat->Timer);
    clocks->DoneHolding();

    MYGLOCK;
    Link *link;
    RegenInfo *info;
    FOR_EACH(pdat->RegenList, info, link)
    {
        info->Generator->data = 0;
    }
    MYGUNLOCK;

    return 0;
}

local int RegenItem(Clock *clock, int time, void *clos)
{
    RegenInfo *info = (RegenInfo *)clos;
    Player *p = info->Player;
    Item *item;
    Link *link;
    int count;

    lm->LogP(L_ERROR, "hs_itemregen", info->Player, "Regen callback");

    MYGLOCK;
    FOR_EACH(&info->Generator->item->ammoUsers, item, link)
    {
        count = info->Generator->data;
        lm->LogP(L_ERROR, "hs_itemregen", info->Player, "Generator data: %d", count);

        if(count < item->max)
        {
            lm->LogP(L_ERROR, "hs_itemregen", info->Player, "Generating %s", item->name);
            items->addItemCheckLimits(p, item, info->Ship, info->Count);
            items->triggerEventOnItem(p, item, info->Ship, "add");
            info->Generator->data++;
        }
    }
    MYGUNLOCK;

    return KEEP_EVENT;
}

///////////////////////////////////////////////////////////////////////
// Commands
///////////////////////////////////////////////////////////////////////
