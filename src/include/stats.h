
/* dist: public */

#ifndef __STATS_H
#define __STATS_H

/* Istats - the statistics/scores manager
 *
 * This module has functions for managing simple scores and statistics.
 */


/* get the stat id codes */
#include "statcodes.h"


#define I_STATS "stats-4"

typedef struct Istats
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*IncrementStat)(Player *p, int stat, int amount);
	/* increments a particular statistic in _all_ intervals */
	/* pyint: player, int, int -> void */

	void (*StartTimer)(Player *p, int stat);
	/* pyint: player, int -> void */
	void (*StopTimer)(Player *p, int stat);
	/* pyint: player, int -> void */
	/* "timer" stats can be managed just like other stats, using
	 * IncrementStat, or you can use these functions, which take care of
	 * tracking the start time and updating the database periodically. */

	void (*SetStat)(Player *p, int stat, int interval, int value);
	/* sets a statistic to a given value */
	/* pyint: player, int, int, int -> void */

	int (*GetStat)(Player *p, int stat, int interval);
	/* gets the value of one statistic */
	/* pyint: player, int, int -> int */

	void (*SendUpdates)(Player *exclude);
	/* sends out score updates for everyone that needs to be updated,
	 * except don't send updates about exclude, or about anyone else to
	 * exclude. */
	/* pyint: player -> void */

	void (*ScoreReset)(Player *p, int interval);
	/* this basically resets all of a player's stats to 0, but doesn't
	 * stop running timers. */
	/* pyint: player, int -> void */
} Istats;

#endif

