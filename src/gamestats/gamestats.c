/*
	GAMESTATS
	5 Jun 2007
	Author:
	- Justin "Arnk Kilo Dylie" Schwartz
	Contributors:

*/
/*
	All Hockey Zone Code is the property of Hockey Zone and the code authors.
*/

#define SAVE_BUFFER 16384

#include "asss.h"

#define MODULENAME gamestats
#define SZMODULENAME "gamestats"
#define INTERFACENAME Igamestats
#define NOT_USING_PDATA

#ifdef HZ_VERSION
#include "hz_asss.h"
#else
#include "akd_asss.h"
typedef void * Icmdlist;
//typedef void * Ileague;
typedef void * Lplayer;
#define I_CMDLIST "undefined-interface-cmdlist"
//#define I_LEAGUE "undefined-interface-league"
#include "local_hz_util.inc"
#endif

#include "reldb.h"
#include "gamestats.h"
#include "clocks.h"

//somehow these make their way into the pre-included stuff under MSVC and not under GCC..
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <unistd.h>

//a simple macro that will use continue if the type won't ever be written to database (usually non-static variables, which are also skipped)
#define CONTINUE_ON_NONSQL(x) if (!x->sql) continue; if (x->summary) continue; if (x->ratiostat) continue;

//copying HashEntry and HashTable structs from util.c
//the purpose being that we can simulate a slightly faster HashEnum..
//'spose it's not really necessary, but what the heck
struct HashEntry
{
	struct HashEntry *next;
	void *p;
	char key[1];
};

//strlwr is not defined in ANSI C, but it is defined under Microsoft Visual C..
#ifndef _MSC_VER
local void strlwr(char *buffer)
{
	//while the current character is not null
	while (*buffer)
	{
		//convert the character to lowercase
		*buffer = tolower(*buffer);
		//move to the next character
		++buffer;
	}
}
#endif

//other interfaces we want to use besides the usual
local Icmdlist *cl;

local Ireldb *db;
local Iclocks *clocks;


//turn on or off automagic saving of stats to the file
local void toggleAutoSaveStats(Arena *, int on);
//the timer function that will handle saving stats to file automagically if we have it on
local int autoSaveTimerFunc(void *);
local int saveStats(Arena *);
local int loadStats(Arena *);
local void clearSaveStats(Arena *);

//process data into/out of buffers
local int create_buffer(Arena *, void *, int len);
local int read_buffer(Arena *, void *, int len);


/*
	AKD-Gamestats File Format

	assume: word=4bytes, short=2bytes

	define item:
		word - type of item
		short - paramater for the item
		varlen - specific data for the item. varies in size based on the type and param. (see below)

	begin:
	word - length of the rest of the file
	word - checksum of the gamestats configuration
	item1
	item2
	. . .
	itemN
*/

typedef enum //(word)
{
	ITEM_NEW_PERIOD,	//param = HighByte-GameID LowByte-Period. buffer size = 0
	ITEM_NEW_SUMMARY,	//param = Length of Event (including 4-byte time) = size of buffer.
	ITEM_NEW_PLAYER,	//param = Length of Name = size of buffer.
	ITEM_NEW_TEAM,		//param = Team Number. buffer size = sizeof(word) * numberOfStatTypes
	ITEM_PLAYER_INFO,	//param = ship mask. (only player info field at this time.)
} gamestat_item_type;

typedef struct gamestat_persist_item
{
	gamestat_item_type type;
	i16 param;
	char buffer[0];
} gamestat_persist_item;

//the entire file after the word for the length of the file
typedef struct gamestat_persist
{
	//compare this against ad->gamestatConfigurationChecksum. if not equal, this data is from an old configuration and is not valid
	int checksum;

	char items[0];
} gamestat_persist;


typedef struct gamestat_playerinfo
{
	shipmask_t shipmask;
} gamestat_playerinfo;

DEF_PARENA_TYPE
	//a list of all periods.
	LinkedList periodList;

	//a list of all gamestat_types in this arena.
	LinkedList stattypeList;

	//an ordered list of all gamestat_types that will appear in stats spam
	LinkedList tableStatList;

	//an ordered list of all gamestat_types that will appear in ?stats
	LinkedList singleStatList;

	LinkedList tickerList;

	//per-arena interfaces we will use
	//Ileague: used to grab player IDs for database writes
	Ileague *lg;

	int nextId;

	//my per-arena mutex
	pthread_mutex_t arenamutex;
	//my per-arena interface
	INTERFACENAME arenainterface;

	//this is added to a gamestat_period::estimatedQuerySize to determine a safe but not overly excessive buffer size for the query
	int estimatedPlayerSize;

	ticks_t lastTick;

	//whether we are using persistant stats
	int usePersist : 1;
ENDDEF_PARENA_TYPE;


//prototype internal functions here.

local int calculateChecksum(Arena *);	//calculate the checksum for our configuration and module. this should not change unless there's a difference in the stats we record or the module

local void resetArena(Arena *);		//erase all games and periods

local gamestat_period *addPeriod(Arena *, int gameId, int period);	//create a new gameId-period pair
local gamestat_period *getPeriod(Arena *, int gameId, int period);	//find a gamestat_period
local LinkedList *addPlayerStatList(Arena *, gamestat_period *, const char *);	//create a new stat list (linkedlist of gamestat) in a gamestat_period for a player
local LinkedList *getPlayerStatList(Arena *, gamestat_period *, const char *);	//find a stat list (linkedlist of gamestat) in a certain gamestat_period
local gamestat *addPlayerStat(Arena *, LinkedList *, gamestat_type *, int team);	//add a new gamestat into a stat list, associated with a type and team
local gamestat *getPlayerStat(Arena *, LinkedList *, gamestat_type *, int team);	//find a gamestat in a list.

local void rawAdd(Arena *, LinkedList *, gamestat_type *, int team, int amt);	//add a raw value to a stat in a stat list directly
local int rawGet(Arena *, LinkedList *, gamestat_type *, int team);		//get the raw value from a stat list directly
local float rawGetf(Arena *, LinkedList *, gamestat_type *, int team);	//get the final float value from a stat list directly

local void addStat(Arena *, Player *, struct gamestat_type *, int gameId, int period, int team, int amt);	//add a value to a player's stat in a certain period, game, team
local int playerHasStats(Arena *a, const char *name, int gameId, int period);
local float getStat(Arena *, const char *, struct gamestat_type *, int gameId, int period, int team);	//get the value of a player's stat in a certain period, game, team
local void startStatTicker(Arena *, Player *, struct gamestat_type *, int gameId, int period, int team, ticks_t ct);	//start a ticker for a certain stat that will add 1 to the value each second
local void stopStatTicker(Arena *, Player *, struct gamestat_type *, ticks_t ct);	//stop tickers for an arena/player/type. providing null for player or type means "all players" or "all types"
//local int statTickerClockFunc(Clock *, int time, void *clos);	//the function that keeps track of stat tickers and does incrementing

local void statsEnum(Arena *, int gameId, int period, statsenumfunc func, void *clos);	//perform a function on all of the stat lists in a period
local float getStatTotal(Arena *, struct gamestat_type *, int gameId, int period, int team);	//find the total for a certain stattype over all players in a period

local gamestat_type *getStatType(Arena *, const char *name);	//obtain a gamestat_type by exact name
local gamestat_type *findStatType(Arena *a, const char *name);	//obtain a gamestat_type by parital case-insensitive name
local gamestat_type *loadStat(Arena *, const char *name);	//load a new stat type
local float getPlayerRating(Arena *a, const char *n, int gameId, int period, int team);		//calculate the rating for a player
local void spamStatsTable(Arena *a, int gameId, int period, const Target *target);	//spam the stats table to a target (must be player, arena, or list)
local void writePublicStatsToDB(Arena *a, int G, int period);	//generate a SQL query, and then execute it
local void writeLeagueStatsToDB(Arena *a, int G, int period, int leagueSeasonId, int gameId);	//generate a SQL query, and then execute it
local void clearGame(Arena *a, int gameId);		//remove all periods from a certain gameId

local void addSummaryItem(Arena *, int gameId, int period, int time, const char *text, ...);	//add a summary entry (printf format)
local void addRawSummaryItem(Arena *, gamestat_period *, int time, const char *buffer, int len);	//add a summary entry (lower level)

local void setPlayerShipmask(Arena *, const char *, int gameId, shipmask_t mask);
local shipmask_t getPlayerShipmask(Arena *, const char *, int gameId);

MYINTERFACE =
{
	INTERFACE_HEAD_INIT(I_GAMESTATS, "gamestats")
	getStatType,
	getStat,
	addStat,
	startStatTicker,
	stopStatTicker,
	getPlayerRating,
	spamStatsTable,
	writePublicStatsToDB,
	writeLeagueStatsToDB,
	clearGame,
	addSummaryItem,
	addRawSummaryItem,
	statsEnum,
	getStatTotal,
	rawAdd,
	rawGet,
	rawGetf,
	toggleAutoSaveStats,
	saveStats,
	clearSaveStats,
	setPlayerShipmask,
	getPlayerShipmask
};

//DEF_GLOBALINTERFACE;


//commands
local void Cdebug(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t spamstats_help =
"Command: spamstats\n"
"Module: gamestats\n"
"Targets: self\n"
"Syntax:\n"
"  ?spamstats [-p=period] [-g=game]\n"
"Generates the stats spam for you only (stats table, statsbests.)";
local void Cspamstats(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t statdesc_help =
"Command: statdesc\n"
"Module: gamestats\n"
"Targets: arena\n"
"Syntax:\n"
"  ?statdesc <statabbrev>\n"
"Describes a stat based on its code. Returns the full name, and how it is calculated if it is based on other stats.\n";
local void Cstatdesc(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t stats_help =
"Command: stats\n"
"Module: gamestats\n"
"Targets: self, player\n"
"Syntax:\n"
"  ?stats [-p=period] [-g=game] [playername]\n"
" -or- /?stats [-p=period] [-g=game]\n"
"Show the stats of a player, or if none is specified, your own stats.\n";
local void Cstats(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t summary_help =
"Command: summary\n"
"Module: gamestats\n"
"Targets: arena\n"
"Syntax:\n"
"  ?summary [-g=game]\n"
"Show events happening for a period/game. If -g is omitted, shows the summary for all games.\n";
local void Csummary(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t loadstats_help =
"Command: loadstats\n"
"Module: gamestats\n"
"Targets: arena\n"
"Syntax:\n"
"  ?loadstats\n"
"Loads saved stats for this arena name. Saved stats are saved in stats/arenaname\n";
local void Cloadstats(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t savestats_help =
"Command: savestats\n"
"Module: gamestats\n"
"Targets: arena\n"
"Syntax:\n"
"  ?savestats\n"
"Saves stats for this arena. Can be reloaded later with ?loadstats.\n";
local void Csavestats(const char *cmd, const char *params, Player *p, const Target *target);

//callback examples
//this thing doesn't actually do anything but if it's needed later it's here
local void playeraction(Player *, int action, Arena *);		//CB_PLAYERACTION callback prototype

local void stattype_free(void *_type)
{
	gamestat_type *type = (gamestat_type *)_type;
	if (type->name)
		afree(type->name);
	if (type->fullname)
		afree(type->fullname);
	if (type->sqlfield)
		afree(type->sqlfield);
	if (type->summary)
	{
		LLFree(type->summary);
	}
	afree(type);
}

EXPORT const char info_gamestats2[] = "v2.01 by Justin Mark \"Arnk Kilo Dylie\" Schwartz <kilodylie@rshl.org>";
EXPORT int MM_gamestats2(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		//store the provided Imodman interface.
		mm = mm_;

		//get all interfaces first. if a required interface is not available, jump to Lfailload and release the interfaces we did get, and return failure.
		GET_USUAL_INTERFACES();	//several interfaces used in many modules, there's no real harm in getting them even if they're not used

		GETINT(clocks, I_CLOCKS);
		OGETINT(cl, I_CMDLIST); //optional interface
		OGETINT(db, I_RELDB);

		//register per-arena and per-player data.
		REG_PARENA_DATA();

		//malloc and init anything else.

		//init a global mutex if you need one. you only need one if you have a global linkedlist, hashtable, or something cool like that.
		//INIT_MUTEX(globalmutex);

		//register persistant data
		//persist->RegArenaPD(&gamestats_data);

		//register global commands, timers, and callbacks.

		//if cmdlist is loaded, use it.
#ifdef HZ_VERSION
		if (cl)
		{
			cl->AddCmdSection(SZMODULENAME);
			cl->AddCmd(SZMODULENAME, "loadstats", "Load the last saved stats.", TARGETS_ARENA);
			cl->AddCmd(SZMODULENAME, "savestats", "Save the current stats.", TARGETS_ARENA);
		}
#endif

		//finally, return success.

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		//reverse cmdlist actions if applicable
#ifdef HZ_VERSION
		if (cl)
		{
			cl->DelCmdSection(SZMODULENAME);
		}
#endif

		//unregister all timers anyway because we're cool.
		ml->ClearTimer(autoSaveTimerFunc, 0);

		//unreg persistant data
		//persist->UnregArenaPD(&gamestats_data);

		//clear the mutex if we were using it
		//DESTROY_MUTEX(globalmutex);

		//free any other malloced data

		//unregister per-arena and per-player data
		UNREG_PARENA_DATA();

		//release interfaces last.
		//this is where GETINT jumps to if it fails.
Lfailload:
		RELEASE_USUAL_INTERFACES();
		RELEASEINT(clocks);
		RELEASEINT(cl);
		RELEASEINT(db);

		//returns MM_FAIL if we jumped from a failed GETINT or other MM_LOAD action, returns MM_OK if not.
		DO_RETURN();
	}


	else if (action == MM_ATTACH)
	{
		int saveInterval;
		gamestat_type *type;
		char buffer[1024];
		const char *tmp = 0;

		//allocate this arena's per-arena data.
		ALLOC_ARENA_DATA(ad);

		OGETARENAINT(ad->lg, I_LEAGUE);

		//get all per-arena interfaces first. if a required interface is not available, jump to Lfailattachand release the interfaces we did get, and return failure.

		//malloc other things in arena data.
		ad->nextId = 0;
		ad->estimatedPlayerSize = 24;
		ad->lastTick = current_ticks();
		ad->usePersist = 0;
		LLInit(&ad->periodList);
		LLInit(&ad->stattypeList);
		LLInit(&ad->tableStatList);
		LLInit(&ad->singleStatList);
		LLInit(&ad->tickerList);

		//init a per-arena mutex if you need one. you only need one if you have a global linkedlist, hashtable, or something cool like that.
		INIT_MUTEX(ad->arenamutex);

		//register global commands, timers, and callbacks.
		cmd->AddCommand("debug", Cdebug, arena, 0);
		cmd->AddCommand("spamstats", Cspamstats, arena, spamstats_help);
		cmd->AddCommand("loadstats", Cloadstats, arena, loadstats_help);
		cmd->AddCommand("savestats", Csavestats, arena, savestats_help);
		cmd->AddCommand("stats", Cstats, arena, stats_help);
		cmd->AddCommand("statdesc", Cstatdesc, arena, statdesc_help);
		cmd->AddCommand("summary", Csummary, arena, summary_help);

		if (cfg->GetStr(arena->cfg, "gamestats", "tablestats"))
		while (strsplit(cfg->GetStr(arena->cfg, "gamestats", "tablestats"), ", \t\n", buffer, 256, &tmp))
		{
			type = getStatType(arena, buffer);
			LLAdd(&ad->tableStatList, type);
		}
		tmp = 0;
		if (cfg->GetStr(arena->cfg, "gamestats", "singlestats"))
		while (strsplit(cfg->GetStr(arena->cfg, "gamestats", "singlestats"), ", \t\n", buffer, 256, &tmp))
		{
			type = getStatType(arena, buffer);
			LLAdd(&ad->singleStatList, type);
		}


		mm->RegCallback(CB_PLAYERACTION, playeraction, arena);

		saveInterval = 100 * cfg->GetInt(arena->cfg, "gamestats", "autosaveinterval", 90);
		ml->SetTimer(autoSaveTimerFunc, saveInterval, saveInterval, arena, arena);

		//if cmdlist is loaded, use it.
#ifdef HZ_VERSION
		if (cl)
		{
			cl->AddSectionToArena(arena, SZMODULENAME);
		}
#endif

		//register the interface if exposing one.
		INIT_ARENAINTERFACE();
		REG_ARENAINTERFACE();
		//finally, return success.

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		//first try to unregister the interface if exposing one.
		UNREG_ARENAINTERFACE();

		//if cmdlist is loaded, use it.
#ifdef HZ_VERSION
		if (cl)
		{
			cl->RemoveSectionFromArena(arena, SZMODULENAME);
		}
#endif

		//unregister global commands, timers, and callbacks.
		cmd->RemoveCommand("debug", Cdebug, arena);
		cmd->RemoveCommand("spamstats", Cspamstats, arena);
		cmd->RemoveCommand("loadstats", Cloadstats, arena);
		cmd->RemoveCommand("savestats", Csavestats, arena);
		cmd->RemoveCommand("stats", Cstats, arena);
		cmd->RemoveCommand("statdesc", Cstatdesc, arena);
		cmd->RemoveCommand("summary", Csummary, arena);

		//remember to clear ALL timers this arena was using even if they were not set in the MM_ATTACH phase..
		mm->UnregCallback(CB_PLAYERACTION, playeraction, arena);

		ml->ClearTimer(autoSaveTimerFunc, arena);

		//free other things in arena data.
		resetArena(arena);
		LLEnumNC(&ad->stattypeList, stattype_free);
		LLEmpty(&ad->stattypeList);
		LLEmpty(&ad->tableStatList);
		LLEmpty(&ad->singleStatList);
		LLEmpty(&ad->tickerList);

		//clear the mutex if we were using it
		DESTROY_MUTEX(ad->arenamutex);

//Lfailattach:

		RELEASEINT(ad->lg);
		//release this arena's per-arena data
		//including all player's per-player data this module would use
		FREE_ARENA_DATA(ad);

		//returns MM_FAIL if we jumped from a failed GETARENAINT or other MM_ATTACH action, returns MM_OK if not.
		DO_RETURN();
	}


	return MM_FAIL;
}

//body: calculateChecksum
local int calculateChecksum(Arena *a)
{
	DEF_AD(a);
	Link *link;
	gamestat_type *I;
	int result = 0;
	int z = (sizeof(gamestat_persist) + sizeof(gamestat_persist_item)) * 8;
	int i;
	char buffer[16];
	ADZ;

	astrncpy(buffer, I_GAMESTATS, 16);
	i = 0;
	while (buffer[i])
	{
		result += (z++)*((int)buffer[i]);
		++i;
	}
	z += i;

	MYALOCK;
	FOR_EACH(&ad->stattypeList, I, link)
	{
		//don't include unimportant fields that could hurt this anyway...
		CONTINUE_ON_NONSQL(I);

		astrncpy(buffer, I->name, 16);
		i = 0;
		while (buffer[i])
		{
			result += (z++)*((int)buffer[i]);
			++i;
		}
		z += i;
	}
	MYAUNLOCK;

	return result;
}

//body: llenum_stat_free
//to be passed to LLEnum with a stat list (linkedlist of gamestats), so that they may be freed, and make sure the clocks are cleared for them
local void llenum_stat_free(void *w)
{
	gamestat *x = (gamestat *)w;
	if (x->clock)
	{
		clocks->FreeClock(x->clock);
	}
	afree(x);
}

//body: hashneum_statlist_free
//to be passed to HashEnum with a hash table of gamestatlists (linkedlists), so that they may be freed properly
local int hashenum_statlist_free(const char *d, void *_x, void *e)
{
	LinkedList *x = (LinkedList *)_x;
	//free each gamestat in the list
	LLEnumNC(x, llenum_stat_free);
	//then free the list
	LLFree(x);
	return 1;
}

//body: llenum_period_free
//to be passed to LLEnum with a list of gamestat_periods, so that they may be freed properly
local void llenum_period_free(void *w)
{
	//explicit const-removing cast
	gamestat_period *x = (gamestat_period *)w;

	//free each list
	HashEnum(&x->playerStats, hashenum_statlist_free, 0);
	//then free the hashtable
	HashDeinit(&x->playerStats);

	if (x->period == 0) //only the totals period contains this information, the others just link to it
	{
		HashEnum(x->playerInfoTable, hash_enum_afree, 0);
		HashFree(x->playerInfoTable);
	}

	//free each summary item
	LLEnum(&x->summaryList, afree);
	//free the summary list
	LLEmpty(&x->summaryList);

	//clear the list of teams we have
	LLEmpty(&x->teams);
	
	afree(x);
}

//body: llenum_ticker_free
local void llenum_ticker_free(void *w)
{
	gamestat_ticker *x = (gamestat_ticker *)w;

	//unlink from the stat we are using.
	clocks->StopClock(x->clock);

	afree(x);
}

//body: resetArena
local void resetArena(Arena *a)
{
	DEF_AD(a);
	ADV;

	MYALOCK;
	LLEnumNC(&ad->tickerList, llenum_ticker_free);
	LLEmpty(&ad->tickerList);

	LLEnumNC(&ad->periodList, llenum_period_free);
	LLEmpty(&ad->periodList);
	MYAUNLOCK;
}

//body: addPeriod
local gamestat_period *addPeriod(Arena *a, int gameId, int period)
{
	DEF_AD(a);
	gamestat_period *result;
	ADZ;

	result = amalloc(sizeof(gamestat_period));
	result->gameId = gameId;
	result->period = period;
	result->estimatedSummaryQuerySize = 80; //a little more than the size of "INSERT INTO `tbGameSummary` (gameId, intPeriod, intTime) VALUES "
	result->playerCount = 0;
	HashInit(&result->playerStats);
	LLInit(&result->teams);
	LLInit(&result->summaryList);
	MYALOCK;
	if (period == 0) //only totals periods allocate this data, the others just link back
	{
		result->playerInfoTable = HashAlloc();
	}
	else
	{
		gamestat_period *gs_period = getPeriod(a, gameId, 0);
		if (!gs_period)
			gs_period = addPeriod(a, gameId, 0);
		result->playerInfoTable = gs_period->playerInfoTable;
	}

	LLAdd(&ad->periodList, result);
	MYAUNLOCK;
	return result;
}

//body: getPeriod
local gamestat_period *getPeriod(Arena *a, int gameId, int period)
{
	DEF_AD(a);
	Link *link;
	gamestat_period *I, *result = 0;
	ADZ;

	MYALOCK;
	FOR_EACH(&ad->periodList, I, link)
	{
		if ((I->gameId == gameId) && (I->period == period))
		{
			result = I;
			break;
		}
	}
	MYAUNLOCK;

	return result;
}

//body: addPlayerStatList
local LinkedList *addPlayerStatList(Arena *a, gamestat_period *gsp, const char *name)
{
	DEF_AD(a);
	LinkedList *result;
	ADZ;

	result = LLAlloc();
	MYALOCK;
	++gsp->playerCount;
	HashAdd(&gsp->playerStats, name, result);
	MYAUNLOCK;

	return result;
}

//body: getPlayerStatList
local LinkedList *getPlayerStatList(Arena *a, gamestat_period *gsp, const char *name)
{
	DEF_AD(a);
	LinkedList *result;
	ADZ;

	MYALOCK;
	result = HashGetOne(&gsp->playerStats, name);
	MYAUNLOCK;

	return result;
}

//body: addPlayerStat
local gamestat *addPlayerStat(Arena *a, LinkedList *list, gamestat_type *type, int team)
{
	DEF_AD(a);
	gamestat *newstat;
	ADZ;
	newstat = amalloc(sizeof(gamestat));
	newstat->team = team;
	newstat->type = type;
	newstat->value = 0;
	newstat->partial = 0;
	MYALOCK;
	LLAdd(list, newstat);
	MYAUNLOCK;
	return newstat;
}

//body: getPlayerStat
local gamestat *getPlayerStat(Arena *a, LinkedList *list, gamestat_type *type, int team)
{
	DEF_AD(a);
	Link *link;
	gamestat *I, *result = 0;
	ADZ;

	MYALOCK;
	FOR_EACH(list, I, link)
	{
		if ((I->type == type) && (I->team == team))
		{
			result = I;
			break;
		}
	}
	MYAUNLOCK;

	return result;
}

//body: rawAdd
local void rawAdd(Arena *a, LinkedList *statlist, struct gamestat_type *type, int team, int amt)
{
	DEF_AD(a);
	int doReturn = 0;
	gamestat *stat;
	Link *link;

	MYALOCK;
	FOR_EACH(statlist, stat, link)
	{
		if (stat->team != team)
			continue;
		if (stat->type != type)
			continue;

		stat->value += amt;

		doReturn = 1;
		break;
	}
	if (doReturn)
	{
		MYAUNLOCK;
		return;
	}

	stat = addPlayerStat(a, statlist, type, team);
	stat->value = amt;
	MYAUNLOCK;
}

//body: rawGet
local int rawGet(Arena *a, LinkedList *statlist, struct gamestat_type *type, int team)
{
	int sigma = STAT_UNDEFINED_BINARY;
	DEF_AD(a);
	Link *link;
	gamestat *stat;

	MYALOCK;
	FOR_EACH(statlist, stat, link)
	{
		if ((stat->team != team) && (team != -1))
			continue;
		if (stat->type != type)
			continue;

		//if sigma is undefined so far, set it to 0 to indicate we have found a value
		if (sigma == STAT_UNDEFINED_BINARY)
			sigma = 0;

		//if team != -1, we are getting the value for one team
		if (team != -1)
		{
			if (!stat->clock)
			{
				sigma = stat->value;
			}
			else
			{
				int clockReading = clocks->ReadClock(stat->clock);
				int seconds = clockReading / 1000;
				if (clockReading % 1000 > 500)
				{
					//normally would be >=, but I would rather not have this round up twice if adding two stats at 20.5 say
					//because for example when adding "offense time" and "defense time" which add up to "field time"
					//which would logically never exceed the total amount of time of the game,
					//if one is 29.500 seconds and the other is 30.500 seconds, and the game is 60 seconds long, we don't want a reading of 61 for "fieldtime"
					//a reading of 59 would be at least be consistent with that rule
					//I suppose you could argue that we wouldn't want people playing the whole game to be disenfranchised of one second, but honestly whatever..
					//this scenario should be very rare.
					++seconds;
				}
				sigma = seconds;
			}

			break;
		}
		//otherwise we're getting it for all teams
		else
		{
			if (!stat->clock)
			{
				sigma += stat->value;
			}
			else
			{
				int clockReading = clocks->ReadClock(stat->clock);
				int seconds = clockReading / 1000;
				if (clockReading % 1000 > 500)
				{
					//see above note
					++seconds;
				}
				sigma += seconds;
			}
		}
	}

	MYAUNLOCK;

	return sigma;
}

//body: rawGetf
local float rawGetf(Arena *a, LinkedList *statlist, struct gamestat_type *type, int team)
{
	float sigma = STAT_UNDEFINED;
	DEF_AD(a);
	Link *link;
	gamestat *stat;
	double floatingPoint = type?(double)type->floatingPoint:1.0;

	MYALOCK;
	FOR_EACH(statlist, stat, link)
	{
		if ((stat->team != team) && (team != -1))
			continue;
		if (stat->type != type)
			continue;

		//if sigma is undefined so far, set it to 0 to indicate we have found a value
		if (sigma == STAT_UNDEFINED)
			sigma = 0;

		//if team != -1, we are getting the value for one team
		if (team != -1)
		{
			if (!stat->clock)
			{
				sigma = (float)((float)stat->value)/(float)pow(10.0, floatingPoint);
			}
			else
			{
				int clockReading = clocks->ReadClock(stat->clock);
				sigma += ((float)clockReading)/1000.0f;
			}

			break;
		}
		//otherwise we're getting it for all teams
		else
		{
			if (!stat->clock)
			{
				sigma += (float)((float)stat->value)/(float)pow(10.0, floatingPoint);
			}
			else
			{
				int clockReading = clocks->ReadClock(stat->clock);
				sigma += ((float)clockReading)/1000.0f;
			}

		}
	}

	MYAUNLOCK;
	return sigma;
}

//body: addStat
local void addStat(Arena *a, Player *p, struct gamestat_type *type, int gameId, int period, int team, int amt)
{
	DEF_AD(a);

	LinkedList *playerStatList;
	gamestat_period *gs_period;

	MYALOCK;
	gs_period = getPeriod(a, gameId, period);
	if (!gs_period)
	{
		gs_period = addPeriod(a, gameId, period);
	}

	playerStatList = getPlayerStatList(a, gs_period, p->name);

	if (!playerStatList)
	{
		playerStatList = addPlayerStatList(a, gs_period, p->name);
	}

	if (!LLMember(&gs_period->teams, (void *)(long)team))
		LLAdd(&gs_period->teams, (void *)(long)team);

	rawAdd(a, playerStatList, type, team, amt);

	//add to the totals for all-game
	if (period)
		addStat(a, p, type, gameId, 0, team, amt);
	MYAUNLOCK;
}

//body: playerHasStats
local int playerHasStats(Arena *a, const char *name, int gameId, int period)
{
	DEF_AD(a);
	gamestat_period *gs_period;
	int result = 0;

	MYALOCK;
	if ((gs_period = getPeriod(a, gameId, period)) && getPlayerStatList(a, gs_period, name))
	{
		result = 1;
	}
	MYAUNLOCK;

	return result;
}

//body: getStat
local float getStat(Arena *a, const char *name, struct gamestat_type *type, int gameId, int period, int team)
{
	DEF_AD(a);

	Link *link;
	LinkedList *playerStatList = 0;
	gamestat_period *gs_period;

	float denominatorVal;
	float sigma = STAT_UNDEFINED;

	MYALOCK;

	gs_period = getPeriod(a, gameId, period);
	if (!gs_period)
	{
		MYAUNLOCK;
		return STAT_UNDEFINED;
	}

	playerStatList = getPlayerStatList(a, gs_period, name);
	if (!playerStatList)
	{
		MYAUNLOCK;
		return STAT_UNDEFINED;
	}

	if (type)
	{
		if (!type->ratiostat && !type->summary)
		{
			int rawres;

			rawres = rawGet(a, playerStatList, type, team);
			if (rawres != STAT_UNDEFINED_BINARY)
			{
				if (sigma == STAT_UNDEFINED)
					sigma = 0.0f;
				//sigma += rawres;
				sigma += (float)((float)rawres)/(float)pow(10.0, (double)type->floatingPoint);
			}
		}
		else if (type->ratiostat)
		{
			float numeratorVal = getStat(a, name, type->numerator, gameId, period, team);

			denominatorVal = getStat(a, name, type->denominator, gameId, period, team);

			if (numeratorVal == STAT_UNDEFINED)
				numeratorVal = 0.0f;
			if (denominatorVal == STAT_UNDEFINED)
				denominatorVal = 0.0f;
			if (type->denominator && (denominatorVal < type->denominator->denominatorminimum))
				denominatorVal = 0.0f;

			if (denominatorVal != 0.0f)
			{
				sigma = numeratorVal/denominatorVal*type->factor;
			}
			else
			{
				sigma = STAT_UNDEFINED;
			}
		}
		else if (type->summary)
		{
			gamestat_type *f;
			float rawres = STAT_UNDEFINED;

			FOR_EACH(type->summary, f, link)
			{
				float y = getStat(a, name, f, gameId, period, team);
				if (y != STAT_UNDEFINED)
				{
					if (rawres == STAT_UNDEFINED)
						rawres = 0.0f;
					rawres += y;
				}
			}

			sigma = rawres;
		}
	}
	else
	{
		sigma = getPlayerRating(a, name, gameId, period, team);
	}
	MYAUNLOCK;

	return sigma;
}

//body: startStatTicker
local void startStatTicker(Arena *a, Player *p, struct gamestat_type *type, int gameId, int period, int team, ticks_t ct)
{
	DEF_AD(a);
	LinkedList *playerStatList = 0;
	gamestat_period *gs_period = 0;
	gamestat_ticker *ticker;
	gamestat *stat;

	MYALOCK;

	gs_period = getPeriod(a, gameId, period);

	if (!gs_period)
	{
		gs_period = addPeriod(a, gameId, period);
	}

	playerStatList = getPlayerStatList(a, gs_period, p->name);
	if (!playerStatList)
	{
		playerStatList = addPlayerStatList(a, gs_period, p->name);
	}

	stat = getPlayerStat(a, playerStatList, type, team);
	if (!stat)
	{
		stat = addPlayerStat(a, playerStatList, type, team);
	}

	if (!stat->clock)
	{
		stat->clock = clocks->NewClock(CLOCK_COUNTSUP, 0);
	}

	clocks->HoldForSynchronization();

	if (period)
		startStatTicker(a, p, type, gameId, 0, team, ct);

	clocks->StartClock(stat->clock);

	clocks->DoneHolding();

	ticker = amalloc(sizeof(*ticker));
	ticker->p = p;
	ticker->type = type;
	ticker->clock = stat->clock;
	LLAdd(&ad->tickerList, ticker);

	MYAUNLOCK;
}

//body: stopStatTicker
local void stopStatTicker(Arena *a, Player *p, struct gamestat_type *type, ticks_t ct)
{
	Link *link;
	DEF_AD(a);

	gamestat_ticker *x;

	MYALOCK;

	clocks->HoldForSynchronization();

	FOR_EACH(&ad->tickerList, x, link)
	{
		if (!p || (x->p == p))
		{
			if (!type || (x->type == type))
			{
				clocks->StopClock(x->clock);

				LLRemove(&ad->tickerList, x);
				afree(x);
			}
		}
	}
	clocks->DoneHolding();

	MYAUNLOCK;
}



typedef struct statsenumdata
{
	statsenumfunc func;
	Arena *a;
	void *clos;
} statsenumdata;

local int substatsenum(const char *key, void *val, void *_data)
{
	statsenumdata *data = (statsenumdata *)_data;
	data->func(data->a, key, val, data->clos);
	return 0;
}

//body: statsEnum
local void statsEnum(Arena *a, int gameId, int period, statsenumfunc func, void *clos)
{
	gamestat_period *gs_period;
	DEF_AD(a);

	MYALOCK;
	gs_period = getPeriod(a, gameId, period);
	if (gs_period)
	{
		statsenumdata data;
		data.a = a;
		data.func = func;
		data.clos = clos;

		HashEnum(&gs_period->playerStats, substatsenum, &data);
	}
	MYAUNLOCK;
}

//body: getStatTotal
typedef struct ordering_prep
{
	Arena *a;
	LinkedList *list;
	int gameId;
	int period;
	int team;
	int trim;
} ordering_prep;

typedef struct ordering_data
{
	const char *name;
	float rating;
	LinkedList *list;
} ordering_data;

local int hashenum_statorder(const char *name, void *liststat, void *clos)
{
	ordering_prep *data = (ordering_prep *)clos;
	float rating;
	ordering_data *order;

	rating = getPlayerRating(data->a, name, data->gameId, data->period, data->team);
	if ((rating == STAT_UNDEFINED) && (data->trim))
		return 0;

	order = amalloc(sizeof(ordering_data));
	order->name = name;
	order->rating = rating;
	order->list = liststat;

	LLAdd(data->list, order);
	return 0;
}

//body: getStatTotal
local float getStatTotal(Arena *a, struct gamestat_type *type, int gameId, int period, int team)
{
	DEF_AD(a);
	float val = 0.0f;
	LinkedList statOrder = LL_INITIALIZER;
	LinkedList totals = LL_INITIALIZER;
	Link *link;

	gamestat_period *gs_period = 0;

	ordering_prep prep;
	ordering_data *data;

	prep.a = a;
	prep.list = &statOrder;
	prep.gameId = gameId;
	prep.period = period;
	prep.team = team;
	prep.trim = 0;

	MYALOCK;

	gs_period = getPeriod(a, gameId, period);

	if (gs_period)
	{
		HashEnum(&gs_period->playerStats, hashenum_statorder, &prep);
		//LLSort(&statOrder, statsOrderLLSort);

		FOR_EACH(&statOrder, data, link)
		{
			val = getStat(a, data->name, type, gameId, period, team);
			if (val != STAT_UNDEFINED)
				rawAdd(a, &totals, type, team, (int)(val * pow(10, type->floatingPoint)));

			afree(data);
		}

		val = rawGetf(a, &totals, type, team);
		if (val == STAT_UNDEFINED)
			val = 0.0f;

		LLEmpty(&statOrder);
		LLEmpty(&totals);
	}

	MYAUNLOCK;

	return val;
}

//body: getStatType
local gamestat_type *getStatType(Arena *a, const char *name)
{
	DEF_AD(a);
	Link *link;
	gamestat_type *type = 0, *x;

	MYALOCK;
	FOR_EACH(&ad->stattypeList, x, link)
	{
		if (!strcasecmp(x->name, name))
		{
			type = x;
			break;
		}
		//break out when we have found our type
	}
	MYAUNLOCK;

	if (!type)
		type = loadStat(a, name);
	return type;
}

//body: findStatType
local gamestat_type *findStatType(Arena *a, const char *name)
{
	DEF_AD(a);
	Link *link;
	gamestat_type *type = 0, *x;
	char buf[300];
	strcpy(buf, name);
	strlwr(buf);

	MYALOCK;
	FOR_EACH(&ad->stattypeList, x, link)
	{
		if (!strcasecmp(x->name, name))
		{
			type = x;
			break;
		}
		//break out when we have found our type
	}
	MYAUNLOCK;

	if (!type)
	{
		MYALOCK;
		FOR_EACH(&ad->stattypeList, x, link)
		{
			char buffer[300];

			strcpy(buffer, x->name);
			strlwr(buffer);
			if ((strlen(name) < strlen(x->name)) && (strstr(buffer, buf) == buffer))
			{
				type = x;
				break;
			}
			//break out when we have found our type
		}
		MYAUNLOCK;
	}

	return type;
}

local gamestat_type *loadStat(Arena *a, const char *name)
{
	DEF_AD(a);
	char buffer[300];
	const char *tmp = 0;
	const char *str;
	unsigned long namelen;

	gamestat_type *newtype = amalloc(sizeof(gamestat_type));
	newtype->id = ad->nextId++;
	namelen = strlen(name);

	newtype->name = amalloc(sizeof(char) * (namelen + 1));
	strcpy(newtype->name, name);

	if ((str = cfg->GetStr(a->cfg, "gamestat_sqlfields", name)))
	{
		newtype->sql = 1;
		/*newtype->sqlfield = amalloc(sizeof(char) * (strlen(cfg->GetStr(a->cfg, "gamestat_sqlfields", name)) + 1));
		strcpy(newtype->sqlfield, cfg->GetStr(a->cfg, "gamestat_sqlfields", name));*/
		newtype->sqlfield = amalloc(sizeof(char) * (strlen(str) + 1));
		strcpy(newtype->sqlfield, str);

		ad->estimatedPlayerSize += 14;
	}
	else
	{
		newtype->sql = 0;
		newtype->sqlfield = amalloc(sizeof(char));
		*(newtype->sqlfield) = 0;
	}

	if ((str = cfg->GetStr(a->cfg, "gamestat_fullnames", name)))
	{
		/*newtype->fullname = amalloc(sizeof(char) * (strlen(cfg->GetStr(a->cfg, "gamestat_fullnames", name)) + 1));
		strcpy(newtype->fullname, cfg->GetStr(a->cfg, "gamestat_fullnames", name));*/
		newtype->fullname = amalloc(sizeof(char) * (strlen(str) + 1));
		strcpy(newtype->fullname, str);
	}
	else
	{
		newtype->fullname = amalloc(sizeof(char) * (namelen + 1));
		strcpy(newtype->fullname, name);
	}


	newtype->showbest = cfg->GetInt(a->cfg, "gamestat_showbest", name, 0);

	newtype->denominatorminimum = cfg->GetInt(a->cfg, "gamestat_denominatorminimums", name, 0);

	newtype->ratingValue = cfg->GetInt(a->cfg, "gamestat_ratingvalues", name, 0);
	newtype->floatingPoint = cfg->GetInt(a->cfg, "gamestat_floatingpoints", name, 0);
	newtype->textWidth = cfg->GetInt(a->cfg, "gamestat_textwidths", name, 3);

	newtype->time = cfg->GetInt(a->cfg, "gamestat_timestats", name, 0);

	newtype->showtotal = cfg->GetInt(a->cfg, "gamestat_showtotal", name, 1);

	if ((str = cfg->GetStr(a->cfg, "gamestat_sumstats", name)) && strcmp(str, ""))
	{
		gamestat_type *type;
		newtype->summary = LLAlloc();
		newtype->sql = 0;
		while (strsplit(str, ", \t\n", buffer, 256, &tmp))
		{
			type = getStatType(a, buffer);
			LLAdd(newtype->summary, type);
		}
	}
	else if ((str = cfg->GetStr(a->cfg, "gamestat_ratiostats", name)) && strcmp(str, ""))
	{
		int donenumerator = 0;
		newtype->ratiostat = 1;
		newtype->factor = 1.0f;
//		chat->SendArenaMessage(a, "stat %s is a ratiostat.", newtype->name);

		while (strsplit(str, "*/", buffer, 256, &tmp))
		{
			//the first token is a number, the factor for this ratio stat (number to multiply the result of the stuff with.)
			if ((*buffer >= '0') && (*buffer <= '9'))
			{
				newtype->factor = (float)atof(buffer);
				//chat->SendArenaMessage(a, "stat %s has factor %.1f", newtype->name, newtype->factor);
			}
			else
			{
				//now we're specifying the numerator
				if (!donenumerator)
				{
					if (strcmp(buffer, "_RATING_"))
					{
						newtype->numerator = getStatType(a, buffer);
					//	chat->SendArenaMessage(a, "stat %s has a numerator of %s", newtype->name, buffer);
					}
					else
					{
					//	chat->SendArenaMessage(a, "stat %s has a numerator of the player rating.", newtype->name);
					}

					donenumerator = 1;
				}
				else
				{
					//now we're specifying the denominator
					if (!newtype->denominator)
					{
						if (strcmp(buffer, "_RATING_"))
						{
							newtype->denominator = getStatType(a, buffer);
							//chat->SendArenaMessage(a, "stat %s has a denominator of %s", newtype->name, buffer);
						}
						else
						{
							//chat->SendArenaMessage(a, "stat %s has a denominator of the player rating.", newtype->name);
						}
					}
					else //we already have everything we need.
					{
						break;
					}
				}
			}
		}
	}

	MYALOCK;
	LLAdd(&ad->stattypeList, newtype);
	MYAUNLOCK;

	return newtype;
}

//body: getPlayerRating
local float getPlayerRating(Arena *a, const char *name, int gameId, int period, int team)
{
	DEF_AD(a);
	gamestat_type *type;
	Link *link;
	float rating = STAT_UNDEFINED;
	float val;

	MYALOCK;
	FOR_EACH(&ad->stattypeList, type, link)
	{
		if (type->ratiostat)
			continue;

		val = getStat(a, name, type, gameId, period, team);
		if ((val != STAT_UNDEFINED) && (type->ratingValue))
		{
			if (rating == STAT_UNDEFINED)
				rating = 0.0f;

			rating += (type->ratingValue * val) / 10.0f;
		}
	}
	MYAUNLOCK;
	return rating;
}

local int llsort_statorder(const void *aparam, const void *bparam)
{
	const ordering_data *a = (ordering_data *)aparam;
	const ordering_data *b = (ordering_data *)bparam;
	//if the first is less than the last, make the last come first
	//otherwise it's fine
	if (a->rating < b->rating)
		return 0;
	return 1;
}

local int llsort_int(const void *a, const void *b)
{
	//if the first is less than the last, then it's fine
	//otherwise make the last come first
	if (a < b)
		return 1;
	return 0;
}
//body: spamStatsTable
local void spamStatsTable(Arena *a, int gameId, int period, const Target *target)
{
	DEF_AD(a);
	//various buffers for our formatting and outputting pleasure
	char rowBuffer[256], lineOfPeriods[256], lineOfAsterisks[256], statstringBuffer[128], formatBuffer[64], timebuffer[64];
	int totallen;
	int team;
	void *_team;
	float val = 0.0f;
	LinkedList statOrder = LL_INITIALIZER;
	LinkedList totals = LL_INITIALIZER;
	Link *link, *link2, *link3;
	float *bestArray;
	char **bestArrayName;
	int i;
	int maxDisplayPerTeam = cfg->GetInt(a->cfg, "Gamestats", "MaxRowsPerTeam", 10);

	Target _T;

	gamestat_type *type;
	gamestat_type *breakOn = 0;

	gamestat_period *gs_period = 0;

	//a struct we will pass to hashenum_statorder, which is the first step into getting a sorted-by-rating list of players on the team
	//the result is put into statOrder, and we will pass statOrder to llsort_statorder to sort these by rating
	ordering_prep prep;
	prep.a = a;
	prep.list = &statOrder;
	prep.gameId = gameId;
	prep.period = period;
	//indicates we are interested in ignoring people who haven't obtained rating (save time on people who got 1 second of play time)
	prep.trim = 1;


//odd problems with Ichat::SendSetMessage make me choose this alternative for safe practice
//even though it's a bit ugly
#define SENDMSG(x) if (target->type == T_LIST) chat->SendSetMessage((LinkedList *)&target->u.list, "%s", x); else if (target->type == T_ARENA) chat->SendArenaMessage(target->u.arena, "%s", x); else if (target->type == T_PLAYER) chat->SendMessage(target->u.p, "%s", x);
#define SENDMSG1(x,m) if (target->type == T_LIST) chat->SendSetMessage((LinkedList *)&target->u.list, x, m); else if (target->type == T_ARENA) chat->SendArenaMessage(target->u.arena, x, m); else if (target->type == T_PLAYER) chat->SendMessage(target->u.p, x, m);
#define SENDMSG2(x,m,n) if (target->type == T_LIST) chat->SendSetMessage((LinkedList *)&target->u.list, x, m, n); else if (target->type == T_ARENA) chat->SendArenaMessage(target->u.arena, x, m, n); else if (target->type == T_PLAYER) chat->SendMessage(target->u.p, x, m, n);

	//for easy transition with old code, allow target to be null
	//in which case we will set the target to this arena via _T
	if (!target)
	{
		_T.type = T_ARENA;
		_T.u.arena = a;
		target = &_T;
	}

	//CLICK
	MYALOCK;

	//quickly allocate space from the stack for a indeterminate-at-compile-time size array
	//ad->nextId also represents the maximum number of stats we will want to record Bests for
	//but we also want to keep track of rating, so add 1 to this
	bestArray = alloca((ad->nextId + 1) * sizeof(float));
	bestArrayName = alloca((ad->nextId + 1) * sizeof(char *));
	for (i = 0; i <= ad->nextId; ++i)
	{
		bestArray[i] = 0;
		bestArrayName[i] = 0;
	}

	//bam!
	gs_period = getPeriod(a, gameId, period);

	if (!gs_period)
	{
		SENDMSG(" no stats found from this period.");
		MYAUNLOCK;
		return;
	}

	//sort the list of teams we have stats for
	//since we want to display the teams in ascending order.
	LLSort(&gs_period->teams, llsort_int);


	//start forming the column header row
	sprintf(rowBuffer, ":              RATING ");
	//length of above string is 22
	totallen = 22;

	//go through each stat type, ignore all types that we do not have listed as going into the table
	FOR_EACH(&ad->stattypeList, type, link)
	{
		if (!LLMember(&ad->tableStatList, type))
			continue;

		//make sure that we are not overdoing it..248 is arbitrary
		if (totallen + type->textWidth + 2 >= 248)
		{
			lm->LogA(L_ERROR, "gamestats", a, "table overflows starting with column %s!", type->name);
			breakOn = type;
			break;
		}

		//now format where the text will go
		totallen += type->textWidth + 2;
		sprintf(formatBuffer, " %%-%i.%is ", type->textWidth, type->textWidth);
		sprintf(statstringBuffer, formatBuffer, type->name);
		//and add it on to the rowBuffer.
		strcat(rowBuffer, statstringBuffer);
	}

	//initialize lineOfAsterisks
	memset(lineOfAsterisks, '*', totallen);
	lineOfAsterisks[totallen] = 0;
	SENDMSG(lineOfAsterisks);

	if (period)
	{
		SENDMSG2("* Period %i Stats (game %i)", period, gameId);
	}
	else
	{
		SENDMSG1("* GAME %i TOTAL STATS", gameId);
	}

	if (maxDisplayPerTeam)
	{
		SENDMSG1("* Displaying the top %i players per team", maxDisplayPerTeam);
	}

	//column header
	SENDMSG(rowBuffer);

	//initialize lineOfPeriods
	memset(lineOfPeriods, '.', totallen);
	lineOfPeriods[totallen] = 0;
	SENDMSG(lineOfPeriods);

	//obsolete method of iterating through teams is gone
	/*for (team = 0; team <= ad->highestTeam; ++team)*/

	//now start displaying player's stats, for each team we have
	//(this uses link for its Link, btw.)
	FOR_EACH(&gs_period->teams, _team, link)
	{
		int displayed = 0;
		//this is the type used in statOrder
		ordering_data *data;

		//set the team in prep
		team = (int)(long)_team;
		prep.team = team;
		//then get our statOrder
		HashEnum(&gs_period->playerStats, hashenum_statorder, &prep);
		LLSort(&statOrder, llsort_statorder);

		//zero our totals statlist
		LLEmpty(&totals);

		//now go through each player
		FOR_EACH(&statOrder, data, link2)
		{
			//this is the player's statlist in this period/game
			LinkedList *activeList = data->list;

			val = data->rating;

			//stupid precision technicalities. assume values will never reach 10000 and then make sure this thing always rounds correctly
			//don't care if there's a better way..
			rawAdd(a, &totals, 0, team, (int)(val * 10.00001f));

			//start making our row buffer with our rating
			sprintf(rowBuffer, ": %-12.12s %-6.1f ", data->name, (val!=STAT_UNDEFINED)?val:0.0f);


			//iterate through each stat
			FOR_EACH(&ad->stattypeList, type, link3)
			{
				//don't process more types than we can handle
				//this will break this loop when we reach the stat that was determined to be the overflowing one
				if (type == breakOn)
					break;

				//shortcut if we can..rawGetf is certainly faster, but isn't available for non-static stats like ratio stats and summary stats
				if (!type->summary && !type->ratiostat)
					val = rawGetf(a, activeList, type, team);
				else
					val = getStat(a, data->name, type, gameId, period, team);

				//(type->showbest > 0) indicates we want to show the best in this stat
				//also ignore values <= 0, nothing special about those
				if ((type->showbest > 0) && (val > 0.f))
				{
					//if there is already a Best listed..
					if (bestArrayName[type->id])
					{
						//if our value is better...
						if (val > bestArray[type->id])
						{
							//format a replacement string and use it.
							char temp2[128];
							sprintf(temp2, "Most %%s (%%.%if): %%s", type->floatingPoint);
							sprintf(bestArrayName[type->id], temp2, type->fullname, val, data->name);
							//remember our value as the best.
							bestArray[type->id] = val;
						}
						//else if our value is the same...
						else if (val == bestArray[type->id])
						{
							//format an additional string and concatenate it
							char verytempbuf[32];
							sprintf(verytempbuf, "  %s", data->name);
							//remember to not buffer overflow
							strncat(bestArrayName[type->id], verytempbuf, 199);
							bestArrayName[type->id][199] = 0;
						}
					}
					else
					//there is no Best listed yet..
					{
						char temp2[128];
						//allocate space for our string
						bestArrayName[type->id] = alloca(sizeof(char) * 200);
						//format a string and use it
						sprintf(temp2, "Most %%s (%%.%if): %%s", type->floatingPoint);
						sprintf(bestArrayName[type->id], temp2, type->fullname, val, data->name);
						//set our value
						bestArray[type->id] = val;
					}
				}


				//if it's a stat we use in the table, put it in the table..
				if (LLMember(&ad->tableStatList, type))
				{
					//add it to the totals for this team
					if (val != STAT_UNDEFINED)
						rawAdd(a, &totals, type, team, (int)(val * pow(10.0f, type->floatingPoint)));

					if (!type->time)
					{
						if (!type->ratiostat || (val != STAT_UNDEFINED))
						{
							sprintf(formatBuffer, " %%-%i.%if ", type->textWidth, type->floatingPoint);
							sprintf(statstringBuffer, formatBuffer, (val!=STAT_UNDEFINED)?val:0.0f);
							strcat(rowBuffer, statstringBuffer);
						}
						else
						{
							//if this is an undefined ratio stat, fill in with dashes
							sprintf(formatBuffer, " %%-%i.%is ", type->textWidth, type->floatingPoint + 2);
							sprintf(statstringBuffer, formatBuffer, "----------");
							strcat(rowBuffer, statstringBuffer);
						}
					}
					else
					{
						//use formatTime for times
						sprintf(formatBuffer, " %%-%i.%is ", type->textWidth, type->textWidth);
						sprintf(statstringBuffer, formatBuffer, formatTime((val!=STAT_UNDEFINED)?(int)val:0, timebuffer));
						strcat(rowBuffer, statstringBuffer);
					}
				}
			}

			//now do HRP check..
			if (bestArrayName[ad->nextId])
			{
				//ad->nextId is accoutned for in the allocation
				//it is used for "rating" (instead of 0 because we want this to appear at the bottom)
				if (data->rating > bestArray[ad->nextId])
				{
					sprintf(bestArrayName[ad->nextId], "Highest Rated Player  (%.1f): %s", data->rating, data->name);
					bestArray[ad->nextId] = data->rating;
				}
				else if (data->rating == bestArray[ad->nextId])
				{
					char verytempbuf[32];
					sprintf(verytempbuf, "  %s", data->name);
					strncat(bestArrayName[ad->nextId], verytempbuf, 199);
					//sneaky way to make it "highest rated players"
					bestArrayName[ad->nextId][20] = 's';
					bestArrayName[ad->nextId][199] = 0;
				}
			}
			else
			{
				bestArrayName[ad->nextId] = alloca(sizeof(char) * 200);
				sprintf(bestArrayName[ad->nextId], "Highest Rated Player  (%.1f): %s", data->rating, data->name);
				bestArray[ad->nextId] = data->rating;
			}

			//AND WE'RE DONE..
			//SENDMSG(rowBuffer);
			if (!maxDisplayPerTeam || displayed < maxDisplayPerTeam)
			{
				SENDMSG1("%s", rowBuffer);
				++displayed;
			}
			//we're done with this entry in statRow
			afree(data);
		}


		//NOW WORK ON THE TOTALS ROW.

		//get the rating (type == 0)
		val = rawGetf(a, &totals, 0, team);
		sprintf(rowBuffer, ": %-12.12s %-6.1f ", "@@@@ TOTALS", (val!=STAT_UNDEFINED)?val:0.0f);

		//format the stuff and go, same story as before.
		FOR_EACH(&ad->stattypeList, type, link2)
		{
			//don't process more types than we can handle
			//this will break this loop when we reach the stat that was determined to be the overflowing one
			if (type == breakOn)
				break;
			if (!LLMember(&ad->tableStatList, type))
				continue;

			if (type->showtotal)
			{
				val = rawGetf(a, &totals, type, team);

				if (!type->time)
				{
					sprintf(formatBuffer, " %%-%i.%if ", type->textWidth, type->floatingPoint);
					sprintf(statstringBuffer, formatBuffer, (val!=STAT_UNDEFINED)?val:0.0f);
					strcat(rowBuffer, statstringBuffer);
				}
				else
				{
					sprintf(formatBuffer, " %%-%i.%is ", type->textWidth, type->textWidth);
					sprintf(statstringBuffer, formatBuffer, formatTime((val!=STAT_UNDEFINED)?(int)val:0, timebuffer));
					strcat(rowBuffer, statstringBuffer);
				}
			}
			else
			{
				sprintf(formatBuffer, " %%-%i.%is ", type->textWidth, type->textWidth);
				sprintf(statstringBuffer, formatBuffer, "----");
				strcat(rowBuffer, statstringBuffer);
			}
		}
		SENDMSG(rowBuffer);


		LLEmpty(&statOrder);
		SENDMSG(lineOfPeriods);
	}

	SENDMSG(lineOfAsterisks);

	//depreciated
	//FOR_EACH(ad->stattypes, type, link)

	//ad->nextId == rating stat
	for (i = 0; i <= ad->nextId; ++i)
	{
		//if there is a best, bam!
		if (bestArrayName[i])
		{
			SENDMSG1("%s", bestArrayName[i]);
		}
	}

	MYAUNLOCK;

#undef SENDMSG
#undef SENDMSG1
#undef SENDMSG2
}

typedef struct dbwrite_preplite
{
	Arena *a;
	i16 game;
	i16 period;
	char *buffer;
	int i;
} dbwrite_preplite;


local int hashenum_stats_sqlbuffer_lite(const char *key, void *val, void *clos)
{
	dbwrite_preplite *prep = (dbwrite_preplite *)clos;
	float rating = 0.f;
	Link *link;
	float s;
	DEF_AD(prep->a);
	gamestat_type *type;
	char escapedName[64];
	int esc;

	char *buffer = alloca(sizeof(char) * (128 + (ad->estimatedPlayerSize)));
	char statstringBuffer[128];

	if (prep->i)
		strcat(prep->buffer, ", ");

	esc = db->EscapeString(key, escapedName, sizeof(escapedName));
	if (!esc)
		return 0;

	sprintf(buffer, " ('%s'", escapedName);

	FOR_EACH(&ad->stattypeList, type, link)
	{
		if (type->summary)
			continue;
		if (type->ratiostat)
			continue;
		if (!type->sql)
			continue;

		s = getStat(prep->a, key, type, prep->game, prep->period, -1);
		if (s == STAT_UNDEFINED)
			s = 0.0f;
		sprintf(statstringBuffer, ", %i", (int)s);

		strcat(buffer, statstringBuffer);

		if (!type->ratiostat)
		{
			if (type->ratingValue)
			{
				rating += (type->ratingValue * s) / 10.0f;
			}
		}
	}

	sprintf(statstringBuffer, ", %.2f)", rating);
	strcat(buffer, statstringBuffer);

	strcat(prep->buffer, buffer);
	prep->i = 1;
	return 0;
}


typedef struct dbwrite_prep
{
	Arena *a;
	gamestat_period *gs_period;
	i16 leagueSeasonId;
	i16 gameId;
	i16 game;
	i16 period;
	char *buffer;
	int i;
} dbwrite_prep;

local int hashenum_stats_sqlbuffer(const char *key, void *val, void *clos)
{
#ifdef HZ_VERSION
	dbwrite_prep *prep = (dbwrite_prep *)clos;
	Lplayer *lp;
	float rating = 0.f;
	int playerId;
	int teamId;
	Link *link;
	float s;
	DEF_AD(prep->a);
	gamestat_type *type;
	gamestat_playerinfo *gs_info;
	shipmask_t shipmask;

	char *buffer = alloca(sizeof(char) * (128 + (ad->estimatedPlayerSize)));
	char statstringBuffer[128];

	if (!ad->lg)
		return 0;
	lp = ad->lg->getPlayerByName(prep->a, key);
	if (!lp)
	{
		return 0;
	}

	playerId = lp->playerId;
	if (!playerId)
	{
		return 0;
	}

	teamId = lp->teamId;

	if (prep->i)
		strcat(prep->buffer, ", ");

	gs_info = HashGetOne(prep->gs_period->playerInfoTable, key);
	if (gs_info)
		shipmask = gs_info->shipmask;
	else
		shipmask = 0;

	if (prep->gameId)
	{
		if (shipmask > 0)
			sprintf(buffer, " (%i, %i, %i, %i, %i, %i", prep->period, teamId, playerId, prep->leagueSeasonId, prep->gameId, shipmask);
		else
			sprintf(buffer, " (%i, %i, %i, %i, %i, NULL", prep->period, teamId, playerId, prep->leagueSeasonId, prep->gameId);
	}
	else
	{
		if (shipmask > 0)
			sprintf(buffer, " (%i, %i, %i, %i, <GAMEID>, %i", prep->period, teamId, playerId, prep->leagueSeasonId, shipmask);
		else
			sprintf(buffer, " (%i, %i, %i, %i, <GAMEID>, NULL", prep->period, teamId, playerId, prep->leagueSeasonId);
	}

	FOR_EACH(&ad->stattypeList, type, link)
	{
		if (type->summary)
			continue;
		if (type->ratiostat)
			continue;
		if (!type->sql)
			continue;

		s = getStat(prep->a, key, type, prep->game, prep->period, -1);
		if (s == STAT_UNDEFINED)
			s = 0.0f;
		sprintf(statstringBuffer, ", %i", (int)s);

		strcat(buffer, statstringBuffer);

		if (!type->ratiostat)
		{
			if (type->ratingValue)
			{
				rating += (type->ratingValue * s) / 10.0f;
			}
		}
	}

	sprintf(statstringBuffer, ", %.2f)", rating);
	strcat(buffer, statstringBuffer);

	//strcat(buffer, ")");

	strcat(prep->buffer, buffer);
	prep->i = 1;
#endif
	return 0;
}


//body: writePublicStatsToDB
local void writePublicStatsToDB(Arena *a, int G, int period)
{
	const char *sz = 0;
	char *queryBuffer;
	char subBuffer[1024];
	
	gamestat_period *gs_period = 0;
	Link *link;
	gamestat_type *type;
	dbwrite_preplite *prep;
	unsigned long bufferLen;
	unsigned long usedBufferLen;

	DEF_AD(a);

	if (!db)
	{
		chat->SendArenaMessage(a, " can't write to database because the database interface is missing");
		return;
	}

	MYALOCK;

	gs_period = getPeriod(a, G, period);
	if (!gs_period)
	{
		chat->SendArenaMessage(a, " no stats found from this period.");
		MYAUNLOCK;
		return;
	}

	//now work on stats
	bufferLen = ad->estimatedPlayerSize * gs_period->playerCount;
	queryBuffer = alloca(sizeof(char) * bufferLen);


	sz = cfg->GetStr(a->cfg, "gamestats", "sqltable");
	if (!sz)
	{
		chat->SendArenaMessage(a, " not writing to database: arena not database-stats enabled.");
		MYAUNLOCK;
		return;
	}

	prep = alloca(sizeof(dbwrite_preplite));
	prep->a = a;
	prep->buffer = queryBuffer;
	prep->period = period;
	prep->game = G;
	prep->i = 0;

	sprintf(queryBuffer, "INSERT INTO `%s` (`player_name`", sz);

	FOR_EACH(&ad->stattypeList, type, link)
	{
		CONTINUE_ON_NONSQL(type);

		sprintf(subBuffer, ", `%s`", type->sqlfield);

		strcat(queryBuffer, subBuffer);
	}
	strcat(queryBuffer, ", `Rating`) VALUES");

	HashEnum(&gs_period->playerStats, hashenum_stats_sqlbuffer_lite, prep);
	MYAUNLOCK;

	strcat(queryBuffer, ";");

	usedBufferLen = strlen(queryBuffer) + 1;

	if (usedBufferLen <= bufferLen)
		lm->LogA(L_INFO, "gamestats", a, "used %lu/%lu bytes in stats", usedBufferLen, bufferLen);
	else
		lm->LogA(L_ERROR, "gamestats", a, "BUFFER OVERRUN in writePublicStatsToDB: used %lu/%lu bytes in stats", usedBufferLen, bufferLen);

	db->Query(0, 0, 0, "$", queryBuffer);

	if (!period)
		chat->SendArenaMessage(a, " stats from period %i written to database.", period);
	else
		chat->SendArenaMessage(a, " all stats written to database.");
}

//body: writeLeagueStatsToDB
local void writeLeagueStatsToDB(Arena *a, int G, int period, int leagueSeasonId, int gameId)
{
	const char *sz = 0;
	char *queryBuffer;
	char subBuffer[1024];
	char tempBuffer[512];
	gamestat_period *gs_period = 0;
	gamesummary_item *sumitem;
	Link *link;
	gamestat_type *type;
	dbwrite_prep *prep;
	unsigned long bufferLen;
	unsigned long usedBufferLen;

	DEF_AD(a);

	//Can't write to DB without interface nor without player IDs.
	if (!db || !ad->lg)
	{
		chat->SendArenaMessage(a, " can't write to database because a necessary interface is missing (db:%i lg:%i)", db?1:0, ad->lg?1:0);
		return;
	}

	MYALOCK;

	gs_period = getPeriod(a, G, period);
	if (!gs_period)
	{
		chat->SendArenaMessage(a, " no summary or stats found from this period.");
		MYAUNLOCK;
		return;
	}

	bufferLen = (unsigned long)gs_period->estimatedSummaryQuerySize;

	queryBuffer = amalloc(sizeof(char) * bufferLen);
	MYAUNLOCK;

	sz = cfg->GetStr(a->cfg, "gamestats", "summarytable");
	if (LLGetHead(&gs_period->summaryList) && sz)
	{
		int i = 0;

		sprintf(queryBuffer, "INSERT INTO `%s` (`game_id`, `period`, `time_elapsed`, `summary`) VALUES ", sz);
		MYALOCK;
		FOR_EACH(&gs_period->summaryList, sumitem, link)
		{
			int esc = db->EscapeString(sumitem->text, tempBuffer, sizeof(tempBuffer));
			//strncpy(tempBuffer, sumitem->text, sizeof(tempBuffer));

			if (!esc)
				continue;

			if (gameId)
			{
				if (i)
					sprintf(subBuffer, ", (%i, %i, %i, '%s')", gameId, period, sumitem->time, tempBuffer);
				else
					sprintf(subBuffer, "(%i, %i, %i, '%s')", gameId, period, sumitem->time, tempBuffer);
			}
			else
			{
				if (i)
					sprintf(subBuffer, ", (%s, %i, %i, '%s')", "<GAMEID>", period, sumitem->time, tempBuffer);
				else
					sprintf(subBuffer, "(%s, %i, %i, '%s')", "<GAMEID>", period, sumitem->time, tempBuffer);
			}

			i = 1;
			strcat(queryBuffer, subBuffer);
		}
		MYAUNLOCK;

		strcat(queryBuffer, ";");
		//lm->LogA(L_DRIVEL, "gamestats", a, "-Q=%i", strlen(queryBuffer));

		if (i)
		{
			FILE *fh = fopen("log/gamestats.log", "a");


			usedBufferLen = strlen(queryBuffer) + 1;

			if (usedBufferLen <= bufferLen)
				lm->LogA(L_INFO, "gamestats", a, "used %lu/%lu bytes in stats", usedBufferLen, bufferLen);
			else
				lm->LogA(L_ERROR, "gamestats", a, "BUFFER OVERRUN in writeLeagueStatsToDB: used %lu/%lu bytes in stats", usedBufferLen, bufferLen);

			if (gameId)
			{
				chat->SendArenaMessage(a, " summary from period %i written to database", period);
				db->Query(0, 0, 0, "$", queryBuffer);
			}
			else
			{
				chat->SendArenaMessage(a, " not writing to database: gameID not specified!");
			}

			if (fh)
			{
				if (gameId)
					fprintf(fh, "# GameID:%i  Period:%i\n\n", gameId, period);
				else
					fprintf(fh, "# Game with no gameID!  Period:%i\n\n", period);

				fprintf(fh, "%s", queryBuffer);
				fprintf(fh, "\n");
				fclose(fh);
				chat->SendArenaMessage(a, " summary query from period %i saved to log.", period);
			}
		}
	}

	afree(queryBuffer);

	//now work on stats
	bufferLen = ad->estimatedPlayerSize * gs_period->playerCount;
	queryBuffer = alloca(sizeof(char) * bufferLen);

	MYALOCK;

	sz = cfg->GetStr(a->cfg, "gamestats", "sqltable");
	if (!sz)
	{
		chat->SendArenaMessage(a, " not writing to database: arena not database-stats enabled.");
		MYAUNLOCK;
		return;
	}

	prep = alloca(sizeof(dbwrite_prep));
	prep->a = a;
	prep->gs_period = gs_period;
	prep->gameId = gameId;
	prep->leagueSeasonId = leagueSeasonId;
	prep->buffer = queryBuffer;
	prep->period = period;
	prep->game = G;
	prep->i = 0;

	sprintf(queryBuffer, "INSERT INTO `%s` (`period`, `team_id`, `player_id`, `leagueseason_id`, `game_id`, `shipmask`", sz);

	FOR_EACH(&ad->stattypeList, type, link)
	{
		CONTINUE_ON_NONSQL(type);

		sprintf(subBuffer, ", `%s`", type->sqlfield);

		strcat(queryBuffer, subBuffer);
	}
	strcat(queryBuffer, ", `Rating`) VALUES");

	HashEnum(&gs_period->playerStats, hashenum_stats_sqlbuffer, prep);
	MYAUNLOCK;

	strcat(queryBuffer, ";");


	{
		FILE *fh = fopen("log/gamestats.log", "a");
		if (fh)
		{
			if (gameId)
				fprintf(fh, "# GameID:%i  Period:%i\n\n", gameId, period);
			else
				fprintf(fh, "# Game with no gameID!  Period:%i\n\n", period);

			fprintf(fh, "%s", queryBuffer);
			fprintf(fh, "\n\n\n");
			fclose(fh);
			chat->SendArenaMessage(a, " stats query from period %i saved to log.", period);
		}
	}

	usedBufferLen = strlen(queryBuffer) + 1;

	if (usedBufferLen <= bufferLen)
		lm->LogA(L_INFO, "gamestats", a, "used %lu/%lu bytes in stats", usedBufferLen, bufferLen);
	else
		lm->LogA(L_ERROR, "gamestats", a, "BUFFER OVERRUN in writeLeagueStatsToDB: used %lu/%lu bytes in stats", usedBufferLen, bufferLen);

	if (gameId)
	{
		db->Query(0, 0, 0, "$", queryBuffer);
		chat->SendArenaMessage(a, " stats from period %i written to database.", period);
	}
	else
	{
		chat->SendArenaMessage(a, " not writing to database: gameID not specified!");
	}
}

//body: clearGame
local void clearGame(Arena *a, int gameId)
{
	DEF_AD(a);
	Link *link;
	gamestat_period *I;
	ADV;

	MYALOCK;
	FOR_EACH(&ad->periodList, I, link)
	{
		if (I->gameId == gameId)
		{
			llenum_period_free(I);
			LLRemove(&ad->periodList, I);
		}
	}
	MYAUNLOCK;
}

//body: addSummaryItem
local void addSummaryItem(Arena *a, int gameId, int period, int time, const char *fmt, ...)
{
	va_list args;
	int len;
	char *buf;
	gamestat_period *gs_period;
	

	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args);
	buf = amalloc(sizeof(char) * len + 1);
	vsnprintf(buf, len+1, fmt, args);
	va_end(args);

	gs_period = getPeriod(a, gameId, period);

	if (!gs_period)
	{
		gs_period = addPeriod(a, gameId, period);
	}

	addRawSummaryItem(a, gs_period, time, buf, len + 1);
	afree(buf);
}

//body: addRawSummaryItem
local void addRawSummaryItem(Arena *a, gamestat_period *gsp, int time, const char *buffer, int len)
{
	DEF_AD(a);
	gamesummary_item *newsum = amalloc(sizeof(gamesummary_item) + (sizeof(char) * len));

	newsum->time = time;
	strncpy(newsum->text, buffer, len);
	newsum->text[len - 1] = 0; //allows us to copy non-null terminated strings in without problems.

	MYALOCK;
	gsp->estimatedSummaryQuerySize += 25 + 2*len;
	LLAdd(&gsp->summaryList, newsum);
	MYAUNLOCK;
}


//body: playeraction
local void playeraction(Player *p, int action, Arena *a)
{
	if ((p->type != T_VIE) && (p->type != T_CONT))
		return;

	//actions applying to an arena.
	if (a)
	{
		//DEF_AD(a);

		if (action == PA_ENTERARENA)
		{

		}
		else if (action == PA_LEAVEARENA)
		{

		}
	}
}

//?debug
//body: Cdebug
local void Cdebug(const char *cmd, const char *params, Player *p, const Target *target)
{
	//const char *pre = "debug: gamestats:";
	int checksum;

	if (*params && !strcasecmp(params, "gamestats"))
		return;

	checksum = calculateChecksum(p->arena);

	chat->SendMessage(p, "gamestats:  configurationChecksum =" WDSZ, WDISSECT(checksum));
}

//?statsspam
//body: Cspamstats
local void Cspamstats(const char *cmd, const char *params, Player *p, const Target *target)
{
	int gameId = getIntParam(params, 'g');
	int period = getIntParam(params, 'p');
	Target t;
	t.type = T_PLAYER;
	t.u.p = p;
	spamStatsTable(p->arena, gameId, period, &t);
}

//?loadstats
//body: Cloadstats
local void Cloadstats(const char *cmd, const char *params, Player *p, const Target *target)
{
	int result;

	result = loadStats(p->arena);
	if (result)
		chat->SendArenaMessage(p->arena, " ** %s loads Stats from saved file.", p->name);
	else
		chat->SendMessage(p, "File was not read.");
}

//?savestats
//body: Csavestats
local void Csavestats(const char *cmd, const char *params, Player *p, const Target *target)
{
	int result;

	result = saveStats(p->arena);
	if (result)
		chat->SendMessage(p, "File was written.");
	else
		chat->SendMessage(p, "File was not written.");
}

//?statdesc
//body: Cstatdesc
local void Cstatdesc(const char *cmd, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);
	gamestat_type *type = findStatType(p->arena, params);
	if (!type)
	{
		chat->SendMessage(p, "That stat type does not exist.");
		return;
	}

	chat->SendMessage(p, "Stat: %s", type->name);
	chat->SendMessage(p, "Full: %s", type->fullname);
	if (type->numerator || type->denominator)
	{
		chat->SendMessage(p, "Formula: %.1f * %s / %s", type->factor, type->numerator?type->numerator->name:"[rating]", type->denominator?type->denominator->name:"[rating]");
	}
	else if (type->summary)
	{
		StringBuffer sb;

		int i = 0;
		Link *link;
		gamestat_type *t;

		SBInit(&sb);

		SBPrintf(&sb, "Formula:");

		MYALOCK;
		FOR_EACH(type->summary, t, link)
		{
			if (i++)
				SBPrintf(&sb, " + %s", t->name);
			else
				SBPrintf(&sb, "%s", t->name);
		}
		MYAUNLOCK;
		chat->SendWrappedText(p, SBText(&sb, 0));

		SBDestroy(&sb);
	}
}

//?stats
//body: Cstats
local void Cstats(const char *cmd, const char *params, Player *p, const Target *target)
{
	StringBuffer sb;

	char statstringBuffer[800], temp[64], temp2[64], timebuf[64];
	char targetname[64];
	Player *t;
	Link *link, *link2;
	gamestat_type *type;
	float rating = 0.0f;
	float val;
	int period;
	int sentAnyStats = 0;
	gamestat_period *I;

	DEF_AD(p->arena);
	if (!p->arena)
		return;

	if (target->type == T_PLAYER)
	{
		t = target->u.p;
	}
	else
	{
		if (strcmp(getParam(params, 0, targetname), ""))
		{
			t = pd->FindPlayer(params);
		}
		else
		{
			t = p;
		}
	}

	

	if (t)
	{
		strcpy(targetname, t->name);
	}
	else
	{
		chat->SendMessage(p, "The person you specified is not in the zone.");
		return;
	}

	period = getIntParam(params, 'p');

	MYALOCK;
	FOR_EACH(&ad->periodList, I, link)
	{
		if (I->period != period)
		{
			continue;
		}
		if (!playerHasStats(p->arena, targetname, I->gameId, I->period))
		{
			continue;
		}

		sentAnyStats = 1;

		SBInit(&sb);

		if (period)
		{
			SBPrintf(&sb, "[%s: Game%i Period %i:]", targetname, I->gameId, period);
		}
		else
		{
			SBPrintf(&sb, "[%s: Game%i Totals:]", targetname, I->gameId);
		}

		statstringBuffer[0] = 0;

		
		FOR_EACH(&ad->stattypeList, type, link2)
		{
			val = getStat(p->arena, targetname, type, I->gameId, period, -1);
			if (val != STAT_UNDEFINED)
				rating += (type->ratingValue * val) / 10.0f;

			if (LLMember(&ad->singleStatList, type))
			{
				if (!type->time)
				{
					if (val != STAT_UNDEFINED)
					{
						sprintf(temp2, " %%s:%%.%if", type->floatingPoint);
						sprintf(temp, temp2, type->name, val);
					}
					/*else
					{
						sprintf(temp2, " %%s:%%.%if", type->floatingPoint);
						sprintf(temp, temp2, type->name, 0);
					}*/
					else
					{
						temp[0] = 0;
					}
				}
				else
				{
					if (val != STAT_UNDEFINED)
						sprintf(temp, " %s:%s", type->name, formatTime((int)val, timebuf));
					/*else
						sprintf(temp, " %s:%s", type->name, formatTime(0, timebuf));*/
					else
					{
						temp[0] = 0;
					}
				}

				strcat(statstringBuffer, temp);
			}
		}
		

		SBPrintf(&sb, " RATING:%-.1f", rating);
		SBPrintf(&sb, "%s", statstringBuffer);

		chat->SendWrappedText(p, SBText(&sb, 0));

		SBDestroy(&sb);

	}
	MYAUNLOCK;

	if (!sentAnyStats)
	{
		chat->SendMessage(p, "%s does not have any stats in this arena at this time.", targetname);
	}

}

local void Csummary(const char *cmd, const char *params, Player *p, const Target *target)
{
	Link *link, *link2;
	gamestat_period *P;
	gamesummary_item *D;
	int only = getBoolParam(params, 'g')?getIntParam(params, 'g'):-1;
	char tbuf[64];

	DEF_AD(p->arena);
	ADV;

	MYALOCK;
	FOR_EACH(&ad->periodList, P, link)
	{
		if (!P->period)
			continue;

		if ((only == -1) || (only == P->gameId))
			chat->SendMessage(p, "*** Game %i, Period %i", P->gameId, P->period);
		else
			continue;

		FOR_EACH(&P->summaryList, D, link2)
		{
			chat->SendMessage(p, "%s - %s", formatTime(D->time, tbuf), D->text);
		}
	}
	MYAUNLOCK;
}

local void toggleAutoSaveStats(Arena *a, int on)
{
	DEF_AD(a);
	ADV;
	ad->usePersist = on;
}

local int autoSaveTimerFunc(void *v)
{
	Arena *a = (Arena *)v;
	DEF_AD(a);
	ADZ;

	if (ad->usePersist)
		saveStats(a);
	return 1;
}

local int saveStats(Arena *a)
{
	char fn[64];
	char backupfn[64];
	char buffer[SAVE_BUFFER];
	FILE *fh;
	int len;

	//no chance at buffer overrun, arena names can't get close to even 32 bytes
	sprintf(fn, "stats/%s", a->name);
	sprintf(backupfn, "stats/%s_backup", a->name);

	//erase the old backup file, and move the active stats to the backup
	//there might be some sort of race condition in a crash that will somehow cause everything to be lost..
	//but that's probably 1 in a million and probably not helpable
	unlink(backupfn);
	rename(fn, backupfn);

	fh = fopen(fn, "wb");
	if (!fh)
	{
		lm->LogA(L_ERROR, "gamestats", a, "failed to open '%s' for writing", fn);
		return 0;
	}

	len = create_buffer(a, buffer, SAVE_BUFFER);
	fwrite(&len, sizeof(int), 1, fh);
	fwrite(buffer, len, 1, fh);
	fclose(fh);

	return 1;
}

local int loadBackupStats(Arena *a, char *buffer)
{
	char fn[64];
	FILE *fh;
	int len;
	int res;

	//no chance at buffer overrun, arena names can't get close to even 32 bytes
	sprintf(fn, "stats/%s_backup", a->name);
	fh = fopen(fn, "rb");
	if (!fh)
	{
		lm->LogA(L_ERROR, "gamestats", a, "failed to open '%s' for reading", fn);
		return 0;
	}

	fread(&len, sizeof(int), 1, fh);
	if (len > SAVE_BUFFER || len < 0)
	{
		lm->LogA(L_ERROR, "gamestats", a, "corrupt backup save file? length requested is over limit or less than 0 (%i/%i)", len, SAVE_BUFFER);
		res = 0;
	}
	else
	{
		fread(buffer, len, 1, fh);
		res = read_buffer(a, buffer, len);
	}

	fclose(fh);
	return res;
}

local int loadStats(Arena *a)
{
	char fn[64];
	char buffer[SAVE_BUFFER];
	FILE *fh;
	int len = 0;
	int res;
	int loadBackup = 0;

	//no chance at buffer overrun, arena names can't get close to even 32 bytes
	sprintf(fn, "stats/%s", a->name);

	fh = fopen(fn, "rb");
	if (!fh)
	{
		lm->LogA(L_ERROR, "gamestats", a, "failed to open '%s' for reading", fn);
		loadBackup = 1;
	}
	else
	{
		fread(&len, sizeof(int), 1, fh);
	}

	if (len > SAVE_BUFFER || len < 0)
	{
		lm->LogA(L_ERROR, "gamestats", a, "corrupt save file? length requested is over limit or less than 0 (%i/%i)", len, SAVE_BUFFER);
		loadBackup = 1;
	}

	if (!loadBackup)
	{
		fread(buffer, len, 1, fh);
		res = read_buffer(a, buffer, len);
		if (res == 0)
			loadBackup = 1;
	}

	if (loadBackup)
	{
		res = loadBackupStats(a, buffer);
		if (!res)
		{
			chat->SendArenaMessage(a, "Both the main and backup stats save files were corrupt or unavailable.");
		}
		else
		{
			chat->SendArenaMessage(a, "The main stats save file was corrupt. Loaded save stats from backup file.");
		}
	}

	fclose(fh);

	if (!res)
	{
		//make sure nothing partial was loaded and then abandoned.
		resetArena(a);
	}

	return res;
}

local void clearSaveStats(Arena *a)
{
	char fn[64];
	char backupfn[64];

	//no chance at buffer overrun, arena names can't get close to even 32 bytes
	sprintf(fn, "stats/%s", a->name);
	sprintf(backupfn, "stats/%s_backup", a->name);

	unlink(fn);
	unlink(backupfn);
}

//body: get_data
//body: create_buffer
//local int get_data(Arena *a, void *data, int len, void *v)
local int create_buffer(Arena *a, void *data, int len)
{
	DEF_AD(a);
	gamestat_persist *entry = (gamestat_persist *)data;
	int z = 4;
	int x, i;
	Link *link, *link2;
	char *buffer = (char *)&entry->items;
	gamestat_persist_item *item = (gamestat_persist_item *)&entry->items;
	gamestat_period *IP;
	gamesummary_item *IS;
	ADZ;

	entry->checksum = calculateChecksum(a);

#define SHIFT_BUFFER(s) \
	z += sizeof(*item) + s; \
	buffer += sizeof(*item) + s; \
	item = (gamestat_persist_item *)buffer;
#define CHECK_BUFFER(s) \
	if ((unsigned)(z + sizeof(*item) + s) >= (unsigned)len) \
	{ \
		lm->LogA(L_ERROR, "gamestats", a, "persistant data buffer is not large enough to hold all of the data (requested %lu, need %lu)", (unsigned long)(sizeof(*item) + s), (unsigned long)((z + sizeof(*item) + s) - len)); \
		MYAUNLOCK; \
		return z; \
	}
#define SHIFT_BUFFER_RAW(s) \
	z += s; \
	buffer += s; \
	item = (gamestat_persist_item *)buffer;
#define CHECK_BUFFER_RAW(s) \
	if ((unsigned)(z + s) >= (unsigned)len) \
	{ \
		lm->LogA(L_ERROR, "gamestats", a, "persistant data buffer is not large enough to hold all of the data (requested %lu, need %lu)", (unsigned long)s, (unsigned long)((z + s) - len)); \
		MYAUNLOCK; \
		return z; \
	}

	MYALOCK;
	FOR_EACH(&ad->periodList, IP, link)
	{
		i8 *low = (i8*)&item->param;
		i8 *high = (i8*)(&item->param) + 1;

		//add the period
		CHECK_BUFFER(0);
		item->type = ITEM_NEW_PERIOD;
		*low = IP->period;
		*high = IP->gameId;
		SHIFT_BUFFER(0);

		//add the summary items
		FOR_EACH(&IP->summaryList, IS, link2)
		{
			x = strlen(IS->text) + sizeof(int);
			CHECK_BUFFER(x);
			item->type = ITEM_NEW_SUMMARY;
			item->param = x;
			memcpy(item->buffer, &IS->time, sizeof(int));
			memcpy(item->buffer + sizeof(int), IS->text, x - sizeof(int));
			SHIFT_BUFFER(x);
		}

		//add the player stats
		for (i = 0; i <= IP->playerStats.bucketsm1; i++)
		{
			gamestat_playerinfo *gs_info;
			HashEntry *prev = NULL, *e = IP->playerStats.lists[i];
			while (e)
			{
				shipmask_t shipmask = 0;
				HashEntry *next = e->next;
				gamestat *stat;
				gamestat_type *type;
				LinkedList finishedTeams = LL_INITIALIZER;
				LinkedList *list = (LinkedList *)e->p;
				int currentTeam = -1;
				int *intBuffer;
				int count = LLCount(list);

				x = strlen(e->key);
				CHECK_BUFFER(x);
				item->type = ITEM_NEW_PLAYER;
				item->param = x;
				memcpy(item->buffer, e->key, x);
				SHIFT_BUFFER(x);

				gs_info = HashGetOne(IP->playerInfoTable, e->key);
				if (gs_info)
				{
					shipmask = gs_info->shipmask;
				}
				else
				{
					shipmask = 0;
				}
				CHECK_BUFFER(0);
				item->type = ITEM_PLAYER_INFO;
				item->param = shipmask;
				SHIFT_BUFFER(0);

				while (count > 0)
				{
					//slice through the list finding a stat with a freq that hasn't been done yet
					FOR_EACH(list, stat, link2)
					{
						if (!LLMember(&finishedTeams, (void *)(long)stat->team))
						{
							LLAdd(&finishedTeams, (void *)(long)stat->team);
							CHECK_BUFFER(0);
							item->type = ITEM_NEW_TEAM;
							currentTeam = item->param = stat->team;
							SHIFT_BUFFER(0);
							break;
						}
					}

					FOR_EACH(&ad->stattypeList, type, link2)
					{
						int value;

						value = rawGet(a, list, type, currentTeam);
						if (value != STAT_UNDEFINED_BINARY)
							--count;
						CONTINUE_ON_NONSQL(type);
						CHECK_BUFFER_RAW(sizeof(int));
						intBuffer = (int *)buffer;
						*intBuffer = value;
						SHIFT_BUFFER_RAW(sizeof(int));
					}
				}
				LLEmpty(&finishedTeams);

				prev = e;
				e = next;
			}
		}
	}
	MYAUNLOCK;
	return z;
#undef CHECK_BUFFER
#undef SHIFT_BUFFER
#undef CHECK_BUFFER_RAW
#undef SHIFT_BUFFER_RAW
}

//body: set_data
//body: read_buffer
//local void set_data(Arena *a, void *data, int len, void *v)
local int read_buffer(Arena *a, void *data, int len)
{
	if (len)
	{
		DEF_AD(a);
		Link *link;
		gamestat_persist *entry = (gamestat_persist *)data;
		char *buffer = (char *)&entry->items;
		int x;
		int z = 4;
		int *intBuffer;
		gamestat_persist_item *item = (gamestat_persist_item *)&entry->items;
		gamestat_period *activePeriod = 0;
		LinkedList *activePlayerStats = 0;
		int expectedChecksum = calculateChecksum(a);
		ADZ;

		if (expectedChecksum != entry->checksum)
		{
			lm->LogA(L_ERROR, "gamestats", a, "configuration checksum in persistant data does not match current configuration checksum!");
			return 0;
		}

		resetArena(a);

#define SHIFT_BUFFER(s) \
	z += sizeof(*item) + s; \
	buffer += sizeof(*item) + s; \
	item = (gamestat_persist_item *)buffer;

#define SHIFT_BUFFER_RAW(s) \
	z += s; \
	buffer += s; \
	item = (gamestat_persist_item *)buffer;
#define CHECK(s) \
	if (!s) \
	{ \
		lm->LogA(L_ERROR, "gamestats", a, "corrupt buffer: tried to add data before it had scope (type %i)", item->type); \
		MYAUNLOCK; \
		return 0; \
	}

		MYALOCK;
		while (z < len)
		{
			if (item->type == ITEM_NEW_PERIOD)
			{
				i8 *low = (i8*)&item->param;
				i8 *high = (i8*)(&item->param) + 1;
				activePeriod = addPeriod(a, *high, *low);
				SHIFT_BUFFER(0);
			}
			else if (item->type == ITEM_NEW_SUMMARY)
			{
				CHECK(activePeriod);
				x = item->param;
				intBuffer = (int *)item->buffer;

				addRawSummaryItem(a, activePeriod, *intBuffer, item->buffer + sizeof(int), 1 + x - sizeof(int));
				SHIFT_BUFFER(x);
			}
			else if (item->type == ITEM_NEW_PLAYER)
			{
				char pname[32];
				CHECK(activePeriod);
				x = item->param;
				strncpy(pname, item->buffer, sizeof(pname));
				if (x > 31 || x < 0)
					pname[31] = 0;
				else
					pname[x] = 0;

				activePlayerStats = addPlayerStatList(a, activePeriod, pname);
				SHIFT_BUFFER(x);

				if (item->type != ITEM_PLAYER_INFO)
				{
					lm->LogA(L_ERROR, "gamestats", a, "ITEM_PLAYER_INFO expected directly following ITEM_NEW_PLAYER, but is missing");
					MYAUNLOCK;
					return 0;
				}
				setPlayerShipmask(a, pname, activePeriod->gameId, item->param);
				SHIFT_BUFFER(0);
			}
			else if (item->type == ITEM_NEW_TEAM)
			{
				gamestat_type *type;
				int team = item->param;
				SHIFT_BUFFER(0);
				if (!LLMember(&activePeriod->teams, (void *)(long)team))
					LLAdd(&activePeriod->teams, (void *)(long)team);
				FOR_EACH(&ad->stattypeList, type, link)
				{
					CONTINUE_ON_NONSQL(type);

					if (z > len)
					{
						lm->LogA(L_ERROR, "gamestats", a, "corrupt buffer: unexpected end-of-team-stats");
						MYAUNLOCK;
						return 0;
					}

					intBuffer = (int *)buffer;
					if (*intBuffer != STAT_UNDEFINED_BINARY)
						rawAdd(a, activePlayerStats, type, team, *intBuffer);
					SHIFT_BUFFER_RAW(sizeof(int));
				}
			}
			else
			{
				lm->LogA(L_ERROR, "gamestats", a, "corrupt buffer: invalid operation");
				MYAUNLOCK;
				return 0;
			}
		}
		MYAUNLOCK;
	}

#undef SHIFT_BUFFER
#undef SHIFT_BUFFER_RAW
#undef CHECK
	return 1; //success
}

local void setPlayerShipmask(Arena *a, const char *name, int gameId, shipmask_t mask)
{
	DEF_AD(a);

	gamestat_period *gs_period;
	gamestat_playerinfo *gs_info;

	MYALOCK;
	gs_period = getPeriod(a, gameId, 0);
	if (!gs_period)
	{
		gs_period = addPeriod(a, gameId, 0);
	}

	gs_info = HashGetOne(gs_period->playerInfoTable, name);
	if (!gs_info)
	{
		gs_info = amalloc(sizeof(*gs_info));
		HashAdd(gs_period->playerInfoTable, name, gs_info);
	}
	gs_info->shipmask = mask;
	MYAUNLOCK;
}

local shipmask_t getPlayerShipmask(Arena *a, const char *name, int gameId)
{
	DEF_AD(a);
	shipmask_t result = SHIPMASK_NONE;

	gamestat_period *gs_period;
	gamestat_playerinfo *gs_info;

	MYALOCK;
	gs_period = getPeriod(a, gameId, 0);
	if (gs_period)
	{
		gs_info = HashGetOne(gs_period->playerInfoTable, name);
		if (gs_info)
			result = gs_info->shipmask;
	}
	MYAUNLOCK;

	return result;
}
