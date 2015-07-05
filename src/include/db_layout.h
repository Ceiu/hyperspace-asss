
/* dist: public */

#ifndef __DB_LAYOUT_H
#define __DB_LAYOUT_H

/* these names describe where to find the database. */
#define ASSS_DB_HOME "data"
#define ASSS_DB_FILENAME "data.db"

/* these are alternate values of the 'arena' field in the keys below. */
#define AG_PUBLIC "(public)"
#define AG_GLOBAL "(global)"

/* the length of an arena or arena group name. don't change this. */
#define MAXAGLEN 16

/* TODO: turn this on. this will invalidate all stored data, so do it as
 * part of a major releaes. */
/* #define USE_RECTYPES */

/* there are 4 types of records in the database:
 *
 * player records hold data about a player, either local to one arena,
 * or global to the zone. (e.g. one player's scores in "turf", or a
 * player's chat mask in league)
 *
 * arena records hold data about a single arena. no intervals are shared
 * for arena records. (e.g. base win stats in pub 0 for this reset, game
 * start times in "duel")
 *
 * current serial records hold the current serial number for an interval
 * in an arena group. (e.g. arena "aswz" is up to reset 17, "smallpb" is
 * up to game 467)
 *
 * generic records are used for other modules to keep pretty much
 * anything they want.
 */


/* this is the key for a single player record. */
#define ASSS_DB_PLAYER_RECTYPE 0xfeefee01
struct player_record_key
{
	char name[24];
	char arenagrp[MAXAGLEN];
	short interval;
	unsigned int serialno;
	int key;
#ifdef USE_RECTYPES
	u32 rectype;
#endif
};

/* the arenagrp field requires a bit of explanation: it will be either
 * "<public>" for public arenas, "<global>" for global data, or a
 * literal arena name. */

/* the value associated with one of these record keys is merely binary
 * data whose format is determined by the module storing it. */


/* this is the key for arenagrp data. */
#define ASSS_DB_ARENA_RECTYPE 0xfeefee02
struct arena_record_key
{
	char arena[MAXAGLEN];
	short interval;
	unsigned int serialno;
	int key;
#ifdef USE_RECTYPES
	u32 rectype;
#endif
};

/* the value associated with arena records is opaque binary data. */


/* this is the key used to store the current serial numbers for various
 * intervals per arena. */
#define ASSS_DB_SERIAL_RECTYPE 0xfeefee03
struct current_serial_record_key
{
	char arenagrp[MAXAGLEN];
	short interval;
#ifdef USE_RECTYPES
	u32 rectype;
#endif
};

/* the value for one of these is a 4 byte unsigned integer. */


/* this is the suffix of the key for generic data. we specify the suffix
 * so that we can get locality among generic records and player/arena
 * records if we want. */
#define ASSS_DB_GENERIC_RECTYPE 0xfeefee04
struct generic_record_key_suffix
{
	int key;
	u32 rectype;
};

/* the value associated with generic records is whatever the user wants. */

#endif

