/*
	HS_GREENS
	26 Feb 2008
	Author:
	- Justin "Arnk Kilo Dylie" Schwartz
	Contributors:

*/
/*
	Independent module presently authorized by the author for use in Hyperspace.
*/

#include "asss.h"
#include "hscore.h"
#include "hscore_items.h"
#include "hscore_spawner.h"
#include "hscore_database.h"
#include <math.h>
#include <string.h>

#define MODULENAME hs_greens
#define SZMODULENAME "hs_greens"
#define INTERFACENAME Ihs_greens

#define NOT_USING_SHIP_NAMES 1
#define NOT_USING_PDATA 1
#include "akd_asss.h"

//other interfaces we want to use besides the usual
local Ihscoreitems *items;

//config values

//other globals
local pthread_mutex_t globalmutex;

//prototype all functions we will be using in the interface here. then define the interface, then prototype that other stuff.
local void loadConfig(Arena *a);
local void loadItemPointers(Arena *a);

#define PRIZE_ID_MAX 28

DEF_PARENA_TYPE
	Item *prizeItem[PRIZE_ID_MAX+1];
	int prizeItemMinimum[PRIZE_ID_MAX+1];
	int prizeItemMaximum[PRIZE_ID_MAX+1];
	int prizeIgnore[PRIZE_ID_MAX+1];
	char *prizeItemName[PRIZE_ID_MAX+1];

	Ihscorespawner *spawner;
ENDDEF_PARENA_TYPE;

//callback
local void arenaaction(Arena *a, int action);
local void playergreen(Player *p, int x, int y, int prize);


EXPORT const char info_hs_greens[] = "v1.0 by Arnk Kilo Dylie <kilodylie@rshl.org>";
EXPORT int MM_hs_greens(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		//store the provided Imodman interface.
		mm = mm_;

		//get all interfaces first. if a required interface is not available, jump to Lfailload and release the interfaces we did get, and return failure.
		GET_USUAL_INTERFACES();	//several interfaces used in many modules, there's no real harm in getting them even if they're not used

		GETINT(items, I_HSCORE_ITEMS);


		//register per-arena and per-player data.
		REG_PARENA_DATA();
		//REG_PPLAYER_DATA();

		//malloc and init anything else.

		//init a global mutex if you need one. you only need one if you have a global linkedlist, hashtable, or something cool like that.
		INIT_MUTEX(globalmutex);

		//register the interface if exposing one.
		//INIT_GLOBALINTERFACE();
		//zero out any functions that would be used on the arena level only for the global interface

		//finally, return success.
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		//first try to unregister the interface if exposing one.
		//UNREG_GLOBALINTERFACE();

		//unregister all timers anyway because we're cool.

		//clear the mutex if we were using it
		DESTROY_MUTEX(globalmutex);

		//free any other malloced data

		//unregister per-arena and per-player data
		UNREG_PARENA_DATA();

		//release interfaces last.
		//this is where GETINT jumps to if it fails.
Lfailload:
		RELEASE_USUAL_INTERFACES();
		RELEASEINT(items);


		//returns MM_FAIL if we jumped from a failed GETINT or other MM_LOAD action, returns MM_OK if not.
		DO_RETURN();
	}


	else if (action == MM_ATTACH)
	{
		//allocate this arena's per-arena data.
		lm->LogA(L_WARN, "hs_greens", arena, "attach");

		ALLOC_ARENA_DATA(ad);
		GETARENAINT(ad->spawner, I_HSCORE_SPAWNER);

		//malloc other things in arena data.

		lm->LogA(L_WARN, "hs_greens", arena, "load config");
		//register global commands, timers, and callbacks.
		loadConfig(arena);

		mm->RegCallback(CB_GREEN, playergreen, arena);
		mm->RegCallback(CB_ARENAACTION, arenaaction, arena);

		//finally, return success.

		lm->LogA(L_WARN, "hs_greens", arena, "attach success");
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		int i;
		//first try to unregister the interface if exposing one.
		//UNREG_ARENAINTERFACE();
		lm->LogA(L_WARN, "hs_greens", arena, "detach");
		//unregister global commands, timers, and callbacks.
		//remember to clear ALL timers this arena was using even if they were not set in the MM_ATTACH phase..
		mm->UnregCallback(CB_GREEN, playergreen, arena);
		mm->UnregCallback(CB_ARENAACTION, arenaaction, arena);

		for (i = 1; i < PRIZE_ID_MAX; ++i)
		{
			if (ad->prizeItemName[i])
			{
				afree(ad->prizeItemName[i]);
			}
		}

Lfailattach:

		//release this arena's per-arena data
		//including all player's per-player data this module would use
		RELEASEINT(ad->spawner);
		FREE_ARENA_DATA(ad);

		//returns MM_FAIL if we jumped from a failed GETARENAINT or other MM_ATTACH action, returns MM_OK if not.
		lm->LogA(L_WARN, "hs_greens", arena, "detach finished (fail=%i)", fail);
		DO_RETURN();
	}


	return MM_FAIL;
}

local void playergreen(Player *p, int x, int y, int prize)
{
	DEF_AD(p->arena);
	Item *item;
	int number;
	lm->LogP(L_WARN, "hs_greens", p, "green %i", prize);

	if (!ad)
	{
		lm->LogP(L_WARN, "hs_greens", p, "ad is 0! (%i)", p->arena?1:0);
		return;
	}

	if (prize <= 0 || prize > PRIZE_ID_MAX)
	{
		lm->LogP(L_WARN, "hs_greens", p, "received green outside of known IDs! (%i)", prize);
		return;
	}

	if (!ad->prizeItemName[prize])
	{
		return;
	}

	//if we haven't got an item here, probably means we haven't tried to load the item pointers
	if (!ad->prizeItem[prize])
	{
		loadItemPointers(p->arena);
	}

	item = ad->prizeItem[prize];
	if (!item)
	{
		lm->LogP(L_WARN, "hs_greens", p, "received green without associated item! (%i)", prize);
		return;
	}

	number = prng->Number(ad->prizeItemMinimum[prize], ad->prizeItemMaximum[prize]);
	if (number <= 0)
	{
		lm->LogP(L_WARN, "hs_greens", p, "prizing <= 0! (%i %i)", prize, number);
		return;
	}

	if (ad->spawner && ad->prizeIgnore[prize])
		ad->spawner->ignorePrize(p, prize);


	items->addItemCheckLimits(p, item, p->p_ship, number);
}

//body: playeraction
local void arenaaction(Arena *a, int action)
{
	if (action == AA_CONFCHANGED)
	{
		loadConfig(a);
	}
}

local void loadItemPointers(Arena *a)
{
	DEF_AD(a);
	int i;

	for (i = 1; i < PRIZE_ID_MAX; ++i)
	{
		if (ad->prizeItemName[i])
		{
			ad->prizeItem[i] = items->getItemByName(ad->prizeItemName[i], a);
		}
	}
}

local void loadConfig(Arena *a)
{
	const char * prizeItemName[PRIZE_ID_MAX+1];
	int i;
	DEF_AD(a);
	if (!ad)
	{
		lm->LogA(L_WARN, "hs_greens", a, "ad is 0 in loadconfig! (%i)", a?1:0);
		return;
	}


	prizeItemName[0] = "";
	prizeItemName[PRIZE_RECHARGE] = cfg->GetStr(a->cfg, "hs_greens", "recharge");
	prizeItemName[PRIZE_ENERGY] = cfg->GetStr(a->cfg, "hs_greens", "energy");
	prizeItemName[PRIZE_ROTATION] = cfg->GetStr(a->cfg, "hs_greens", "rotation");
	prizeItemName[PRIZE_STEALTH] = cfg->GetStr(a->cfg, "hs_greens", "stealth");
	prizeItemName[PRIZE_CLOAK] = cfg->GetStr(a->cfg, "hs_greens", "cloak");
	prizeItemName[PRIZE_XRADAR] = cfg->GetStr(a->cfg, "hs_greens", "xradar");
	prizeItemName[PRIZE_WARP] = cfg->GetStr(a->cfg, "hs_greens", "warp");
	prizeItemName[PRIZE_GUN] = cfg->GetStr(a->cfg, "hs_greens", "gun");
	prizeItemName[PRIZE_BOMB] = cfg->GetStr(a->cfg, "hs_greens", "bomb");
	prizeItemName[PRIZE_BOUNCE] = cfg->GetStr(a->cfg, "hs_greens", "bounce");
	prizeItemName[PRIZE_THRUST] = cfg->GetStr(a->cfg, "hs_greens", "thrust");
	prizeItemName[PRIZE_SPEED] = cfg->GetStr(a->cfg, "hs_greens", "speed");
	prizeItemName[PRIZE_FULLCHARGE] = cfg->GetStr(a->cfg, "hs_greens", "fullcharge");
	prizeItemName[PRIZE_SHUTDOWN] = cfg->GetStr(a->cfg, "hs_greens", "shutdown");
	prizeItemName[PRIZE_MULTIFIRE] = cfg->GetStr(a->cfg, "hs_greens", "multifire");
	prizeItemName[PRIZE_PROX] = cfg->GetStr(a->cfg, "hs_greens", "prox");
	prizeItemName[PRIZE_SUPER] = cfg->GetStr(a->cfg, "hs_greens", "super");
	prizeItemName[PRIZE_SHIELD] = cfg->GetStr(a->cfg, "hs_greens", "shield");
	prizeItemName[PRIZE_SHRAP] = cfg->GetStr(a->cfg, "hs_greens", "shrap");
	prizeItemName[PRIZE_ANTIWARP] = cfg->GetStr(a->cfg, "hs_greens", "antiwarp");
	prizeItemName[PRIZE_REPEL] = cfg->GetStr(a->cfg, "hs_greens", "repel");
	prizeItemName[PRIZE_BURST] = cfg->GetStr(a->cfg, "hs_greens", "burst");
	prizeItemName[PRIZE_DECOY] = cfg->GetStr(a->cfg, "hs_greens", "decoy");
	prizeItemName[PRIZE_THOR] = cfg->GetStr(a->cfg, "hs_greens", "thor");
	prizeItemName[PRIZE_MULTIPRIZE] = cfg->GetStr(a->cfg, "hs_greens", "multiprize");
	prizeItemName[PRIZE_BRICK] = cfg->GetStr(a->cfg, "hs_greens", "brick");
	prizeItemName[PRIZE_ROCKET] = cfg->GetStr(a->cfg, "hs_greens", "rocket");
	prizeItemName[PRIZE_PORTAL] = cfg->GetStr(a->cfg, "hs_greens", "portal");

	for (i = 1; i < PRIZE_ID_MAX; ++i)
	{
		if (prizeItemName[i])
			ad->prizeItemName[i] = astrdup(prizeItemName[i]);
		else
			ad->prizeItemName[i] = 0;
	}

	ad->prizeItemMinimum[PRIZE_RECHARGE] = cfg->GetInt(a->cfg, "hs_greens", "recharge_min", 0);
	ad->prizeItemMinimum[PRIZE_ENERGY] = cfg->GetInt(a->cfg, "hs_greens", "energy_min", 0);
	ad->prizeItemMinimum[PRIZE_ROTATION] = cfg->GetInt(a->cfg, "hs_greens", "rotation_min", 0);
	ad->prizeItemMinimum[PRIZE_STEALTH] = cfg->GetInt(a->cfg, "hs_greens", "stealth_min", 0);
	ad->prizeItemMinimum[PRIZE_CLOAK] = cfg->GetInt(a->cfg, "hs_greens", "cloak_min", 0);
	ad->prizeItemMinimum[PRIZE_XRADAR] = cfg->GetInt(a->cfg, "hs_greens", "xradar_min", 0);
	ad->prizeItemMinimum[PRIZE_WARP] = cfg->GetInt(a->cfg, "hs_greens", "warp_min", 0);
	ad->prizeItemMinimum[PRIZE_GUN] = cfg->GetInt(a->cfg, "hs_greens", "gun_min", 0);
	ad->prizeItemMinimum[PRIZE_BOMB] = cfg->GetInt(a->cfg, "hs_greens", "bomb_min", 0);
	ad->prizeItemMinimum[PRIZE_BOUNCE] = cfg->GetInt(a->cfg, "hs_greens", "bounce_min", 0);
	ad->prizeItemMinimum[PRIZE_THRUST] = cfg->GetInt(a->cfg, "hs_greens", "thrust_min", 0);
	ad->prizeItemMinimum[PRIZE_SPEED] = cfg->GetInt(a->cfg, "hs_greens", "speed_min", 0);
	ad->prizeItemMinimum[PRIZE_FULLCHARGE] = cfg->GetInt(a->cfg, "hs_greens", "fullcharge_min", 0);
	ad->prizeItemMinimum[PRIZE_SHUTDOWN] = cfg->GetInt(a->cfg, "hs_greens", "shutdown_min", 0);
	ad->prizeItemMinimum[PRIZE_MULTIFIRE] = cfg->GetInt(a->cfg, "hs_greens", "multifire_min", 0);
	ad->prizeItemMinimum[PRIZE_PROX] = cfg->GetInt(a->cfg, "hs_greens", "prox_min", 0);
	ad->prizeItemMinimum[PRIZE_SUPER] = cfg->GetInt(a->cfg, "hs_greens", "super_min", 0);
	ad->prizeItemMinimum[PRIZE_SHIELD] = cfg->GetInt(a->cfg, "hs_greens", "shield_min", 0);
	ad->prizeItemMinimum[PRIZE_SHRAP] = cfg->GetInt(a->cfg, "hs_greens", "shrap_min", 0);
	ad->prizeItemMinimum[PRIZE_ANTIWARP] = cfg->GetInt(a->cfg, "hs_greens", "antiwarp_min", 0);
	ad->prizeItemMinimum[PRIZE_REPEL] = cfg->GetInt(a->cfg, "hs_greens", "repel_min", 0);
	ad->prizeItemMinimum[PRIZE_BURST] = cfg->GetInt(a->cfg, "hs_greens", "burst_min", 0);
	ad->prizeItemMinimum[PRIZE_DECOY] = cfg->GetInt(a->cfg, "hs_greens", "decoy_min", 0);
	ad->prizeItemMinimum[PRIZE_THOR] = cfg->GetInt(a->cfg, "hs_greens", "thor_min", 0);
	ad->prizeItemMinimum[PRIZE_MULTIPRIZE] = cfg->GetInt(a->cfg, "hs_greens", "multiprize_min", 0);
	ad->prizeItemMinimum[PRIZE_BRICK] = cfg->GetInt(a->cfg, "hs_greens", "brick_min", 0);
	ad->prizeItemMinimum[PRIZE_ROCKET] = cfg->GetInt(a->cfg, "hs_greens", "rocket_min", 0);
	ad->prizeItemMinimum[PRIZE_PORTAL] = cfg->GetInt(a->cfg, "hs_greens", "portal_min", 0);

	ad->prizeItemMaximum[PRIZE_RECHARGE] = cfg->GetInt(a->cfg, "hs_greens", "recharge_max", 0);
	ad->prizeItemMaximum[PRIZE_ENERGY] = cfg->GetInt(a->cfg, "hs_greens", "energy_max", 0);
	ad->prizeItemMaximum[PRIZE_ROTATION] = cfg->GetInt(a->cfg, "hs_greens", "rotation_max", 0);
	ad->prizeItemMaximum[PRIZE_STEALTH] = cfg->GetInt(a->cfg, "hs_greens", "stealth_max", 0);
	ad->prizeItemMaximum[PRIZE_CLOAK] = cfg->GetInt(a->cfg, "hs_greens", "cloak_max", 0);
	ad->prizeItemMaximum[PRIZE_XRADAR] = cfg->GetInt(a->cfg, "hs_greens", "xradar_max", 0);
	ad->prizeItemMaximum[PRIZE_WARP] = cfg->GetInt(a->cfg, "hs_greens", "warp_max", 0);
	ad->prizeItemMaximum[PRIZE_GUN] = cfg->GetInt(a->cfg, "hs_greens", "gun_max", 0);
	ad->prizeItemMaximum[PRIZE_BOMB] = cfg->GetInt(a->cfg, "hs_greens", "bomb_max", 0);
	ad->prizeItemMaximum[PRIZE_BOUNCE] = cfg->GetInt(a->cfg, "hs_greens", "bounce_max", 0);
	ad->prizeItemMaximum[PRIZE_THRUST] = cfg->GetInt(a->cfg, "hs_greens", "thrust_max", 0);
	ad->prizeItemMaximum[PRIZE_SPEED] = cfg->GetInt(a->cfg, "hs_greens", "speed_max", 0);
	ad->prizeItemMaximum[PRIZE_FULLCHARGE] = cfg->GetInt(a->cfg, "hs_greens", "fullcharge_max", 0);
	ad->prizeItemMaximum[PRIZE_SHUTDOWN] = cfg->GetInt(a->cfg, "hs_greens", "shutdown_max", 0);
	ad->prizeItemMaximum[PRIZE_MULTIFIRE] = cfg->GetInt(a->cfg, "hs_greens", "multifire_max", 0);
	ad->prizeItemMaximum[PRIZE_PROX] = cfg->GetInt(a->cfg, "hs_greens", "prox_max", 0);
	ad->prizeItemMaximum[PRIZE_SUPER] = cfg->GetInt(a->cfg, "hs_greens", "super_max", 0);
	ad->prizeItemMaximum[PRIZE_SHIELD] = cfg->GetInt(a->cfg, "hs_greens", "shield_max", 0);
	ad->prizeItemMaximum[PRIZE_SHRAP] = cfg->GetInt(a->cfg, "hs_greens", "shrap_max", 0);
	ad->prizeItemMaximum[PRIZE_ANTIWARP] = cfg->GetInt(a->cfg, "hs_greens", "antiwarp_max", 0);
	ad->prizeItemMaximum[PRIZE_REPEL] = cfg->GetInt(a->cfg, "hs_greens", "repel_max", 0);
	ad->prizeItemMaximum[PRIZE_BURST] = cfg->GetInt(a->cfg, "hs_greens", "burst_max", 0);
	ad->prizeItemMaximum[PRIZE_DECOY] = cfg->GetInt(a->cfg, "hs_greens", "decoy_max", 0);
	ad->prizeItemMaximum[PRIZE_THOR] = cfg->GetInt(a->cfg, "hs_greens", "thor_max", 0);
	ad->prizeItemMaximum[PRIZE_MULTIPRIZE] = cfg->GetInt(a->cfg, "hs_greens", "multiprize_max", 0);
	ad->prizeItemMaximum[PRIZE_BRICK] = cfg->GetInt(a->cfg, "hs_greens", "brick_max", 0);
	ad->prizeItemMaximum[PRIZE_ROCKET] = cfg->GetInt(a->cfg, "hs_greens", "rocket_max", 0);
	ad->prizeItemMaximum[PRIZE_PORTAL] = cfg->GetInt(a->cfg, "hs_greens", "portal_max", 0);

	ad->prizeIgnore[PRIZE_RECHARGE] = cfg->GetInt(a->cfg, "hs_greens", "recharge_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_ENERGY] = cfg->GetInt(a->cfg, "hs_greens", "energy_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_ROTATION] = cfg->GetInt(a->cfg, "hs_greens", "rotation_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_STEALTH] = cfg->GetInt(a->cfg, "hs_greens", "stealth_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_CLOAK] = cfg->GetInt(a->cfg, "hs_greens", "cloak_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_XRADAR] = cfg->GetInt(a->cfg, "hs_greens", "xradar_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_WARP] = cfg->GetInt(a->cfg, "hs_greens", "warp_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_GUN] = cfg->GetInt(a->cfg, "hs_greens", "gun_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_BOMB] = cfg->GetInt(a->cfg, "hs_greens", "bomb_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_BOUNCE] = cfg->GetInt(a->cfg, "hs_greens", "bounce_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_THRUST] = cfg->GetInt(a->cfg, "hs_greens", "thrust_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_SPEED] = cfg->GetInt(a->cfg, "hs_greens", "speed_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_FULLCHARGE] = cfg->GetInt(a->cfg, "hs_greens", "fullcharge_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_SHUTDOWN] = cfg->GetInt(a->cfg, "hs_greens", "shutdown_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_MULTIFIRE] = cfg->GetInt(a->cfg, "hs_greens", "multifire_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_PROX] = cfg->GetInt(a->cfg, "hs_greens", "prox_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_SUPER] = cfg->GetInt(a->cfg, "hs_greens", "super_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_SHIELD] = cfg->GetInt(a->cfg, "hs_greens", "shield_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_SHRAP] = cfg->GetInt(a->cfg, "hs_greens", "shrap_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_ANTIWARP] = cfg->GetInt(a->cfg, "hs_greens", "antiwarp_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_REPEL] = cfg->GetInt(a->cfg, "hs_greens", "repel_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_BURST] = cfg->GetInt(a->cfg, "hs_greens", "burst_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_DECOY] = cfg->GetInt(a->cfg, "hs_greens", "decoy_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_THOR] = cfg->GetInt(a->cfg, "hs_greens", "thor_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_MULTIPRIZE] = cfg->GetInt(a->cfg, "hs_greens", "multiprize_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_BRICK] = cfg->GetInt(a->cfg, "hs_greens", "brick_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_ROCKET] = cfg->GetInt(a->cfg, "hs_greens", "rocket_donotignore", 0)?0:1;
	ad->prizeIgnore[PRIZE_PORTAL] = cfg->GetInt(a->cfg, "hs_greens", "portal_donotignore", 0)?0:1;
}
