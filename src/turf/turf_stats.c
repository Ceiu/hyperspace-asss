
/*
 * Turf Statistics Module for ASSS
 * - gathers and records data from turf_reward
 *
 * TODO:
 * 1.  extend module to handle indiviual player stats, right now it only does
 *     team stats.  Note: before I do individual stats, I would like to modify
 *     turf_reward to support it first
 * 2.  extend module to handle commands with targets (for example,  a command to
 *     get the server to output you team's stats to your team chat)
 * 3.  make output look nice :)
 * 4.  an idea I have is to set a short timer after data has been recorded to
 *     time arena output so that postReward returns as soon as possible, letting
 *     turf_reward finish up and unlock asap.  Otherwise, how it is now,
 *     turf_reward is waiting for all the stats to be outputted before it can
 *     process any new ongoing events since turf_reward is locked.
 *
 * dist: public
 */

#include "asss.h"
#include "turf_reward.h"

/* easy calls for mutex */
#define LOCK_STATUS(arena) \
	pthread_mutex_lock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))

local Imodman     *mm;
local Iplayerdata *playerdata; /* player data */
#define pd playerdata
local Iarenaman   *arenaman;   /* arena manager */
local Iconfig     *config;     /* config (for arena .cfg) services */
local Ilogman     *logman;     /* logging services */
local Ichat       *chat;       /* message players */
local Icmdman     *cmdman;     /* for command handling */

typedef struct TurfStatsData
{
	int numFlags;
	int numPlayers;
	int numTeams;
	long int numWeights;
	unsigned long int numPoints;
	double sumPerCapitaFlags;
	double sumPerCapitaWeights;

	unsigned int tags;
	unsigned int steals;
	unsigned int lost;
	unsigned int recoveries;

	LinkedList teams; /* linked list of TurfTeam structs */
} TurfStatsData;

typedef struct TurfStats
{
	/* config settings */
	int maxHistory;
	int statsOnDing;

	/* stats data */
	int dingCount;
	int numStats;
	LinkedList stats; /* linked list of TurfStatsInfo structs
                       * for history order the idea is to add a link
                       * to the front and remove from the end */
} TurfStats;


local int tskey;  /* key to turf stats data */
local int mtxkey; /* key to turf stats mutexes */


/* function prototypes */
/* connected to callbacks */
local void postReward(Arena *arena, TurfArena *ta);
local void arenaAction(Arena *arena, int action);


/* comands */
local helptext_t turfstats_help, forcestats_help;
/* standard user commands:
 * C_turfStats: to get stats for all teams
 */

 local void C_turfStats(const char *, const char *, Player *, const Target *);
/* mod commands
 * C_forceStats: to force the stats to be outputted to everyone instantly
 */
local void C_forceStats(const char *, const char *, Player *, const Target *);


/* helper functions */
local void clearHistory(Arena *arena);
local void ADisplay(Arena *arena, int histNum);
local void PDisplay(Arena *arena, Player *pid, int histNum);

EXPORT const char info_turf_stats[] = "v0.2.4 by GiGaKiLLeR <gigamon@hotmail.com>";

EXPORT int MM_turf_stats(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)  /* when the module is to be loaded */
	{
		mm = _mm;

		/* get all of the interfaces that we are to use */
		playerdata = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		arenaman   = mm->GetInterface(I_ARENAMAN,   ALLARENAS);
		config     = mm->GetInterface(I_CONFIG,     ALLARENAS);
		logman     = mm->GetInterface(I_LOGMAN,     ALLARENAS);
		chat       = mm->GetInterface(I_CHAT,       ALLARENAS);
		cmdman     = mm->GetInterface(I_CMDMAN,     ALLARENAS);

		/* if any of the interfaces are null then loading failed */
		if (!playerdata || !arenaman || !config || !logman || !chat || !cmdman)
			return MM_FAIL;

		tskey = arenaman->AllocateArenaData(sizeof(TurfStats *));
		mtxkey = arenaman->AllocateArenaData(sizeof(pthread_mutex_t));
		if (tskey == -1 || mtxkey == -1) return MM_FAIL;

		cmdman->AddCommand("turfstats", C_turfStats, ALLARENAS, turfstats_help);
		cmdman->AddCommand("forcestats", C_forceStats, ALLARENAS, forcestats_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)  /* when the module is to be unloaded */
	{
		cmdman->RemoveCommand("turfstats", C_turfStats, ALLARENAS);
		cmdman->RemoveCommand("forcestats", C_forceStats, ALLARENAS);

		arenaman->FreeArenaData(tskey);
		arenaman->FreeArenaData(mtxkey);

		/* release all interfaces */
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(arenaman);
		mm->ReleaseInterface(config);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmdman);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		/* create all necessary callbacks */
		mm->RegCallback(CB_ARENAACTION, arenaAction, arena);
		mm->RegCallback(CB_TURFPOSTREWARD, postReward, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		/* unregister all the callbacks */
		mm->UnregCallback(CB_ARENAACTION, arenaAction, arena);
		mm->UnregCallback(CB_TURFPOSTREWARD, postReward, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

local void arenaAction(Arena *arena, int action)
{
	TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);

	if (action == AA_PRECREATE)
	{
		pthread_mutexattr_t attr;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey), &attr);
		pthread_mutexattr_destroy(&attr);
	}
	else if (action == AA_PRECREATE)
	{
		ts = amalloc(sizeof(TurfStats));
		*p_ts = ts;
	}

	ts = *p_ts;
	LOCK_STATUS(arena);

	if (action == AA_CREATE)
	{
		ConfigHandle c = arena->cfg;
		ts->maxHistory = config->GetInt(c, "TurfStats", "MaxHistory", 0);
		if (ts->maxHistory<0)
			ts->maxHistory = 0;
		ts->statsOnDing = config->GetInt(c, "TurfStats", "StatsOnDing", 1);
		if (ts->statsOnDing<1)
			ts->statsOnDing=1;

		ts->dingCount = 0;
		ts->numStats = 0;
		LLInit(&ts->stats); /* initalize list of stats */
	}
	else if (action == AA_DESTROY)
	{
		/* free history array */
		clearHistory(arena);

		/* might as well, just in case to be safe */
		ts->numStats = 0;
		ts->dingCount = 0;
	}
	else if (action == AA_CONFCHANGED)
	{
		int newMaxHistory;
		ConfigHandle c = arena->cfg;

		ts->statsOnDing = config->GetInt(c, "TurfStats", "StatsOnDing", 1);
		if (ts->statsOnDing<1)
			ts->statsOnDing=1;

		newMaxHistory = config->GetInt(c, "TurfStats", "MaxHistory", 0);
		if (newMaxHistory < 0)
			newMaxHistory = 0;  /* max history must be >= 0 */
		if (newMaxHistory != ts->maxHistory)
		{
			ts->maxHistory = newMaxHistory;

			/* erase history */
			clearHistory(arena);
		}
	}

	UNLOCK_STATUS(arena);

	if (action == AA_DESTROY)
	{
		afree(ts);
	}
	else if (action == AA_POSTDESTROY)
	{
		pthread_mutex_destroy((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey));
	}
}

local void postReward(Arena *arena, TurfArena *ta)
{
	TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l = NULL;
	TurfStatsData *tsd;

	if (!arena || !*p_ts) return; else ts = *p_ts;

	LOCK_STATUS(arena);

	if (ts->numStats >= ts->maxHistory)
	{
		/* we already have the maximum # of histories
		 * erase oldest history (end of linked list) */
		Link *nextL = NULL;
		TurfStatsData *data;

		for(nextL = LLGetHead(&ts->stats) ; nextL ; nextL=nextL->next)
		{
			l = nextL;
		}

		l = LLGetHead(&ts->stats);  /* l now points to the end of the LinkedList */
		if (l) {
			data = l->data;
			LLRemove(&ts->stats, data); /* remove the link */
		}

		ts->numStats--;
	}

	/* create new node for stats data, add it to the linked list, and fill in data */
	tsd = amalloc(sizeof(TurfStatsData));
	LLAddFirst(&ts->stats, tsd);
	ts->numStats++;

	tsd->numFlags       = ta->numFlags;
	tsd->numPlayers     = ta->numPlayers;
	tsd->numTeams       = ta->numTeams;
	tsd->numWeights     = ta->numWeights;
	tsd->numPoints      = ta->numPoints;
	tsd->sumPerCapitaFlags   = ta->sumPerCapitaFlags;
	tsd->sumPerCapitaWeights = ta->sumPerCapitaWeights;

	tsd->tags       = ta->tags;
	tsd->steals     = ta->steals;
	tsd->lost       = ta->lost;
	tsd->recoveries = ta->recoveries;

	/* copy teams data */
	LLInit(&tsd->teams);
	for(l = LLGetHead(&ta->teams) ; l ; l=l->next)
	{
		TurfTeam *src = l->data;
		TurfTeam *dst = amalloc(sizeof(TurfTeam));

		*dst = *src;

		LLAdd(&tsd->teams, dst);
	}

	ts->dingCount++;
	if (ts->dingCount >= ts->statsOnDing)
		ADisplay(arena, 0);  /* output for the history we just copied */

	UNLOCK_STATUS(arena);
}

/* arena should be locked already */
local void clearHistory(Arena *arena)
{
	TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l = NULL;

	if (!arena || !*p_ts) return; else ts = *p_ts;

	for(l = LLGetHead(&ts->stats) ; l ; l=l->next)
	{
		TurfStatsData *data = l->data;;

		LLEnum(&data->teams, afree);
		LLEmpty(&data->teams);
	}

	LLEnum(&ts->stats, afree);
	LLEmpty(&ts->stats);
	ts->numStats = 0;
	ts->dingCount = 0;
}


/* arena should be locked already */
local void ADisplay(Arena *arena, int histNum)
{
	TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l;
	int x;
	TurfStatsData *tsd;
	TurfTeam *pFreq;

	if (!arena || !*p_ts) return; else ts = *p_ts;

	if( (histNum+1) > ts->numStats )
		return;  /* history designated doesn't exist */

	/* get to the right link in ts->stats */
	for(l = LLGetHead(&ts->stats), x=0 ; l ; l=l->next, x++)
	{
		if (x==histNum)
		{
			tsd = l->data;
			break;
		}
	}

	chat->SendArenaMessage(arena, "Freq Plyrs Flags %%Flgs  Wghts %%Wghts   PerCap   %%JP    Pts");
	chat->SendArenaMessage(arena, "---- ----- ----- ----- ------ ------ -------- ----- ------");

	/* tsd now points to the stats we want to output, output freq stats */
	for(l = LLGetHead(&tsd->teams); l; l = l->next)
	{
		int freq, numFlags, numPlayers;
		unsigned int tags, steals, recoveries, lost, numPoints;
		long int numWeights;
		double percentFlags, percentWeights, perCapita, percent;
		pFreq = l->data;

		/* all the data members */
		freq           = pFreq->freq;
		numFlags       = pFreq->numFlags;
		percentFlags   = pFreq->percentFlags;
		numWeights     = pFreq->numWeights;
		percentWeights = pFreq->percentWeights;
		tags           = pFreq->tags;
		steals         = pFreq->steals;
		recoveries     = pFreq->recoveries;
		lost           = pFreq->lost;
		numPlayers     = pFreq->numPlayers;
		perCapita      = pFreq->perCapitaWeights;
		percent        = pFreq->percent;
		numPoints      = pFreq->numPoints;

		if (numPoints > 0)
		{
			if (freq < 100)
			{
				/* public teams */
				chat->SendArenaMessage(arena,
					"%04d %5d %5d %5.1f %6ld %6.1f %8.1f %5.1f %6u",
					freq, numPlayers, numFlags, percentFlags, numWeights,
					percentWeights, perCapita, percent, numPoints);
			}
			else
			{
				/* private teams */
				chat->SendArenaMessage(arena,
					"priv %5d %5d %5.1f %6ld %6.1f %8.1f %5.1f %6u",
					numPlayers, numFlags, percentFlags, numWeights,
					percentWeights, perCapita, percent, numPoints);
			}
		}
	}
}


/* arena should be locked already */
local void PDisplay(Arena *arena, Player *pid, int histNum)
{
	TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	Link *l;
	int x;
	TurfStatsData *tsd;
	TurfTeam *pFreq;

	if (!arena || !*p_ts) return; else ts = *p_ts;

	if( (histNum+1) > ts->numStats )
	{
		chat->SendMessage(pid, "History from %d dings ago is not available.", histNum);
		return;  /* history designated doesn't exist */
	}

	/* get to the right link in ts->stats */
	for(l = LLGetHead(&ts->stats), x=0 ; l ; l=l->next, x++)
	{
		if (x==histNum)
		{
			tsd = l->data;
			break;
		}
	}

	chat->SendMessage(pid, "Freq Plyrs Flags %%Flgs  Wghts %%Wghts   PerCap   %%JP    Pts");
	chat->SendMessage(pid, "---- ----- ----- ----- ------ ------ -------- ----- ------");

	/* tsd now points to the stats we want to output, output freq stats */
	for(l = LLGetHead(&tsd->teams) ; l ; l=l->next)
	{
		int freq, numFlags, numPlayers;
		unsigned int tags, steals, recoveries, lost, numPoints;
		long int numWeights;
		double percentFlags, percentWeights, perCapita, percent;
		pFreq = l->data;

		/* all the data members */
		freq           = pFreq->freq;
		numFlags       = pFreq->numFlags;
		percentFlags   = pFreq->percentFlags;
		numWeights     = pFreq->numWeights;
		percentWeights = pFreq->percentWeights;
		tags           = pFreq->tags;
		steals         = pFreq->steals;
		recoveries     = pFreq->recoveries;
		lost           = pFreq->lost;
		numPlayers     = pFreq->numPlayers;
		perCapita      = pFreq->perCapitaWeights;
		percent        = pFreq->percent;
		numPoints      = pFreq->numPoints;

		if (numPoints > 0)
		{
			if (freq < 100)
			{
				/* public teams */
				chat->SendMessage(pid,
					"%04d %5d %5d %5.1f %6ld %6.1f %8.1f %5.1f %6u",
					freq, numPlayers, numFlags, percentFlags, numWeights,
					percentWeights, perCapita, percent, numPoints);
			}
			else
			{
				/* private teams */
				chat->SendMessage(pid,
					"priv %5d %5d %5.1f %6ld %6.1f %8.1f %5.1f %6u",
					numPlayers, numFlags, percentFlags, numWeights,
					percentWeights, perCapita, percent, numPoints);
			}
		}
	}
}


local helptext_t turfstats_help =
"Module: turf_stats\n"
"Targets: none\n"
"Args: none\n"
"Gets stats to previous dings.\n";
local void C_turfStats(const char *tc, const char *params, Player *p, const Target *target)
{
	int histNum = 0;
	Arena *arena = p->arena;
	if (!arena) return;

	/* TODO: give more functionality using args to get history # so and so,
	 * right now only displays last ding */

	LOCK_STATUS(arena);
	PDisplay(arena, p, histNum);
	UNLOCK_STATUS(arena);
}


local helptext_t forcestats_help =
"Module: turf_stats\n"
"Targets: none\n"
"Args: none\n"
"Displays stats to arena for previous dings.\n";
local void C_forceStats(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	TurfStats *ts, **p_ts = P_ARENA_DATA(arena, tskey);
	int histNum = 0;

	if (!arena || !*p_ts) return; else ts = *p_ts;

	/* TODO: give more functionality using args to get history # so and so,
	 * right now only displays last ding */

	LOCK_STATUS(arena);
	if( (histNum+1) > ts->numStats )
	{
		chat->SendMessage(p,
			"History from %d dings ago is not available.",
			histNum);
		return;  /* history designated doesn't exist */
	}
	ADisplay(arena, histNum);
	UNLOCK_STATUS(arena);
}


