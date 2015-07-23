
/* dist: public */

#ifndef __FLAGCORE_H
#define __FLAGCORE_H

/* Iflagcore
 */

enum {
	/* pyconst: enum, "FI_*" */
	FI_NONE,
	FI_CARRIED,
	FI_ONMAP,
};

/* possible settings for Flag:CarryFlags */
enum {
	/* pyconst: enum, "CARRY_*" */
	CARRY_NONE,
	CARRY_ALL,
	CARRY_ONE
};

/* causes of a flag gain */
enum
{
	/* pyconst: enum, "FLAGGAIN_*" */
	FLAGGAIN_PICKUP,        /* picked up a flag from the map */
	FLAGGAIN_KILL,          /* gained a flag by killing a flag carrier */
	FLAGGAIN_OTHER,         /* other reason */
};

/* causes of a flag loss/cleanup */
enum
{
	/* pyconst: enum, "CLEANUP_*" */
	CLEANUP_DROPPED,        /* regular drop */
	CLEANUP_INSAFE,         /* carrier was in safe zone */
	CLEANUP_SHIPCHANGE,     /* ship change */
	CLEANUP_FREQCHANGE,     /* freq change */
	CLEANUP_LEFTARENA,      /* carrier left arena/quit game */
	CLEANUP_KILL_NORMAL,    /* flag got transfered for kill */
	CLEANUP_KILL_TK,        /* carrier was team-killed */
	CLEANUP_KILL_CANTCARRY, /* carrier was killed, but killer can't carry more flags */
	CLEANUP_KILL_FAKE,      /* carrier was killed by a fake player */
	CLEANUP_OTHER,          /* unknown reason */
};

typedef struct FlagInfo
{
	/* pytype: struct, struct FlagInfo, flaginfo */
	int state;
	Player *carrier;
	int x, y;
	int freq;
} FlagInfo;


#define CB_FLAGGAIN "flaggain"
typedef void (*FlagGainFunc)(Arena *a, Player *p, int fid, int how);
/* pycb: arena, player, int, int */
/* run when a player is carrying a new flag. how will be one of the
 * FLAGGAIN_ constants. */

#define CB_FLAGLOST "flaglost"
typedef void (*FlagLostFunc)(Arena *a, Player *p, int fid, int how);
/* pycb: arena, player, int, int */
/* run when a player isn't carrying a flag anymore. how will be one of
 * the CLEANUP_ constants. */

#define CB_FLAGONMAP "flagonmap"
typedef void (*FlagOnMapFunc)(Arena *a, int fid, int x, int y, int freq);
/* pycb: arena, int, int, int, int */
/* run when a flag appears on the map in a new location or with a new owner */

#define CB_FLAGRESET "flagreset"
typedef void (*FlagResetFunc)(Arena *a, int freq, int points);
/* pycb: arena, int, int */
/* run when the flag game is reset (either a win or a forced reset) */


#define I_FLAGCORE "flagcore-4"

typedef struct Iflagcore
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	int (*GetFlags)(Arena *a, int fid, FlagInfo *fis, int count);
	/* pyint: arena, int, flaginfo out, one -> int */
	int (*SetFlags)(Arena *a, int fid, FlagInfo *fis, int count);
	/* pyint: arena, int, flaginfo, one -> int */
	void (*FlagReset)(Arena *a, int freq, int points);
	/* pyint: arena, int, int -> void */

	/* convenience: */
	int (*CountFlags)(Arena *a);
	int (*CountFreqFlags)(Arena *a, int freq);
	int (*CountPlayerFlags)(Player *p);
	int (*IsWinning)(Arena *a, int freq);

	/**
	 * Forces an immediate update of the turf flag statuses. Does nothing if the carry mode is not
	 * CARRY_NONE.
	 *
	 * @param *arena
	 *	A pointer to the arena in which the status update should be sent
	 */
	void (*SendTurfStatus)(Arena *arena);

	/* used during initialization: */

	void (*SetCarryMode)(Arena *a, int carrymode);
	/* pyint: arena, int -> void */
	void (*ReserveFlags)(Arena *a, int flagcount);
	/* pyint: arena, int -> void */
} Iflagcore;


#define I_FLAGGAME "flaggame-2"

typedef struct Iflaggame
{
	INTERFACE_HEAD_DECL
	/* pyint: impl */

	void (*Init)(Arena *a);
	/* pyint: arena -> void */
	/* implementation should call flagcore->SetCarryMode */

	void (*FlagTouch)(Arena *a, Player *p, int fid);
	/* pyint: arena, player, int -> void */
	/* a player touched a flag */

	void (*Cleanup)(Arena *a, int fid, int reason, Player *oldcarrier, int oldfreq);
	/* pyint: arena, int, int, player, int -> void */
	/* a flag needs to be cleaned up for some reason */
} Iflaggame;

#endif

