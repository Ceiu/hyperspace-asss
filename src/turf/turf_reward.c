/* -----------------------------------------------------------------------------
 *
 *  Turf Reward Module for ASSS - an extensible scoring module for Turf Zones
 *                  by GiGaKiLLeR <gigamon@hotmail.com>
 *
 * -----------------------------------------------------------------------------
 *
 * This module began as an effort towards creating what I referred to as
 * "a very specialized version of the Turf Zone reward system"
 *
 * It eventually became apparent that what I really wanted was to create was a
 * way to plug different types of algorithms as scoring methods.  Slowly it has
 * evolved into what you see here.  Separate modules for ASSS can be used to
 * register onto the I_TURFREWARD_POINTS interface, which turf_reward calls.
 * In other words, anyone can write a module that defines a new scoring
 * algorithm, and have turf_reward do the rest.  Additionally, turf_reward
 * comes equipped with a fairly broad range features, including weighted flags,
 * flag recoveries, various other options, and more to come...
 *
 * My greatest thanks to Grelminar for allowing me the chance to do any of this,
 * for taking the time to look over and fix my code, for allowing me to practice
 * my newbie[ish] C programming skills, and much much more.
 *
 * Some Noteworthy Turf Reward System Specs:
 *
 * - Pluggable reward algorithm system
 *     New scoring modules can be written by registering with the
 *     I_TURFREWARD_POINTS interface.  This interface is called by turf_reward.
 *     The CalcReward(...) function (defined by separate scoring modules) is
 *     sent a pointer to the specific arena's turf_reward data when called by
 *     the turf_reward module.  The function can use this data, plus any data
 *     it might have collected on it's own, to calculate scores to be awarded to
 *     players.  These scores should be inputted into the predefined TurfArena
 *     data structure.
 *
 * - Weighted flag system
 *      1. Each flag has a weight.  This weight can change based on the period
 *         of ownership.  The number of weights that can be set and the
 *         corresponding value to each weight can be set through the config.
 *         Through this, the longer a flag is held the more it is "worth".  The
 *         weight settings are not limited to that, however.  In theory is it
 *         possible to set weights to lower over time.  Negative weights are
 *         strictly forbidden, I see no advantage in having them anyways.
 *      2. Recoveries: when a previously owned flag is tagged back before a set
 *         number of dings or period of time elapsed (both can be set using the
 *         conf) the flag's full "worth"/weight prior to losing it is restored.
 *
 *         Example trying to show SOME of the possibilities:
 *         ** Depending on settings in cfg **
 *             My team owns a flag worth 4 WU (weight units) upon next ding. The
 *             enemy team tags that flag.  It is now worth 1 WU upon next ding
 *             to the enemy team.
 *             Case 1: Ding.  The enemy team recieves the 1 WU for that flag
 *                     since they own it.
 *             Case 2: I tag the flag back to my team.  Flag is worth 4 WU as
 *                     before it was lost (NOT 1 WU).
 *                     Ding.  I am rewarded with 4 WU for that flag, and now it
 *                     is worth 5 WU upon next ding.
 *             Case 3: Ding.  The enemy team recieves the 1 WU for that flag
 *                     since they own it.  The flag is now worth 2 WU for the
 *                     enemy team upon next ding. I tag the flag back to my
 *                     team.  The flag, now owned by my team, is again worth
 *                     4 WU as before we lost it to the enemy (NOT 1 WU).  Ding.
 *                     I am rewarded 4 WU for that flag, and now it is worth
 *                     5 WU upon the next ding.  However, note that the enemy
 *                     team has their chance to recover the flag for 2 WU.
 *                     Also, note: the module is not limited to 2 teams having
 *                     a history of previous ownership of a flag (as in this
 *                     example).  There can be an arbitrary number of teams that
 *                     have had previous ownership and the ability to 'recover'
 *                     the flag for their respective previous weight/worth.
 *
 *
 * A couple details on the default turf_reward_points scoring module:
 * (Useful info for those wanting to use the built in scoring module.)
 * - Scoring is not based on # of flags your team owns but rather the # of
 *   weights to those flags (quite a bit more advanced than simple periodic)
 *      The algorithm (TR_STYLE_STANDARD) is:
 *
 *      PerCapita = # of weights per person on team
 *                = (# of weights / # ppl on the team)
 *
 *      Sum up all team's PerCapita's.
 *
 *      Jackpot = # players * 200
 *      NOTE: remember this is for TR_STYLE_STANDARD, each scoring algorithm
 *            can define it's own way of scoring.  I'm not saying that this
 *            is a good way to decide how many points to award, it's likely not
 *            to be.
 *
 *      % of Jackpot to recieve = (PerCapita / Sum of PerCapita's) * 100
 *
 *      Points to be awarded
 *          = ( Jackpot * (% of jackpot to recieve/100) ) / (# ppl on team)
 *
 * - Goals of this scoring method
 *      1.  The more people on a team.  The less points awarded.  All other
 *          variables held constant.  This is an attemptsto eliminate the
 *          incentive to fill your team up to the max, but rather figure out the
 *          optimal # of players (skillwise/teamwise) to get the maximium
 *          benefit/points with respect to the amount of opposition (other teams).
 *      2.  The longer flags are held the more they are worth. (incentive to
 *          "Hold your Turf!").  Territory you already own may be worth more to
 *          keep, rather than losing it while taking over a new area. The
 *          decision completely up to the player as always.
 *      3.  Lost territory can be regained at full worth if reclaimed before a
 *          set number of dings. Additional strategy as certain areas on the map
 *          can be worth more to reclaim/attack.  Again, leaving the door wide
 *          open to the player's decision on what's best to do in each situation.
 *
 * dist: public
 * -----------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "asss.h"                /* necessary include to connect the module */
#include "persist.h"
#include "turf_reward.h"

#define KEY_TR_OWNERS 1337       /* for persistant flag data */

/* easy calls for mutex */
#define LOCK_STATUS(arena) \
	pthread_mutex_lock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))


/* interfaces to various other modules I will probably have to use */
local Imodman     *mm;         /* to get interfaces */
local Iplayerdata *playerdata; /* player data */
#define pd playerdata
local Iarenaman   *arenaman;   /* arena manager, to get conf handle for arena */
local Iflagcore   *flagcore;   /* to access flag tags and info */
local Iconfig     *config;     /* conf (for arena .conf) services */
local Istats      *stats;      /* stat / score services */
local Ilogman     *logman;     /* logging services */
local Imainloop   *mainloop;   /* main loop - for setting the ding timer */
local Imapdata    *mapdata;    /* get to number of flags in each arena */
local Ichat       *chat;       /* message players */
local Icmdman     *cmdman;     /* for command handling */
local Inet        *net;        /* to send packet for score update */
local Ipersist    *persist;    /* persistant flag data */


/* global (to this file) declarations */
local int trkey;  /* turf reward data for every arena */
local int mtxkey; /* to keep things thread safe when accessing TR data */
local ArenaPersistentData persist_tr_owners;


/* Function Prototypes */
/* Functions connected to callbacks:
 *    arenaAction: arena creation, destruction, and conf changes
 *    flagTag: does everything necessary when a flag is claimed
 *    freqChange: when a player changes to another team
 *    shipChange: when a player changes ships (possibly team as well)
 *    killEvent: when a player makes a kill
 *    turfRewardTimer: called when a reward is due to be processed
 */
local void arenaAction(Arena *arena, int action);
local void flagTag(Arena *arena, Player *p, int fid, int oldteam, int newteam);
local void shipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void killEvent(Arena *arena, Player *killer, Player *killed,
		int bounty, int flags, int *pts, int *green);
local int turfRewardTimer(void *v);


/* Functions connected to the turf_reward interface:
 *    flagGameReset: resets all flag data for an arena
 *                   (including data in flags module)
 *    dingTimerReset: resets the timer to the specified arena
 *    doReward: forces a reward reward to happen immediately
 *              returns 1 on failure, 0 on success
 *    GetTurfData: to get a pointer to an arena's TurfArena data
 *    ReleaseTurfData: must be called after a call to GetTurfData
 */
local void flagGameReset(Arena *arena);
local void dingTimerReset(Arena *arena);
local int doReward(Arena *arena);
local TurfArena* GetTurfData(Arena *arena);
local void ReleaseTurfData(Arena *arena);


/* Helper / Utility functions:
 *    loadSettings: reads the settings for an arena from cfg
 *
 *    clearArenaData: clears out an arena's data
 *                    (not including teams, flags, or numFlags)
 *    clearTeamsData: clears and initializes the team data for a particular arena
 *    clearFlagsData: clears and initializes the flag data for a particular arena
 *    calcWeight: figures out how much a flag is worth
 *    preCalc: does a few calculations that will make writing external
 *             calculation modules a lot easier
 *    checkArenaRequirements: checks if arena passes minimum requirements
 *                            returns 0 on pass, 1 on fail
 *    awardPtsPlayer: awards points based on invdividual player scores
 *    awardPtsTeam: awards points based on team scores
 *    awardPtsPlayerTeam: awards points based on both individual scores
 *                        and team scores
 *    updateFlags: increment the numDings for all owned flags and recalculate
 *                 their weights
 *    cleanup: releases the scoring interface when arena is destroyed
 *    getTeamPtr: get a pointer to a team.  If it doesn't exist, creates the
 *                team, and returns a pointer to it.  If team exists, just
 *                returns pointer to existing one.
 *    getTurfPlayerPtr: like findTurfPlayerPtr() but if no match found, creates
 *                      the node and addes to the players linked list.  Also,
 *                      if the pid matched, but the name was different, the
 *                      node is removed and replaced with an updated one
 */
local void loadSettings(Arena *arena);
local void clearArenaData(Arena *arena);
local void clearPlayersData(Arena *arena);
local void clearTeamsData(Arena *arena);
local void clearFlagsData(Arena *arena, int init);
local int calcWeight(Arena *arena, TurfArena *ta, TurfFlag *tf, ticks_t currentTicks);
local void preCalc(Arena *arena, TurfArena *ta, ticks_t currentTicks);
local int checkArenaRequirements(Arena *arena, TurfArena *ta);
local void awardPtsPlayer(Arena *arena, TurfArena *ta);
local void awardPtsTeam(Arena *arena, TurfArena *ta);
local void awardPtsPlayerTeam(Arena *arena, TurfArena *ta);
local void updateFlags(Arena *arena, TurfArena *ta);
local void cleanup(void *v);
local TurfTeam* getTeamPtr(TurfArena *ta, int team, int createIfDNE);
local TurfPlayer* getTurfPlayerPtr(TurfArena *ta, char *name, TurfTeam *team, int createIfDNE);
local void postRewardCleanup(TurfArena *ta);


/* functions for commands */
/* standard user commands:
 *    C_turfTime: to find out how much time till next ding
 *    C_turfIndo: to get settings info on minimum requirements, etc
 */
local void C_turfTime(const char *, const char *, Player *, const Target *);
local void C_turfInfo(const char *, const char *, Player *, const Target *);

/* mod commands:
 *    C_turfResetFlags: to reset the flag data on all flags
 *    C_forceDing: to force a ding to occur, does not change the timer
 *    C_turfResetTimer: to reset the timer
 */
local void C_turfResetFlags(const char *, const char *, Player *, const Target *);
local void C_forceDing(const char *, const char *, Player *, const Target *);
local void C_turfResetTimer(const char *, const char *, Player *, const Target *);

/* help text for commands */
local helptext_t turftime_help, turfinfo_help, turfresetflags_help,
	turfresettimer_help, forceding_help;


/* connect functions to turf_reward interface */
local Iturfreward _myint =
{
	INTERFACE_HEAD_INIT(I_TURFREWARD, "turfreward-core")
	flagGameReset, dingTimerReset, doReward,
	GetTurfData, ReleaseTurfData
};

/* persistant turf flag data */
typedef struct PersistentTurfRewardData
{
	int teamFreq;
	char taggerName[24];
	int dings;
	int weight;
	int recovered;
	ticks_t tagTC;
	ticks_t lastTC;
} PersistentTurfRewardData;


/* for enum conf settings */
/* reward style settings */
DEFINE_FROM_STRING(tr_style_val, TR_STYLE_MAP)
DEFINE_TO_STRING(tr_style_str, TR_STYLE_MAP);

/* weight calculation settings */
DEFINE_FROM_STRING(tr_weight_val, TR_WEIGHT_MAP)

/* recovery system settings */
DEFINE_FROM_STRING(tr_recovery_val, TR_RECOVERY_MAP)


EXPORT const char info_turf_reward[]
	= "v0.5.6 by GiGaKiLLeR <gigamon@hotmail.com>";


/* the actual entrypoint into this module */
EXPORT int MM_turf_reward(int action, Imodman *_mm, Arena *arena)
{
	if( action == MM_LOAD ) /* when the module is to be loaded */
	{
		mm = _mm;

		/* get all of the interfaces that we are to use */
		playerdata = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		arenaman   = mm->GetInterface(I_ARENAMAN,   ALLARENAS);
		flagcore   = mm->GetInterface(I_FLAGCORE,   ALLARENAS);
		config     = mm->GetInterface(I_CONFIG,     ALLARENAS);
		stats      = mm->GetInterface(I_STATS,      ALLARENAS);
		logman     = mm->GetInterface(I_LOGMAN,     ALLARENAS);
		mainloop   = mm->GetInterface(I_MAINLOOP,   ALLARENAS);
		mapdata    = mm->GetInterface(I_MAPDATA,    ALLARENAS);
		chat       = mm->GetInterface(I_CHAT,       ALLARENAS);
		cmdman     = mm->GetInterface(I_CMDMAN,     ALLARENAS);
		net        = mm->GetInterface(I_NET,        ALLARENAS);
		persist    = mm->GetInterface(I_PERSIST,    ALLARENAS);

		/* if any of the interfaces are null then loading failed */
		if( !playerdata || !arenaman || !flagcore || !config  ||
			!stats      || !logman   || !mainloop || !mapdata ||
			!chat       || !cmdman   || !net      || !persist )
		{
			return MM_FAIL;
		}

		trkey = arenaman->AllocateArenaData(sizeof(TurfArena *));
		mtxkey = arenaman->AllocateArenaData(sizeof(pthread_mutex_t));
		if( trkey == -1 || mtxkey == -1 ) return MM_FAIL;

		/* special turf_reward commands */
		cmdman->AddCommand("turftime",       C_turfTime,       ALLARENAS, turftime_help);
		cmdman->AddCommand("turfinfo",       C_turfInfo,       ALLARENAS, turfinfo_help);
		cmdman->AddCommand("forceding",      C_forceDing,      ALLARENAS, forceding_help);
		cmdman->AddCommand("turfresetflags", C_turfResetFlags, ALLARENAS, turfresetflags_help);
		cmdman->AddCommand("turfresettimer", C_turfResetTimer, ALLARENAS, turfresettimer_help);

		/* register the interface for turf_reward */
		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if( action == MM_UNLOAD ) /* when the module is to be unloaded */
	{
		/* unregister the interface for turf_reward */
		if(mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;

		/* make sure all timers are gone */
		mainloop->CleanupTimer(turfRewardTimer, NULL, cleanup);

		/* get rid of turf_reward commands */
		cmdman->RemoveCommand("turftime",       C_turfTime,       ALLARENAS);
		cmdman->RemoveCommand("turfinfo",       C_turfInfo,       ALLARENAS);
		cmdman->RemoveCommand("forceding",      C_forceDing,      ALLARENAS);
		cmdman->RemoveCommand("turfresetflags", C_turfResetFlags, ALLARENAS);
		cmdman->RemoveCommand("turfresettimer", C_turfResetTimer, ALLARENAS);

		arenaman->FreeArenaData(trkey);
		arenaman->FreeArenaData(mtxkey);

		/* release all interfaces */
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(arenaman);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(config);
		mm->ReleaseInterface(stats);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(mainloop);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmdman);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(persist);

		return MM_OK;
	}
	else if( action == MM_ATTACH )
	{
		/* module only attached to an arena if listed in conf */
		/* create all necessary callbacks */
		mm->RegCallback(CB_ARENAACTION,    arenaAction,    arena);
		mm->RegCallback(CB_TURFTAG,        flagTag,        arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChange, arena);
		mm->RegCallback(CB_KILL,           killEvent,      arena);

		if( persist )
			persist->RegArenaPD(&persist_tr_owners);

		return MM_OK;
	}
	else if( action == MM_DETACH )
	{
		/* unregister all the callbacks when detaching arena */
		mm->UnregCallback(CB_ARENAACTION,    arenaAction,    arena);
		mm->UnregCallback(CB_TURFTAG,        flagTag,        arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChange, arena);
		mm->UnregCallback(CB_KILL,           killEvent,      arena);

		if( persist )
			persist->UnregArenaPD(&persist_tr_owners);

		return MM_OK;
	}
	return MM_FAIL;
}


local void arenaAction(Arena *arena, int action)
{
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);

	if (action == AA_PRECREATE)
	{
		pthread_mutexattr_t attr;
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey), &attr);
		pthread_mutexattr_destroy(&attr);
	}
	else if (action == AA_CREATE)
	{
		ta = amalloc(sizeof(*ta));

		*p_ta = ta;

		ta->settings.reward_style         = 0;
		ta->settings.min_players_on_team  = 0;
		ta->settings.min_players_in_arena = 0;
		ta->settings.min_teams            = 0;
		ta->settings.min_flags            = 0;
		ta->settings.min_percent_flags    = 0;
		ta->settings.min_weights          = 0;
		ta->settings.min_percent_weights  = 0;
		ta->settings.min_percent          = 0;
		ta->settings.reward_modifier      = 0;
		ta->settings.recover_dings        = 0;
		ta->settings.set_weights          = 0;
		ta->settings.weights              = NULL;
		ta->settings.timer_initial        = 0;
		ta->settings.timer_interval       = 0;

		ta->trp = NULL;

		ta->numFlags = 0;
		clearArenaData(arena);
		ta->flags = NULL;
	}

	ta = *p_ta;

	LOCK_STATUS(arena);

	if( action == AA_CREATE )
	{
		loadSettings(arena);

		/* create and initialize all the flags */
		ta->numFlags = mapdata->GetFlagCount(arena);
		ta->flags = amalloc(ta->numFlags * sizeof(TurfFlag));
		clearFlagsData(arena, 1);

		/* create and intialize the data on teams and players */
		LLInit(&ta->teams);
		LLInit(&ta->validTeams);
		LLInit(&ta->invalidTeams);
		LLInit(&ta->players);
		LLInit(&ta->playersPts);
		LLInit(&ta->playersNoPts);

		ta->trp = mm->GetInterface(I_TURFREWARD_POINTS, arena);

		/* set up the timer for arena */
		ta->dingTime = current_ticks();
		mainloop->SetTimer(turfRewardTimer, ta->settings.timer_initial,
			ta->settings.timer_interval, arena, arena);
	}
	else if( action == AA_DESTROY )
	{
		/* clear old timer and cleanup the I_TURFREWARD_POINTS interface */
		mainloop->CleanupTimer(turfRewardTimer, arena, cleanup);

		/* clean up any old arena data */
		clearArenaData(arena);

		/* if there is existing weights data, discard */
		if( ta->settings.weights )
		{
			afree(ta->settings.weights);
			ta->settings.weights = NULL;
		}

		/* if there is existing flags data, discard */
		if( ta->flags )
		{
			clearFlagsData(arena, 0);
			afree(ta->flags);
			ta->flags = NULL;
		}

		/* if there is existing teams data or players data, discard */
		clearTeamsData(arena);
		clearPlayersData(arena);
	}
	else if( action == AA_CONFCHANGED )
	{
		int initial  = ta->settings.timer_initial;
		int interval = ta->settings.timer_interval;

		loadSettings(arena);
		if( (initial!=ta->settings.timer_initial) || 
			(interval!=ta->settings.timer_interval) )
		{
			chat->SendArenaMessage(arena,
				"Ding timer settings in config was changed. Updating...");
			dingTimerReset(arena);
		}
	}

	UNLOCK_STATUS(arena);

	if (action == AA_DESTROY)
	{
		afree(ta);
		*p_ta = NULL;
	}
	else if (action == AA_POSTDESTROY)
	{
		pthread_mutex_destroy((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey));
	}
}


local void loadSettings(Arena *arena)
{
	ConfigHandle c = arena->cfg;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	/* cfghelp: TurfReward:RewardStyle, arena, enum, def: TR_STYLE_DISABLED
	 * The reward algorithm to be used.  Built in algorithms include:
	 * TR_STYLE_DISABLED: disable scoring, 
	 * TR_STYLE_PERIODIC: normal periodic scoring but with the all the extra stats, 
	 * TR_STYLE_STANDARD: see souce code documenation (complex formula) + jackpot based on # players
	 * TR_STYLE_STD_BTY: standard + jackpot based on bounty exchanged
	 * TR_STYLE_FIXED_PTS: each team gets a fixed # of points based on 1st, 2nd, 3rd,... place
	 * TR_STYLE_WEIGHTS: number of points to award equals number of weights owned */
	ta->settings.reward_style 
		= tr_style_val(config->GetStr(c, "TurfReward", "RewardStyle"), TR_STYLE_DISABLED);

	/* cfghelp: TurfReward:MinPlayersTeam, arena, int, def: 3
	 * The minimum number of players needed on a team for players on that
	 * team to be eligable to recieve points. */
	ta->settings.min_players_on_team 
		= config->GetInt(c, "TurfReward", "MinPlayersTeam", 3);

	/* cfghelp: TurfReward:MinPlayersArena, arena, int, def: 6
	 * The minimum number of players needed in the arena for anyone
	 * to be eligable to recieve points. */
	ta->settings.min_players_in_arena 
		= config->GetInt(c, "TurfReward", "MinPlayersArena", 6);

	/* cfghelp: TurfReward:MinTeams, arena, int, def: 2
	 * The minimum number of teams needed in the arena for anyone
	 * to be eligable to recieve points. */
	ta->settings.min_teams 
		= config->GetInt(c, "TurfReward", "MinTeams", 2);

	/* cfghelp: TurfReward:MinFlags, arena, int, def: 1
	 * The minimum number of flags needed to be owned by a freq for
	 * that team to be eligable to recieve points. */
	ta->settings.min_flags 
		= config->GetInt(c, "TurfReward", "MinFlags", 1);

	/* cfghelp: TurfReward:MinFlagsPercent, arena, int, def: 0
	 * The minimum percent of flags needed to be owned by a freq for
	 * that team to be eligable to recieve points.
	 * (ex. 18532 means 18.532%) */
	ta->settings.min_percent_flags
		= (double)config->GetInt(c, "TurfReward", "MinFlagsPercent", 0) / 1000.0;

	/* cfghelp: TurfReward:MinWeights, arena, int, def: 1
	 * The minimum number of weights needed to be owned by a freq for
	 * that team to be eligable to recieve points. */
	ta->settings.min_weights 
		= config->GetInt(c, "TurfReward", "MinWeights", 1);

	/* cfghelp: TurfReward:MinWeightsPercent, arena, int, def: 0
	 * The minimum percent of weights needed to be owned by a freq for
	 * that team to be eligable to recieve points.
	 * (ex. 18532 means 18.532%) */
	ta->settings.min_percent_weights
		= (double)config->GetInt(c, "TurfReward", "MinWeightsPercent", 0) / 1000.0;

	/* cfghelp: TurfReward:MinPercent, arena, int, def: 0
	 * The minimum percent of points needed to be owned by a freq for
	 * that team to be eligable to recieve points.
	 * (ex. 18532 means 18.532%) */
	ta->settings.min_percent 
		= (double)config->GetInt(c, "TurfReward", "MinPercent", 0) / 1000.0;

	/* cfghelp: TurfReward:RewardModifier, arena, int, def: 200
	 * Modifies the number of points to award.  Meaning varies based on reward
	 * algorithm being used.
	 * For $REWARD_STD: jackpot = # players * RewardModifer */
	ta->settings.reward_modifier 
		= config->GetInt(c, "TurfReward", "RewardModifier", 200);

	/* cfghelf: TurfReward:MaxPoints, arena, int, def: 10000
	 * The maximum number of points allowed to be award to a single player per
	 * ding. If a player's points is calculated to be ablove the max, only this
	 * amount will be awarded. */
	ta->settings.max_points 
		= config->GetInt(c, "TurfReward", "MaxPoints", 10000);

	/* cfghelp: TurfReward:TimerInitial, arena, int, def: 6000
	 * Inital turf_reward ding timer period. */
	ta->settings.timer_initial 
		= config->GetInt(c, "TurfReward", "TimerInitial", 6000);

	/* cfghelp: TurfReward:TimerInterval, arena, int, def: 6000
	 * Subsequent turf_reward ding timer period. */
	ta->settings.timer_interval 
		= config->GetInt(c, "TurfReward", "TimerInterval", 6000);

	/* cfghelp: TurfReward:SpecRecievePoints, arena, bool, def: 0
	 * Whether players in spectator mode recieve reward points. */
	ta->settings.spec_recieve_points 
		= config->GetInt(c, "TurfReward", "SpecRecievePoints", 0);
	 
	/* cfghelp: TurfReward:SafeRecievePoints, arena, bool, def: 0
	 * Whether players in safe zones recieve reward points. */
	ta->settings.safe_recieve_points 
		= config->GetInt(c, "TurfReward", "SafeRecievePoints", 0);

	/* cfghelp: TurfReward:RecoveryCutoff, arena, enum, def: TR_RECOVERY_DINGS
	 * Style of recovery cutoff to be used.
	 * TR_RECOVERY_DINGS - recovery cutoff based on RecoverDings.
	 * TR_RECOVERY_TIME - recovery cutoff based on RecoverTime.
	 * TR_RECOVERY_DINGS_AND_TIME - recovery cutoff based on both RecoverDings
	 * and RecoverTime. */
	ta->settings.recovery_cutoff 
		= tr_recovery_val(config->GetStr(c, "TurfReward", "RecoveryCutoff"), TR_RECOVERY_DINGS);

	/* cfghelp: TurfReward:RecoverDings, arena, int, def: 1
	 * After losing a flag, the number of dings allowed to pass before a freq
	 * loses the chance to recover.  0 means you have no chance of recovery
	 * after it dings (to recover, you must recover before any ding occurs),  1
	 * means it is allowed to ding once and you still have a chance to recover
	 * (any ding after that you lost chance of full recovery), ... */
	ta->settings.recover_dings 
		= config->GetInt(c, "TurfReward", "RecoverDings", 1);

	/* cfghelp: TurfReward:RecoverTime, arena, int, def: 300
	 * After losing a flag, the time (seconds) allowed to pass before a freq
	 * loses the chance to recover. */
	ta->settings.recover_time 
		= config->GetInt(c, "TurfReward", "RecoverTime", 300);

	/* cfghelp: TurfReward:RecoverMax, arena, int, def: -1
	 * Maximum number of times a flag may be recovered. (-1 means no max) */
	ta->settings.recover_max 
		= config->GetInt(c, "TurfReward", "RecoverMax", -1);

	/* cfghelp: TurfReward:WeightCalc, arena, enum, def: TR_WEIGHT_DINGS
	 * The method weights are calculated:
	 * TR_WEIGHT_TIME means each weight stands for one minute 
	 * (ex: Weight004 is the weight for a flag owned for 4 minutes).
	 * TR_WEIGHT_DINGS means each weight stands for one ding of ownership 
	 * (ex: Weight004 is the weight for a flag that was owned during 4 dings). */
	ta->settings.weight_calc
		= tr_weight_val(config->GetStr(c, "TurfReward", "WeightCalc"), TR_WEIGHT_DINGS);

	/* cfghelp: TurfReward:SetWeights, arena, int, def: 0
	 * How many weights to set from cfg (16 means you want to specify Weight0 to
	 * Weight15). If set to 0, then by default one weight is set with a value
	 * of 1. */
	if (ta->settings.weights)
	{
		/* there is existing weights data, discard */
		afree(ta->settings.weights);
		ta->settings.weights = NULL;
	}
	ta->settings.set_weights = config->GetInt(c, "TurfReward", "SetWeights", 0);

	if(ta->settings.set_weights < 1)
	{
		/* user didn't set the weights, just set a weight with 1 WU then */
		ta->settings.set_weights = 1;
		ta->settings.weights = amalloc(sizeof(int));
		ta->settings.weights[0]=1;
	}
	else
	{
		int x;
		char wStr[] = "Weight####";
		int defaultVal = 1;  /* to keep it non-decreasing and non-increasing */
		                     /* if conf didn't specify a weight */

		if (ta->settings.set_weights>100)
			ta->settings.set_weights = 100;

		ta->settings.weights = amalloc(ta->settings.set_weights * sizeof(int));

		for(x=0 ; x<ta->settings.set_weights ; x++)
		{
			sprintf(wStr, "Weight%d", x);
			ta->settings.weights[x] = config->GetInt(c, "TurfReward", wStr, defaultVal);
			defaultVal = ta->settings.weights[x];
		}
	}

	/* now that settings are read in
	 * check for possible problems, adjust if necessary */
	if(ta->settings.min_players_on_team < 1)
		ta->settings.min_players_on_team = 1;
	if(ta->settings.min_players_in_arena < 1)
		ta->settings.min_players_in_arena = 1;
	if(ta->settings.min_teams < 1)
		ta->settings.min_teams = 1;
	if(ta->settings.min_flags < 1)
		ta->settings.min_flags = 1;
	if(ta->settings.min_weights < 1)
		ta->settings.min_weights =1;
	if(ta->settings.timer_initial < 1500)
		ta->settings.timer_initial = 1500;
	if(ta->settings.timer_interval < 1500)  /* 15 second safety so that server cannot */
		ta->settings.timer_interval = 1500; /* be overloaded by dings */
}


local void clearArenaData(Arena *arena)
{
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	ta->dingTime            = current_ticks();
	ta->numPlayers          = 0;
	ta->numTeams            = 0;
	ta->numValidTeams       = 0;
	ta->numInvalidTeams     = 0;
	ta->sumPerCapitaFlags   = 0.0;
	ta->numWeights          = 0;
	ta->sumPerCapitaWeights = 0.0;
	ta->numPoints           = 0;
	ta->tags                = 0;
	ta->steals              = 0;
	ta->recoveries          = 0;
	ta->lost                = 0;
	ta->kills               = 0;
    ta->bountyExchanged     = 0;
}


local void clearPlayersData(Arena *arena)
{
	Link *l;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;
	
	LLEmpty(&ta->playersPts);
	LLEmpty(&ta->playersNoPts);

	for(l=LLGetHead(&ta->players) ; l ; l=l->next)
	{
		TurfPlayer *pPlayer = l->data;

		/* inform points module of removal if extra was used */
		if(pPlayer->extra != NULL)
			ta->trp->RemoveTurfPlayer(pPlayer);

		/* empty, do not free, flags list */
		LLEmpty(&pPlayer->flags);
	}
	
	LLEnum(&ta->players, afree);
	LLEmpty(&ta->players);
}


local void clearTeamsData(Arena *arena)
{
	Link *l;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	for(l=LLGetHead(&ta->teams) ; l ; l=l->next)
	{
		TurfTeam *pTeam = l->data;

		/* inform points module of removal if extra was used */
		if( pTeam->extra != NULL )
			ta->trp->RemoveTurfTeam(pTeam);

		/* empty, do not free, flags list */
		LLEmpty(&pTeam->flags);

		/* empty players linked list, do not free, they are
		 * still being pointed to by ta->players
		 * In other words, clearPlayersData() will do the freeing */
		LLEmpty(&pTeam->players);
		LLEmpty(&pTeam->playersPts);
		LLEmpty(&pTeam->playersNoPts);
	}

	LLEnum(&ta->teams, afree);
	LLEmpty(&ta->teams);
	LLEmpty(&ta->validTeams);
	LLEmpty(&ta->invalidTeams);
}


local void clearFlagsData(Arena *arena, int init)
{
	int x;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	for(x=0 ; x<ta->numFlags ; x++)
	{
		TurfFlag *pFlag = &ta->flags[x];

		pFlag->team      = NULL;
		pFlag->tagger    = NULL;

		pFlag->dings     = -1;
		pFlag->weight    =  0;
		pFlag->recovered =  0;

		pFlag->tagTC     =  0;
		pFlag->lastTC    =  0;

		/* now clear out the linked list 'old' */
		if (init)
			LLInit(&pFlag->old);

		LLEnum(&pFlag->old, afree);
		LLEmpty(&pFlag->old);
	}
}


local void flagTag(Arena *arena, Player *p, int fid, int oldfreq, int freq)
{
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	int r_freq=-1, r_dings, r_weight, r_pid, r_rec, r_tc, /* flag recover data */
	    l_freq=-1, l_dings, l_weight, l_rec, l_tc; /* flag lost data */
	TurfFlag *pTF   = NULL;  /* pointer to turf flag that was tagged */
	TurfFlagPrevious *oPtr  = NULL; /* pointer to node of linked list holding */
	                                /* previous owners */
	TurfTeam *pTeam = NULL;  /* pointer to team that tagged flag */
	TurfPlayer *pTP = NULL;  /* pointer to player that tagged flag */
	Link *l = NULL;  /* pointer to a node in a linked list */
	ticks_t currentTicks = current_ticks();

	if (!arena || !*p_ta) return; else ta = *p_ta;

	if(fid < 0 || fid >= ta->numFlags)
	{
		logman->LogP(L_MALICIOUS, "turf_reward", p,
			"nonexistent flag tagged: %d not in 0..%d",
			fid, ta->numFlags-1);
		return;
	}

	LOCK_STATUS(arena);

	pTF = &ta->flags[fid];

	if( (pTF->team) && (pTF->team->freq==freq) )
	{
		/* flag was already owned by that team */
		UNLOCK_STATUS(arena);
		logman->LogP(L_WARN, "turf_reward", p,
			"state sync problem: flag tagged was already owned by player's team.");
		return;
	}

	/* get pointer to team that tagged (if DNE, create) */
	pTeam = getTeamPtr(ta, freq, 1);

	/* get a pointer to player for associated team (if DNE, create) */
	pTP = getTurfPlayerPtr(ta, p->name, pTeam, 1);

	if( (pTF->team) && (pTF->team->freq>=0) )
	{
		/* flag that was tagged was owned by another team */
		TurfTeam *pTeamOld = pTF->team;
		TurfPlayer *pPlayerOld = pTF->tagger;

		if(ta->settings.recover_max == -1 || pTF->recovered < ta->settings.recover_max)
		{
			/* team that lost flag gets a chance to recover */
			oPtr = amalloc(sizeof(TurfFlagPrevious));

			oPtr->lastOwned = 0;
			strncpy(oPtr->taggerName, pTF->tagger->name, 24);
			oPtr->freq      = l_freq   = pTF->team->freq;
			oPtr->dings     = l_dings  = pTF->dings;
			oPtr->weight    = l_weight = pTF->weight;
			oPtr->recovered = l_rec    = pTF->recovered;
			oPtr->tagTC     = l_tc     = pTF->tagTC;
			oPtr->lostTC    = currentTicks;  /* time flag was lost */

			/* add node to linked list of previous flag owners */
			LLAddFirst(&pTF->old, oPtr);
		}

		/* remove this flag from the linked list of the team that lost it */
		LLRemove(&pTeamOld->flags, pTF);
		pTeamOld->numFlags--;
		pTeamOld->percentFlags 
			= ((double)pTeamOld->numFlags) / ((double)ta->numFlags) * 100.0;

		/* remove this flag from the linked list of the player that tagged it */
		LLRemove(&pPlayerOld->flags, pTF);

		/* increment number of flag losses */
		pPlayerOld->lost++; /* previous tagger           */
		pTeamOld->lost++;   /* previous team that owned  */
		ta->lost++;         /* total flag losses counter */

		/* increment steals */
		pTP->steals++;   /* player that tagged  */
		pTeam->steals++; /* team that tagged    */
		ta->steals++;    /* total steal counter */
	}

	/* update flag's owner */
	pTF->team = pTeam;
	pTF->tagger = pTP;

	/* add this flag to the linked list of the team that tagged it */
	LLAddFirst(&pTeam->flags, pTF);
	pTeam->numFlags++;
	pTeam->percentFlags = ((double)pTeam->numFlags) / ((double)ta->numFlags) * 100.0;

	/* add this flag to the linked list of the player that tagged it */
	LLAddFirst(&pTP->flags, pTF);

	/* increment tags */
	pTP->tags++;   /* player that tagged */
	pTeam->tags++; /* team that tagged   */
	ta->tags++;    /* total tag counter  */

	/* search for matching freq in list of teams that have chance to recover */
	for(l=LLGetHead(&pTF->old) ; l ; l=l->next)
	{
		oPtr = l->data;

		if(oPtr->freq == freq)
		{
			/* lastOwned for each entry is incremented and checked during each
			 * ding, meaning all nodes are already assured to be valid for
			 * recovery based on dings.  However, we must still check if an
			 * entry is still valid for recovery based on time past since lost */
			if(ta->settings.recovery_cutoff==TR_RECOVERY_TIME
				|| ta->settings.recovery_cutoff==TR_RECOVERY_DINGS_AND_TIME)
			{
				if ( (TICK_DIFF(currentTicks, oPtr->lostTC) / 100)
					> ta->settings.recover_time )
				{
					LLRemove(&pTF->old, oPtr);
					afree(oPtr);
					break; /* break out of the for loop */
				}
			}

			/* found entry that matches freq, meaning flag was recovered
			 * restore flag's previous data */
			r_freq   = freq;
			r_pid    = p->pid;
			r_dings  = pTF->dings  = oPtr->dings;
			r_weight = pTF->weight = oPtr->weight;
			r_rec    = pTF->recovered = oPtr->recovered + 1;
			r_tc     = pTF->tagTC     = oPtr->tagTC;

			/* remove node from linked list */
			LLRemove(&pTF->old, oPtr);
			afree(oPtr);

			/* increment number of flag recoveries */
			pTP->recoveries++;   /* player that recovered  */
			pTeam->recoveries++; /* team that recovered    */
			ta->recoveries++;    /* total recovery counter */

			break;  /* break out of the loop */
		}
	}

	if(r_freq==-1)
	{
		/* flag wasn't recovered, fill in data for newly tagged flag */
		pTF->dings     = 0;
		pTF->tagTC     = currentTicks;
		pTF->weight    = calcWeight(arena, ta, pTF, currentTicks);
		pTF->recovered = 0;
	}
	pTF->lastTC = currentTicks;

	UNLOCK_STATUS(arena);

	logman->LogP(L_DRIVEL, "turf_reward", p,
		"tagged flag:[%d] dings:[%d] weight:[%d] freq:[%d]", 
		fid, pTF->dings, pTF->weight, pTeam->freq);

	/* finally do whatever callbacks are necessary */
	if(r_freq!=-1)
		DO_CBS(CB_TURFRECOVER, arena, TurfRecoverFunc,
			(arena, fid, r_pid, r_freq, r_dings, r_weight, r_rec));
	if(l_freq!=-1)
	{
		/* this flag was lost, meaning it was also stolen */
		DO_CBS(CB_TURFSTEAL, arena, TurfStealFunc, (arena, p, fid));
		DO_CBS(CB_TURFLOST, arena, TurfLostFunc,
			(arena, fid, 0, l_freq, l_dings, l_weight, l_rec));
	}
}


local int calcWeight(Arena *arena, TurfArena *ta, TurfFlag *tf, ticks_t currentTicks)
{
	int weightNum = 0;

	switch(ta->settings.weight_calc)
	{
	case TR_WEIGHT_TIME:
		/* calculate by time owned (minutes) */
		weightNum = (TICK_DIFF(currentTicks, tf->tagTC) / 100) / 60;
		break;
	case TR_WEIGHT_DINGS:
		/* calculate by # of dings */
		weightNum = tf->dings;
		break;
	default:
		/* setting not understood */
		logman->LogA(L_DRIVEL, "turf_reward", arena,
			"bad setting for WeightCalc:%d", ta->settings.weight_calc);
		return 0;
	}

	if (weightNum < 0)
		return 0;
	if (weightNum > ta->settings.set_weights-1)
		return ta->settings.weights[ta->settings.set_weights-1];
	return ta->settings.weights[weightNum];
}


local int turfRewardTimer(void *v)
{
	Arena *arena = v;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return FALSE; else ta = *p_ta;

	if( !arena ) return FALSE;

	logman->LogA(L_DRIVEL, "turf_reward", arena, "timer ding");

	/* we have a go for starting the reward sequence */
	if( doReward(arena) == 0 )
	{
		ta->dingTime = current_ticks();
		return TRUE;  /* yes, we want timer called again */
	}

	logman->LogA(L_DRIVEL, "turf_reward", arena, "timer stopped");
	return FALSE; /* no, timer stops here */
}


local int doReward(Arena *arena)
{
	ticks_t currentTicks = current_ticks();
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return 1; else ta = *p_ta;

	LOCK_STATUS(arena);

	if( !ta || !ta->flags )
	{
		UNLOCK_STATUS(arena);
		return 1;
	}

	/* do pre-calculations to prepare ta data for points module */
	preCalc(arena, ta, currentTicks);

	if( !checkArenaRequirements(arena, ta) )
	{
		/* arena requirements were met */
		/* calculate the points to award using interface for score calculations */
		if( ta->trp )
			ta->trp->CalcReward(arena,ta);
		else
		{
			UNLOCK_STATUS(arena);
			logman->LogA(L_DRIVEL, "turf_reward", arena,
				"problem calling CalcReward interface (NULL)");
			return 1;
		}

		/* check status returned by call to CalcReward */
		if( ta->calcState.status==TR_CALC_FAIL )
		{
			UNLOCK_STATUS(arena);
			logman->LogA(L_DRIVEL, "turf_reward", arena,
				"CalcReward failed, halting any further rewards");
			return 1;
		}

		/* check if we are to award points */
		switch( ta->calcState.award )
		{
		case TR_AWARD_PLAYER:
			awardPtsPlayer(arena, ta);
			stats->SendUpdates(NULL);
			break;
		case TR_AWARD_TEAM:
			awardPtsTeam(arena, ta);
			stats->SendUpdates(NULL);
			break;
		case TR_AWARD_BOTH:
			awardPtsPlayerTeam(arena, ta);
			stats->SendUpdates(NULL);
			break;
		case TR_AWARD_NONE: default:
			break;
		}

		/* do the callback for post-reward (WHILE LOCK IS IN PLACE)
		 * Note: this might become a problem if functions registered
		 * with the callback require a large amount of processing time
		 * */
		DO_CBS(CB_TURFPOSTREWARD, arena, TurfPostRewardFunc, (arena,
					ta));

		/* check if we are to update flags */
		if( ta->calcState.update )
			updateFlags(arena, ta);
	}

	/* initialize necessary data for next round */
	postRewardCleanup(ta);

	UNLOCK_STATUS(arena);
	return 0;
}


/* call with TR lock on */
local void preCalc(Arena *arena, TurfArena *ta, ticks_t currentTicks)
{
	Player *pdPtr;
	Link *l, *link, *next;
	int x;

	/* make sure arena data is clear */
	ta->numPlayers          = 0;
	ta->numValidPlayers     = 0;
	ta->numPoints           = 0;
	ta->numTeams            = 0;
	ta->numValidTeams       = 0;
	ta->numInvalidTeams     = 0;
	ta->sumPerCapitaFlags   = 0.0;
	ta->numWeights          = 0;
	ta->sumPerCapitaWeights = 0.0;

	/* make sure each team's data is clear */
	for(l=LLGetHead(&ta->teams) ; l ; l=l->next)
	{
		TurfTeam *pTeam = l->data;

		pTeam->numPlayers       = 0;
		pTeam->perCapitaFlags   = 0.0;
		pTeam->numWeights       = 0;
		pTeam->percentWeights   = 0.0;
		pTeam->perCapitaWeights = 0.0;
		pTeam->percent          = 0.0;
		pTeam->numPoints        = 0;
	}

	/* go through all flags and if owned, update numWeights for the owning team */
	for(x=0 ; x<ta->numFlags ; x++)
	{
		TurfFlag *pFlag = &ta->flags[x];

		if(pFlag->team)
		{
			/* flag is owned */
			pFlag->weight = calcWeight(arena, ta, pFlag, currentTicks);
			pFlag->team->numWeights += pFlag->weight;
			ta->numWeights += pFlag->weight;
		}
	}

	/* figure out percent weights for each team */
	for(l=LLGetHead(&ta->teams) ; l ; l=l->next)
	{
		TurfTeam *pTeam = l->data;
		pTeam->percentWeights = (ta->numWeights==0) 
			? 0.0 : ((double)pTeam->numWeights) / (double)ta->numWeights * 100.0;
	}

	/* Go through all players in arena creating nodes if DNE already */
	playerdata->Lock();
	FOR_EACH_PLAYER(pdPtr)
	{
		TurfPlayer *pPlayer = NULL;
		TurfTeam   *pTeam   = NULL;

		if (pdPtr->arena != arena)
			continue; /* goto next player, this one's not in the arena */

		pTeam   = getTeamPtr(ta, pdPtr->p_freq, 1);
		pPlayer = getTurfPlayerPtr(ta, pdPtr->name, pTeam, 1);

		pPlayer->isActive = 1;
		pPlayer->pid      = pdPtr->pid;

		ta->numPlayers++;
		if( (pdPtr->status!=S_PLAYING) ||
			(ta->settings.spec_recieve_points==0 && pdPtr->p_ship==SHIP_SPEC) ||
			(ta->settings.safe_recieve_points==0 && (pdPtr->position.status & STATUS_SAFEZONE)) )
		{
			/* pdPtr is pointing to a non-player */
			LLAddFirst(&pTeam->playersNoPts, pPlayer);
			LLAddFirst(&ta->playersNoPts, pPlayer);
		}
		else
		{
			/* pdPtr is pointing to a player */
			LLAddFirst(&pTeam->playersPts, pPlayer);
			LLAddFirst(&ta->playersPts, pPlayer);
			pTeam->numPlayers++;
			ta->numValidPlayers++;
		}
	}
	playerdata->Unlock();

	/* remove teams that don't have players and don't have flags */
	/* count the number of valid teams at the same time */
	for(l=LLGetHead(&ta->teams) ; l ; l=next)
	{
		TurfTeam *pTeam = l->data;
		next = l->next;

		if( pTeam->numPlayers==0 && LLIsEmpty(&pTeam->players) &&
			pTeam->numFlags==0   && LLIsEmpty(&pTeam->flags) )
		{
			/* team is not linked to any flags or players, remove */
			LLRemove(&ta->teams, pTeam);
	
			/* inform points module of removal if extra was used */
			if( pTeam->extra != NULL )
				ta->trp->RemoveTurfTeam(pTeam);

			/* empty, do not free, flags list */
			LLEmpty(&pTeam->flags);

			/* empty players linked list, do not free, they are
			 * still being pointed to by ta->players
			 * In other words, clearPlayersData() will do the freeing */
			LLEmpty(&pTeam->players);
			LLEmpty(&pTeam->playersPts);
			LLEmpty(&pTeam->playersNoPts);

			afree(pTeam);
			continue;
		}

		ta->numTeams++;

		if( pTeam->numPlayers     >= ta->settings.min_players_on_team &&
			pTeam->numFlags       >= ta->settings.min_flags &&
			pTeam->percentFlags   >= ta->settings.min_percent_flags &&
			pTeam->numWeights     >= ta->settings.min_weights &&
			pTeam->percentWeights >= ta->settings.min_percent_weights)
		{
			/* team passsed minimum requirements */
			ta->numValidTeams++;
			LLAdd(&ta->validTeams, pTeam);
			if( pTeam->numPlayers != 0 )
			{
				pTeam->perCapitaFlags   = pTeam->numFlags   / pTeam->numPlayers;
				pTeam->perCapitaWeights = pTeam->numWeights / pTeam->numPlayers;

				ta->sumPerCapitaFlags   += pTeam->perCapitaFlags;
				ta->sumPerCapitaWeights += pTeam->perCapitaWeights;
			}
		}
		else
		{
			/* team did not pass minimum requirements */
			ta->numInvalidTeams++;
			LLAdd(&ta->invalidTeams, pTeam);
		}
	}

	/* points module should override these */
	ta->calcState.status = TR_CALC_FAIL;
	ta->calcState.award  = TR_AWARD_NONE;
	ta->calcState.update = 0;

	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numPlayers:[%d]", ta->numPlayers);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numValidPlayers:[%d]", ta->numValidPlayers);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numTeams:[%d]", ta->numTeams);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numValidTeams:[%d]", ta->numValidTeams);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numInvalidTeams:[%d]", ta->numInvalidTeams);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numFlags:[%d]", ta->numFlags);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->sumPerCapitaFlags:[%f]", ta->sumPerCapitaFlags);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numWeights:[%ld]", ta->numWeights);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->sumPerCapitaWeights:[%f]", ta->sumPerCapitaWeights);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->numPoints:[%ld]", ta->numPoints);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->tags:[%d]", ta->tags);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->steals:[%d]", ta->steals);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->recoveries:[%d]", ta->recoveries);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->lost:[%d]", ta->lost);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->kills:[%d]", ta->kills);
	logman->LogA(L_DRIVEL, "turf_reward", arena, "ta->bountyExchanged:[%d]", ta->bountyExchanged);
}


local int checkArenaRequirements(Arena *arena, TurfArena *ta)
{
	if( ta->numPlayers < ta->settings.min_players_in_arena)
	{
		chat->SendArenaSoundMessage(arena, SOUND_DING,
			"Reward: 0 (arena minimum requirements not met)");
		logman->LogA(L_DRIVEL, "turf_reward", arena, 
			"min arena requirements not met - # players have:[%d] need:[%d]", 
			ta->numPlayers, ta->settings.min_players_in_arena);
		return 1;
	}

	if( ta->numValidTeams < ta->settings.min_teams )
	{
		chat->SendArenaSoundMessage(arena, SOUND_DING,
			"Reward: 0 (arena minimum requirements not met)");
		logman->LogA(L_DRIVEL, "turf_reward", arena, 
			"min arena requirements not met - # teams have:[%d] need:[%d]",
			ta->numValidTeams, ta->settings.min_teams);
		return 1;
	}

	return 0;
}


local void awardPtsPlayer(Arena *arena, TurfArena *ta)
{
	Link *lt;

	if( !ta->trp )
	{
		logman->LogA(L_DRIVEL, "turf_reward", arena,
			"problem calling RewardMessage interface (NULL)");
		return;
	}

	playerdata->Lock();
	/* players on teams that passed minimum requirements */
	for(lt=LLGetHead(&ta->validTeams) ; lt ; lt=lt->next)
	{
		TurfTeam *pTeam = lt->data;
		Link *lp;

		/* players to get pts */
		for(lp=LLGetHead(&pTeam->playersPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);
			int points = pPlayer->points;

			/* make sure we got a match */
			if( !player || (strncmp(pPlayer->name, player->name, 24)!= 0) )
				continue;

			if( points < 0 )
				points = 0;

			if( points > ta->settings.max_points)
				points = ta->settings.max_points;

			if( points == ta->settings.max_points )
			{
				/* call interface to send the player a customized message */
				ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_PLAYER_MAX, points);
			}
			else
			{
				/* call interface to send the player a customized message */
				ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_PLAYER, points);
			}

			if( points > 0 )
			{
				/* award player */
				stats->IncrementStat(player, STAT_FLAG_POINTS, points);
			}
		}

		/* players not getting pts */
		for(lp=LLGetHead(&pTeam->playersNoPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_NON_PLAYER, 0);
		}
	}

	/* players on teams that didn't pass minimum requirements */
	for(lt=LLGetHead(&ta->invalidTeams) ; lt ; lt=lt->next)
	{
		TurfTeam *pTeam = lt->data;
		Link *lp;

		for(lp=LLGetHead(&pTeam->playersPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_INVALID_TEAM, 0);
		}

		for(lp=LLGetHead(&pTeam->playersNoPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_INVALID_TEAM, 0);
		}
	}
	playerdata->Unlock();
}


local void awardPtsTeam(Arena *arena, TurfArena *ta)
{
	Link *lt;

	if( !ta->trp )
	{
		logman->LogA(L_DRIVEL, "turf_reward", arena,
			"problem calling RewardMessage interface (NULL)");
		return;
	}

	playerdata->Lock();
	/* players on teams that passed minimum requirements */
	for(lt=LLGetHead(&ta->validTeams) ; lt ; lt=lt->next)
	{
		TurfTeam *pTeam = lt->data;
		Link *lp;

		/* players to get pts */
		for(lp=LLGetHead(&pTeam->playersPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);
			int points = pPlayer->team->numPoints;

			/* make sure we got a match */
			if( !player || (strncmp(pPlayer->name, player->name, 24)!= 0) )
				continue;

			if( points < 0 )
				points = 0;

			if( points > ta->settings.max_points)
				points = ta->settings.max_points;

			if( points == ta->settings.max_points )
			{
				/* call interface to send the player a customized message */
				ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_PLAYER_MAX, points);
			}
			else
			{
				/* call interface to send the player a customized message */
				ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_PLAYER, points);
			}

			if( points > 0 )
			{
				/* award player */
				stats->IncrementStat(player, STAT_FLAG_POINTS, points);
			}
		}

		/* players not getting pts */
		for(lp=LLGetHead(&pTeam->playersNoPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_NON_PLAYER, 0);
		}
	}

	/* players on teams that didn't pass minimum requirements */
	for(lt=LLGetHead(&ta->invalidTeams) ; lt ; lt=lt->next)
	{
		TurfTeam *pTeam = lt->data;
		Link *lp;

		for(lp=LLGetHead(&pTeam->playersPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_INVALID_TEAM, 0);
		}

		for(lp=LLGetHead(&pTeam->playersNoPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_INVALID_TEAM, 0);
		}
	}
	playerdata->Unlock();
}


local void awardPtsPlayerTeam(Arena *arena, TurfArena *ta)
{
	Link *lt;

	if( !ta->trp )
	{
		logman->LogA(L_DRIVEL, "turf_reward", arena,
			"problem calling RewardMessage interface (NULL)");
		return;
	}

	playerdata->Lock();
	/* players on teams that passed minimum requirements */
	for(lt=LLGetHead(&ta->validTeams) ; lt ; lt=lt->next)
	{
		TurfTeam *pTeam = lt->data;
		Link *lp;

		/* players to get pts */
		for(lp=LLGetHead(&pTeam->playersPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);
			int points = pPlayer->team->numPoints + pPlayer->points;

			/* make sure we got a match */
			if( !player || (strncmp(pPlayer->name, player->name, 24)!= 0) )
				continue;

			if( points < 0 )
				points = 0;

			if( points > ta->settings.max_points)
				points = ta->settings.max_points;

			if( points == ta->settings.max_points )
			{
				/* call interface to send the player a customized message */
				ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_PLAYER_MAX, points);
			}
			else
			{
				/* call interface to send the player a customized message */
				ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_PLAYER, points);
			}

			if( points > 0 )
			{
				/* award player */
				stats->IncrementStat(player, STAT_FLAG_POINTS, points);
			}
		}

		/* players not getting pts */
		for(lp=LLGetHead(&pTeam->playersNoPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_NON_PLAYER, 0);
		}
	}

	/* players on teams that didn't pass minimum requirements */
	for(lt=LLGetHead(&ta->invalidTeams) ; lt ; lt=lt->next)
	{
		TurfTeam *pTeam = lt->data;
		Link *lp;

		for(lp=LLGetHead(&pTeam->playersPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_INVALID_TEAM, 0);
		}

		for(lp=LLGetHead(&pTeam->playersNoPts) ; lp ; lp=lp->next)
		{
			TurfPlayer *pPlayer = lp->data;
			Player *player = playerdata->PidToPlayer(pPlayer->pid);

			/* call interface to send the player a customized message */
			ta->trp->RewardMessage(player, pPlayer, ta, TR_RM_INVALID_TEAM, 0);
		}
	}
	playerdata->Unlock();
}


local void updateFlags(Arena *arena, TurfArena *ta)
{
	int x = 0;
	TurfFlag *flags = ta->flags;
	ticks_t currentTicks = current_ticks();

	/* increment numdings and weights for all owned flags */
	for(x=0 ; x<ta->numFlags ; x++)
	{
		TurfFlag *flagPtr = &flags[x];
		TurfFlagPrevious *oPtr;
		Link *l, *next;

		if( flagPtr->team )
		{
			/* flag is owned, increment # dings and update weight accordingly */
			flagPtr->dings++;
			flagPtr->weight = calcWeight(arena, ta, flagPtr, currentTicks);
		}

		/* increment lastOwned for every node (previous owners that can recover) */
		for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
		{
			next = l->next;
			oPtr = l->data;
			oPtr->lastOwned++;
		}

		if(ta->settings.recovery_cutoff==TR_RECOVERY_DINGS ||
			ta->settings.recovery_cutoff==TR_RECOVERY_DINGS_AND_TIME)
		{
			/* remove entries for teams that lost the chance to recover
			 * (setting based on dings) */
			for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
			{
				next = l->next;
				oPtr = l->data;

				/* check if lastOwned is within limits */
				if(oPtr->lastOwned > ta->settings.recover_dings)
				{
					/*entry for team that lost chance to recover flag */
					LLRemove(&flagPtr->old, oPtr);
					afree(oPtr);
				}
			}
		}

		if(ta->settings.recovery_cutoff==TR_RECOVERY_TIME ||
			ta->settings.recovery_cutoff==TR_RECOVERY_DINGS_AND_TIME)
		{
			/* remove entries for teams that lost the chance to recover
			 * (setting based on time) */
			for(l = LLGetHead(&flagPtr->old) ; l ; l = next)
			{
				next = l->next;
				oPtr = l->data;

				/* check if lostTC - current time
				 * (time since flag was lost) is still within limits */
				if( (TICK_DIFF(currentTicks, oPtr->lostTC) / 100)
					> ta->settings.recover_time)
				{
					LLRemove(&flagPtr->old, oPtr);
					afree(oPtr);
				}
			}
		}
	}
}


local helptext_t turftime_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Displays the amount of time till next ding.\n";
local void C_turfTime(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	unsigned int days, hours, minutes, seconds;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	/* TODO: change to 00:00:00 format */

	LOCK_STATUS(arena);
	seconds = (ta->settings.timer_interval - TICK_DIFF(current_ticks(), ta->dingTime)) / 100;
	UNLOCK_STATUS(arena);

	if (seconds!=0)
	{
		if ( (minutes = (seconds / 60)) )
		{
			seconds = seconds % 60;
			if ( (hours = (minutes / 60)) )
			{
				minutes = minutes % 60;
				if ( (days = (hours / 24)) )
				{
					hours = hours % 24;
					chat->SendMessage(p,
						"Next ding in: %d days %d hours %d minutes %d seconds.",
						days,
						hours,
						minutes,
						seconds);
				}
				else
					chat->SendMessage(p,
						"Next ding in: %d hours %d minutes %d seconds.",
						hours,
						minutes,
						seconds);
			}
			else
				chat->SendMessage(p,
					"Next ding in: %d minutes %d seconds.",
					minutes,
					seconds);
		}
		else
			chat->SendMessage(p, "Next ding in: %d seconds.", seconds);
	}
}


local helptext_t turfinfo_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Displays the current settings / requirements to recieve awards.\n";
local void C_turfInfo(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	LOCK_STATUS(arena);

	chat->SendMessage(p, "Arena Settings");
	chat->SendMessage(p, "   Reward style: %s", tr_style_str(ta->settings.reward_style));
	chat->SendMessage(p, "   Reward modifier: %d", ta->settings.reward_modifier);
	chat->SendMessage(p, "   Reward every: %d seconds", ta->settings.timer_interval/100);
	chat->SendMessage(p, "   Reward maximum: %d points", ta->settings.max_points);

	if(ta->settings.weight_calc == TR_WEIGHT_DINGS)
		chat->SendMessage(p, "   Weights by: dings");
	else if(ta->settings.weight_calc == TR_WEIGHT_TIME)
		chat->SendMessage(p, "   Weights by: time");

	chat->SendMessage(p, "   Weights defined: %d", ta->settings.set_weights);

	if(ta->settings.recovery_cutoff == TR_RECOVERY_DINGS)
		chat->SendMessage(p, "   Recovery by: dings");
	else if(ta->settings.recovery_cutoff == TR_RECOVERY_TIME)
		chat->SendMessage(p, "   Recovery by: time");
	else if(ta->settings.recovery_cutoff == TR_RECOVERY_DINGS_AND_TIME)
		chat->SendMessage(p, "   Recovery by: dings and time");

	if( (ta->settings.recovery_cutoff == TR_RECOVERY_DINGS) ||
		(ta->settings.recovery_cutoff == TR_RECOVERY_DINGS_AND_TIME) )
		chat->SendMessage(p, "   Recovery cutoff: %d %s", 
			ta->settings.recover_dings, 
			(ta->settings.recover_dings == 1) ? "ding" : "dings");

	if( (ta->settings.recovery_cutoff == TR_RECOVERY_TIME) ||
		(ta->settings.recovery_cutoff == TR_RECOVERY_DINGS_AND_TIME) )
		chat->SendMessage(p, "   Recovery cutoff: %d %s", 
			ta->settings.recover_time, 
			(ta->settings.recover_time == 1) ? "second" : "seconds");

	if(ta->settings.recover_max == -1)
		chat->SendMessage(p, "   Recovery max: none");
	else
		chat->SendMessage(p, "   Recovery max: %d", ta->settings.recover_max);

	chat->SendMessage(p, "   Recieve points in spec: %s", 
		(ta->settings.spec_recieve_points==0) ? "no" : "yes");

	chat->SendMessage(p, "   Recieve points in safe: %s", 
		(ta->settings.safe_recieve_points==0) ? "no" : "yes");

	chat->SendMessage(p, "Arena Requirements");
	chat->SendMessage(p, "   Minimum players: %d", ta->settings.min_players_in_arena);
	chat->SendMessage(p, "   Minimum teams: %d", ta->settings.min_teams);
	chat->SendMessage(p, "Team Requirements");
	chat->SendMessage(p, "   Minimum players: %d", ta->settings.min_players_on_team);
	chat->SendMessage(p, "   Minimum # flags: %d", ta->settings.min_flags);
	chat->SendMessage(p, "   Minimum %% flags: %5.1f", ta->settings.min_percent_flags);
	chat->SendMessage(p, "   Minimum # weights: %d", ta->settings.min_weights);
	chat->SendMessage(p, "   Minimum %% weights: %5.1f", ta->settings.min_percent_weights);

	UNLOCK_STATUS(arena);
}


local helptext_t turfresetflags_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the turf_reward module's and flags module's flag data in your current arena.\n";
local void C_turfResetFlags(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	flagGameReset(arena);  /* does the locking for us already */
}


local helptext_t forceding_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Forces a reward to take place immediately in your current arena.\n";
local void C_forceDing(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	doReward(arena);  /* does the locking for us already */
}


local helptext_t turfresettimer_help =
"Module: turf_reward\n"
"Targets: none\n"
"Args: none\n"
"Resets the ding timer in your current arena.\n";
local void C_turfResetTimer(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena) return;
	dingTimerReset(arena);  /* does the locking for us already */
}


local void flagGameReset(Arena *arena)
{
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	LOCK_STATUS(arena);

	/* clear the data in this module */
	if (ta && ta->flags)
	{
		clearFlagsData(arena, 0);
		clearTeamsData(arena);
		clearPlayersData(arena);
	}

	/* clear the data in the flags module */
	flagcore->FlagReset(arena, -1, 0);

	UNLOCK_STATUS(arena);
}


local void dingTimerReset(Arena *arena)
{
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	LOCK_STATUS(arena);
	if (ta)
	{
		/* get rid of the current timer (if one exists) */
		mainloop->ClearTimer(turfRewardTimer, arena);

		/* now create a new timer */
		ta->dingTime = current_ticks();
		mainloop->SetTimer(turfRewardTimer, ta->settings.timer_initial,
			ta->settings.timer_interval, arena, arena);

		chat->SendArenaSoundMessage(arena, SOUND_BEEP1,
			"Notice: Reward timer reset. Initial:%i Interval:%i",
			ta->settings.timer_initial, ta->settings.timer_interval);
	}
	UNLOCK_STATUS(arena);
}


local TurfArena* GetTurfData(Arena *arena)
{
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return NULL; else ta = *p_ta;
	LOCK_STATUS(arena);
	return ta;
}


local void ReleaseTurfData(Arena *arena)
{
	UNLOCK_STATUS(arena);
}


local void cleanup(void *v)
{
	Arena *arena = v;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;
	mm->ReleaseInterface(ta->trp);
}


local TurfTeam* getTeamPtr(TurfArena *ta, int freq, int createIfDNE)
{
	TurfTeam *pTeam;
	Link *l;

	for(l = LLGetHead(&ta->teams) ; l ; l=l->next)
	{
		pTeam = l->data;
		if(pTeam->freq == freq)
			return pTeam;   /* found the team */
	}

	/* team didn't exist */
	if( createIfDNE )
	{
		/* lets create a node for the team */
		pTeam = amalloc(sizeof(TurfTeam));

		pTeam->freq             = freq;

		pTeam->numFlags         = 0;
		pTeam->percentFlags     = 0.0;
		pTeam->perCapitaFlags   = 0.0;

		pTeam->numWeights       = 0;
		pTeam->percentWeights   = 0.0;
		pTeam->perCapitaWeights = 0.0;

		pTeam->tags             = 0;
		pTeam->steals           = 0;
		pTeam->recoveries       = 0;
		pTeam->lost             = 0;

		pTeam->kills            = 0;
		pTeam->bountyTaken      = 0;
        pTeam->deaths           = 0;
		pTeam->bountyGiven      = 0;

		pTeam->numPlayers       = 0;
		pTeam->percent          = 0.0;
		pTeam->numPoints        = 0;
		pTeam->extra            = NULL;

		LLInit(&pTeam->flags);
		LLInit(&pTeam->players);
		LLInit(&pTeam->playersPts);
		LLInit(&pTeam->playersNoPts);

		/* add the newly created node to the linked list */
		LLAddFirst(&ta->teams, pTeam);
		return pTeam;
	}
	else
	{
		/* dont create a node for the team */
		return NULL;
	}
}


local TurfPlayer* getTurfPlayerPtr(TurfArena *ta, char *name, TurfTeam *team, int createIfDNE)
{
	TurfPlayer *tpPtr;
	Link *l;

	for(l = LLGetHead(&team->players) ; l ; l=l->next)
	{
		tpPtr = l->data;

		if(strncmp(tpPtr->name, name, 24) == 0)
		{
			/* name matched */
			return tpPtr;
		}
	}

	/* didn't find a matching node */
	if( createIfDNE )
	{
		/* lets create the node */
		tpPtr = (TurfPlayer *)amalloc(sizeof(TurfPlayer));

		/* set all of the player's data */
		strncpy(tpPtr->name, name, 24);
		tpPtr->pid = 0;
		tpPtr->team = team;
		LLInit(&tpPtr->flags);
		tpPtr->points = tpPtr->tags = tpPtr->steals = tpPtr->recoveries = tpPtr->lost = 0;
		tpPtr->kills = tpPtr->bountyTaken = tpPtr->deaths = tpPtr->bountyGiven = 0;
		tpPtr->isActive = 0;
		tpPtr->extra = NULL;

		LLAddFirst(&ta->players, tpPtr);   /* add to arena's players linked list */
		LLAddFirst(&team->players, tpPtr); /* add to team's players linked list */
		return tpPtr;
	}
	else
	{
		/* dont create the node */
		return NULL;
	}
}


local void shipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	TurfTeam *pTeam = NULL;
	Arena *arena = p->arena;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	LOCK_STATUS(arena);

	/* make sure the team exists, create if necessary */
	pTeam = getTeamPtr(ta, newfreq, 1);

	/* make sure the player exists, create if necessary */
	getTurfPlayerPtr(ta, p->name, pTeam, 1);

	UNLOCK_STATUS(arena);
}


local void killEvent(Arena *arena, Player *killer, Player *killed,
		int bounty, int flags, int *pts, int *green)
{
	TurfTeam *pTeam = NULL;
	TurfPlayer *pPlayer = NULL;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	LOCK_STATUS(arena);

	/* for player that made the kill */
	/* make sure the team exists, create if necessary */
	pTeam = getTeamPtr(ta, killer->p_freq, 1);
	/* make sure the player exists, create if necessary */
	pPlayer = getTurfPlayerPtr(ta, killer->name, pTeam, 1);
	pPlayer->kills++;
	pPlayer->bountyTaken += bounty;

	/* for player that got killed */
	/* make sure the team exists, create if necessary */
	pTeam = getTeamPtr(ta, killed->p_freq, 1);
	/* make sure the player exists, create if necessary */
	pPlayer = getTurfPlayerPtr(ta, killed->name, pTeam, 1);
	pPlayer->deaths++;
	pPlayer->bountyGiven += bounty;

	/* for arena */
	ta->kills++;
	ta->bountyExchanged += bounty;

	UNLOCK_STATUS(arena);
}


local void postRewardCleanup(TurfArena *ta)
{
	Link *l, *next;

	/* initialize arena for next around */
	ta->numPlayers          = 0;
	ta->numValidPlayers     = 0;
	ta->numTeams            = 0;
	ta->numValidTeams       = 0;
	ta->numInvalidTeams     = 0;
	ta->sumPerCapitaFlags   = 0.0;
	ta->numWeights          = 0;
	ta->sumPerCapitaWeights = 0.0;
	ta->numPoints           = 0;
	ta->tags                = 0;
	ta->steals              = 0;
	ta->recoveries          = 0;
	ta->lost                = 0;
	ta->kills               = 0;
	ta->bountyExchanged     = 0;

	/* players list maintenance */
	for(l=LLGetHead(&ta->players) ; l ; l=next)
	{
		TurfPlayer *pPlayer = l->data;
		next = l->next;

		if( (!pPlayer->isActive) && (LLIsEmpty(&pPlayer->flags)) )
		{
			/* player inactive and not linked to a flag */
			TurfTeam *pTeam = pPlayer->team;

			/* unlink player from team and arena players lists */
			LLRemove(&pTeam->players, pPlayer);
			LLRemove(&ta->players, pPlayer);

			/* inform points module of removal if extra was used */
			if(pPlayer->extra != NULL)
				ta->trp->RemoveTurfPlayer(pPlayer);

			afree(pPlayer);

			continue;
		}

		/* initialize players for next round */
		pPlayer->points      = 0;
		pPlayer->tags        = 0;
		pPlayer->steals      = 0;
		pPlayer->recoveries  = 0;
		pPlayer->lost        = 0;
		pPlayer->kills       = 0;
		pPlayer->bountyTaken = 0;
		pPlayer->deaths      = 0;
		pPlayer->bountyGiven = 0;

		pPlayer->isActive = 0;
	}

	/* empty out the non-players linked list */
	LLEmpty(&ta->playersPts);
	LLEmpty(&ta->playersNoPts);

	/* teams list maintenence */
	for(l=LLGetHead(&ta->teams) ; l ; l=next)
	{
		TurfTeam *pTeam = l->data;
		next = l->next;

		/* empty, do not free */
		LLEmpty(&pTeam->playersPts);
		LLEmpty(&pTeam->playersNoPts);

		if( LLIsEmpty(&pTeam->flags) && LLIsEmpty(&pTeam->players) )
		{
			/* team doesn't own flags or have players, remove it */
			LLRemove(&ta->teams, pTeam);

			/* inform points module of removal if extra was used */
			if( pTeam->extra != NULL )
				ta->trp->RemoveTurfTeam(pTeam);

			afree(pTeam);
			continue;
		}

		/* initialize teams for next round */
		pTeam->perCapitaFlags   = 0.0;
		pTeam->numWeights       = 0;
		pTeam->percentWeights   = 0.0;
		pTeam->perCapitaWeights = 0.0;
		pTeam->tags             = 0;
		pTeam->steals           = 0;
		pTeam->recoveries       = 0;
		pTeam->lost             = 0;
		pTeam->numPlayers       = 0;
		pTeam->percent          = 0.0;
		pTeam->numPoints        = 0;
		pTeam->kills            = 0;
		pTeam->bountyTaken      = 0;
        pTeam->deaths           = 0;
		pTeam->bountyGiven      = 0;
	}

	/* empty out teams lists */
	LLEmpty(&ta->validTeams);
	LLEmpty(&ta->invalidTeams);
}


local int get_tr_owners(Arena *arena, void *data, int len, void *v)
{
#if 0
	int x;
	int flagCount;
	PersistentTurfRewardData *ptfd = data;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return 0; else ta = *p_ta;

	LOCK_STATUS(arena);

	flagCount = ta->numFlags;
	if( (flagCount==0) || (!ta->flags) )
	{
		/* no flags */
		UNLOCK_STATUS(arena);
		return 0;
	}

	for(x=0 ; x<flagCount ; x++)
	{
		ptfd[x].teamFreq  = ta->flags[x].team->freq;
		memcpy(ptfd[x].taggerName, ta->flags[x].tagger->name, 24);
		ptfd[x].dings     = ta->flags[x].dings;
		ptfd[x].weight    = ta->flags[x].dings;
		ptfd[x].recovered = ta->flags[x].dings;
		ptfd[x].tagTC     = ta->flags[x].dings;
		ptfd[x].lastTC    = ta->flags[x].dings;
	}
	
	UNLOCK_STATUS(arena);
	return flagCount * sizeof(PersistentTurfRewardData);
#endif
	return 0;
}


local void set_tr_owners(Arena *arena, void *data, int len, void *v)
{
#if 0
	int x;
	int flagCount;
	PersistentTurfRewardData *ptfd = data;
	TurfArena *ta, **p_ta = P_ARENA_DATA(arena, trkey);
	if (!arena || !*p_ta) return; else ta = *p_ta;

	LOCK_STATUS(arena);

	flagCount = ta->numFlags;
	if( (flagCount==0) || (!ta->flags) || 
		(len!=(flagCount*sizeof(PersistentTurfRewardData))) )
	{
		UNLOCK_STATUS(arena);
		return;
	}

	for(x=0 ; x<flagCount ; x++)
	{
		TurfFlag *pFlag = &ta->flags[x];

		/* hook in team */
		TurfTeam *pTeam = getTeamPtr(ta, ptfd[x].teamFreq, 1);
		LLAdd(&pTeam->flags, pFlag);
		pTeam->numFlags++;
		pTeam->percentFlags 
			= ((double)pTeam->numFlags) / ((double)ta->numFlags) * 100.0;

		/* hook in tagger */
		TurfPlayer *pPlayer = getTurfPlayerPtr(ta, ptfd[x].taggerName, pTeam, 1);
		LLAdd(&pPlayer->flags, pFlag);

		/* set flag's data */
		pFlag->team      = pTeam;
		pFlag->tagger    = pPlayer;
		pFlag->dings     = ptfd[x].dings;
		pFlag->weight    = ptfd[x].weight;
		pFlag->recovered = ptfd[x].recovered;
		pFlag->tagTC     = ptfd[x].tagTC;
		pFlag->lastTC    = ptfd[x].lastTC;
	}

	UNLOCK_STATUS(arena);
#endif
}


local void clear_tr_owners(Arena *arena, void *v)
{
	/* no-op: the arena create action does this already */
}


local ArenaPersistentData persist_tr_owners =
{
	KEY_TR_OWNERS, INTERVAL_GAME, PERSIST_ALLARENAS,
	get_tr_owners, set_tr_owners, clear_tr_owners
};

