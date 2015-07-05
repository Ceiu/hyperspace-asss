
/* dist: public */

#ifndef __STATCODES_H
#define __STATCODES_H

/* note: each of these stats is kept track of over several intervals:
 * forever, per-reset, and per-game. */

enum stat_t
{
	/* pyconst: enum, "STAT_*" */

	/* these four correspond to the standard subspace statistics */
	STAT_KILL_POINTS = 0,
	STAT_FLAG_POINTS,
	STAT_KILLS,
	STAT_DEATHS,

	/* these are extra general purpose statistics */
	STAT_ASSISTS = 100,
	STAT_TEAM_KILLS,
	STAT_TEAM_DEATHS,
	STAT_ARENA_TOTAL_TIME,
	STAT_ARENA_SPEC_TIME, /* not implemented */
	STAT_DAMAGE_TAKEN, /* not implemented */
	STAT_DAMAGE_DEALT, /* not implemented */

	/* these are for flag stats */
	STAT_FLAG_PICKUPS = 200,
	STAT_FLAG_CARRY_TIME,
	STAT_FLAG_DROPS,
	STAT_FLAG_NEUT_DROPS,
	STAT_FLAG_KILLS,
	STAT_FLAG_DEATHS,
	STAT_FLAG_GAMES_WON, /* no per-game */
	STAT_FLAG_GAMES_LOST, /* no per-game */
	STAT_TURF_TAGS,

	/* for powerball */
	STAT_BALL_CARRIES = 300,
	STAT_BALL_CARRY_TIME,
	STAT_BALL_GOALS,
	STAT_BALL_GAMES_WON, /* no per-game */
	STAT_BALL_GAMES_LOST, /* no per-game */

	/* other games */
	STAT_KOTH_GAMES_WON = 400, /* no per-game */

	/* non-standard ball stats (none implemented yet) */
	STAT_BALL_ASSISTS = 500,
	STAT_BALL_STEALS,
	STAT_BALL_DELAYED_STEALS,
	STAT_BALL_TURNOVERS,
	STAT_BALL_DELAYED_TURNOVERS,
	STAT_BALL_SAVES,
	STAT_BALL_CHOKES,
};


/* these are the possible intervals */
enum interval_t
{
	/* pyconst: enum, "INTERVAL_*" */

	/* these are shared between arenas with the same arenagrp */
	INTERVAL_FOREVER = 0,
	INTERVAL_RESET,
	INTERVAL_MAPROTATION,
	/* these are not shared between arenas */
	INTERVAL_GAME = 5,
	INTERVAL_FOREVER_NONSHARED
};
#define MAX_INTERVAL 10
#define INTERVAL_IS_SHARED(iv) ((iv) < 5)


/* this function can be used to get a nice name for a particular stat */
const char *get_stat_name(int st);
const char *get_interval_name(int iv);

#endif

