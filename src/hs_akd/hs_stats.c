/*
	HS_STATS
	9 Jan 2010
	Author:
	- Justin "Arnk Kilo Dylie" Schwartz
	Contributors:

*/
/*
	Independent module authorized by the author for use in Hyperspace.
*/

#include "asss.h"

#define MODULENAME hs_stats
#define SZMODULENAME "hs_stats"
#define INTERFACENAME Ihs_stats

#define NOT_USING_SHIP_NAMES 1
#define NOT_USING_PDATA 1
#include "akd_asss.h"
#include "gamestats/gamestats.h"

DEF_PARENA_TYPE
	gamestat_type *stat_kills;
	gamestat_type *stat_deaths;
	gamestat_type *stat_teamkills;

	Igamestats *gs;
ENDDEF_PARENA_TYPE;

//callback
local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void flagreset(Arena *a, int freq, int points);

EXPORT const char info_hs_stats[] = "v1.0 by Arnk Kilo Dylie <kilodylie@rshl.org>";
EXPORT int MM_hs_stats(int action, Imodman *mm_, Arena *arena)
{
	BMM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		//store the provided Imodman interface.
		mm = mm_;

		//get all interfaces first. if a required interface is not available, jump to Lfailload and release the interfaces we did get, and return failure.
		GET_USUAL_INTERFACES();	//several interfaces used in many modules, there's no real harm in getting them even if they're not used

		//register per-arena and per-player data.
		BREG_PARENA_DATA();

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{

		UNREG_PARENA_DATA();

Lfailload:
		RELEASE_USUAL_INTERFACES();


		//returns MM_FAIL if we jumped from a failed GETINT or other MM_LOAD action, returns MM_OK if not.
		DO_RETURN();
	}


	else if (action == MM_ATTACH)
	{
		//allocate this arena's per-arena data.
		//ALLOC_ARENA_DATA(ad);
		GETARENAINT(ad->gs, I_GAMESTATS);

		ad->stat_kills = ad->gs->getStatType(arena, "K");
		ad->stat_deaths = ad->gs->getStatType(arena, "D");
		ad->stat_teamkills = ad->gs->getStatType(arena, "TK");

		//malloc other things in arena data.

		mm->RegCallback(CB_FLAGRESET, flagreset, arena);
		mm->RegCallback(CB_KILL, playerkill, arena);

		//finally, return success.

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		//unregister global commands, timers, and callbacks.
		//remember to clear ALL timers this arena was using even if they were not set in the MM_ATTACH phase..
		mm->UnregCallback(CB_FLAGRESET, flagreset, arena);
		mm->UnregCallback(CB_KILL, playerkill, arena);

Lfailattach:

		//release this arena's per-arena data
		//including all player's per-player data this module would use
		RELEASEINT(ad->gs);

		DO_RETURN();
	}


	return MM_FAIL;
}

local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	BDEF_AD(a);
	int freqA = killer->p_freq;
	int freqB = killed->p_freq;

	if (freqA < 90 || freqA > 91 || freqB < 90 || freqB > 91)
	{
		return;
	}

	if (freqA != freqB)
	{
		//game 0, period 1
		ad->gs->AddStat(a, killer, ad->stat_kills, 0, 1, killer->p_freq, 1);
	}
	else
	{
		//game 0, period 1
		ad->gs->AddStat(a, killer, ad->stat_teamkills, 0, 1, killer->p_freq, 1);
	}

	//game 0, period 1
	ad->gs->AddStat(a, killed, ad->stat_deaths, 0, 1, killed->p_freq, 1);
}

local void flagreset(Arena *a, int freq, int points)
{
	BDEF_AD(a);
	if (freq == -1)
		return;

	ad->gs->SpamStatsTable(a, 0, 1, 0); //arena, game 0, 1st period, all players
	ad->gs->ClearGame(a, 0); //reset game 0
}

