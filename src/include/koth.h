
/* dist: public */

#ifndef __KOTH_H
#define __KOTH_H


/* causes of a crown gain or loss */
enum
{
	/* pyconst: enum, "KOTH_CAUSE_*" */
	KOTH_CAUSE_GAME_START,       /* a new game is starting and p is in a ship */
	KOTH_CAUSE_LEAVE_GAME,       /* p left the game/arena or changed ships */
	KOTH_CAUSE_TOO_MANY_DEATHS,  /* p died too many times */
	KOTH_CAUSE_RECOVERED,        /* p killed enough players with crowns to recover */
	KOTH_CAUSE_EXPIRED,          /* p's crown timer expired */
};

/** called whenever a player gains or loses a crown in the koth game. */
#define CB_CROWNCHANGE "crownchange-1"
/** the type of CB_CROWNCHANGE.
 * @param p the player gaining or losing a crown
 * @param gain true if gaining, false if losing
 * @param cause the cause of the gain or loss
 */
typedef void (*CrownChangeFunc)(Player *p, int gain, int cause);
/* pycb: player, int, int */


/** called when a koth game begins. */
#define CB_KOTH_START "kothstart-1"
typedef void (*KothStartFunc)(Arena *a, int initial_crowns);
/* pycb: arena, int */


/** called when a koth game with a win (before CB_KOTH_PLAYER_WIN). */
#define CB_KOTH_END "kothend-1"
typedef void (*KothEndFunc)(Arena *a, int playing, int winners, int points_per_winner);
/* pycb: arena, int, int, int */

/** called once for each player that wins points in a koth game. */
#define CB_KOTH_PLAYER_WIN "kothplayerwin-1"
typedef void (*KothPlayerWinFunc)(Arena *a, Player *p, int points);
/* pycb: arena, player, int */

/** called after the last CB_KOTH_PLAYER_WIN for a particular game end. */
#define CB_KOTH_PLAYER_WIN_END "kothplayerwinend-1"
typedef void (*KothPlayerWinEndFunc)(Arena *a);
/* pycb: arena */


#define I_POINTS_KOTH "points-koth-2"

typedef struct Ipoints_koth
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */
	int (*GetPoints)(
			Arena *arena,
			int totalplaying,
			int winners);
	/* this will be called by the koth module when some group of people
	 * wins a round of koth. it should return the number of points to be
	 * given to each player. */
	/* pyint: arena, int, int -> int */
} Ipoints_koth;

#endif

