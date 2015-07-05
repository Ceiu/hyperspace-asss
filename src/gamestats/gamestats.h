#ifndef GAMESTATS_H
#define GAMESTATS_H

#include "clocks.h"

#define STAT_UNDEFINED -1000.0f
#define STAT_UNDEFINED_BINARY -1073741824

typedef void (*statsenumfunc)(Arena *a, const char *name, LinkedList *, void *clos);

typedef struct gamestat_type
{
	LinkedList *summary;
	struct gamestat_type *numerator;
	struct gamestat_type *denominator;

	char *name;
	char *fullname;
	char *sqlfield;

	float factor;
	int ratingValue;

	short denominatorminimum;
	short id;

	unsigned int textWidth : 4;
	unsigned int floatingPoint : 4;

	int time : 1;
	int sql : 1;

	int showbest : 2;
	int showtotal : 1;

	int ratiostat : 1;

} gamestat_type;

typedef struct gamestat
{
	gamestat_type *type;
	Clock *clock;
	int value;
	short partial;

	int team;
} gamestat;

typedef struct gamestat_period
{
	int gameId;
	int period;

	int estimatedSummaryQuerySize;
	int playerCount;
	LinkedList teams;
	LinkedList summaryList;
	
	HashTable *playerInfoTable; //this information needs to be shared between periods in the same game
	HashTable playerStats;
} gamestat_period;

typedef struct gamesummary_item
{
	int time;
	char text[0];
} gamesummary_item;

typedef struct gamestat_ticker
{
	gamestat_type *type;
	Player *p;
	Clock *clock;
} gamestat_ticker;

#define GAMESTATS_VER 2.3

#define I_GAMESTATS "gamestats2.3.1"

typedef struct Igamestats
{
	INTERFACE_HEAD_DECL
	gamestat_type *(*getStatType)(Arena *, const char *name);
	float (*getStat)(Arena *, const char *name, gamestat_type *, int gameId, int period, int team);
	void (*AddStat)(Arena *, Player *, gamestat_type *, int gameId, int period, int team, int amt);

	void (*StartStatTicker)(Arena *, Player *, struct gamestat_type *, int gameId, int period, int team, ticks_t ct);
	void (*StopStatTicker)(Arena *, Player *, struct gamestat_type *, ticks_t ct);

	float (*getPlayerRating)(Arena *, const char *name, int gameId, int period, int team);

	void (*SpamStatsTable)(Arena *, int gameId, int period, const Target *);
	void (*WritePublicStatsToDB)(Arena *, int game, int period);
	void (*WriteLeagueStatsToDB)(Arena *, int game, int period, int leagueSeasonId, int gameId);

	void (*ClearGame)(Arena *a, int gameId);

	void (*AddSummaryItem)(Arena *, int gameId, int period, int time, const char *fmt, ...);
	void (*AddRawSummaryItem)(Arena *, gamestat_period *, int time, const char *buffer, int len);

	void (*StatsEnum)(Arena *, int gameId, int period, statsenumfunc, void *clos);
	float (*getStatTotal)(Arena *, struct gamestat_type *, int gameId, int period, int team);

	void (*RawAdd)(Arena *, LinkedList *, gamestat_type *, int team, int amt);
	int (*rawGet)(Arena *, LinkedList *, gamestat_type *, int team);
	float (*rawGetf)(Arena *, LinkedList *, gamestat_type *, int team);

	void (*ToggleAutoSave)(Arena *, int on);
	int (*SaveStats)(Arena *);
	void (*ClearSaveStats)(Arena *);

	void (*SetPlayerShipmask)(Arena *, const char *name, int gameId, shipmask_t mask);
	shipmask_t (*getPlayerShipmask)(Arena *, const char *name, int gameId);
} Igamestats;

#endif
