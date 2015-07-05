
/* dist: public */

#ifndef __ARENAMAN_H
#define __ARENAMAN_H

/** @file
 * this file contains definitions and interfaces for the arenaman
 * module, which you'll have to use to do things involving arenas.
 */

#include "config.h"
#include "db_layout.h"


/** this structure represents one arena */
struct Arena
{
	/** what state the arena is in. @see ARENA_DO_INIT, etc. */
	int status;
	/** the full name of the arena */
	char name[20];
	/** the name of the arena, minus any trailing digits.
	 * the basename is used in many places to allow easy sharing of
	 * settings and things among copies of one basic arena. */
	char basename[20];
	/** a handle to the main config file for this arena */
	ConfigHandle cfg;
	/** the frequency for spectators in this arena.
	 * this setting is so commonly used, it deserves a spot here. */
	int specfreq;
	/** how many players are in ships in this arena.
	 * call GetPopulationSummary to update this. */
	int playing;
	/** how many players total are in this arena.
	 * call GetPopulationSummary to update this. */
	int total;
	/** whether this arena should not be destroyed when
	 * there are no players inside it. */
	int keep_alive : 1;

	int _reserved : 31;

	/** space for private data associated with this arena */
	byte arenaextradata[0];
};


enum
{
	/* pyconst: enum, "AA_*" */
	/** when arena is created */
	AA_CREATE,
	/** when config file changes */
	AA_CONFCHANGED,
	/** when the arena is destroyed */
	AA_DESTROY,
	/** really really early */
	AA_PRECREATE,
	/** really really late */
	AA_POSTDESTROY
};


/** this callback is called when an arena is created or destroyed, or
 ** some configuration value for the arena changes. */
#define CB_ARENAACTION "arenaaction"
/** the type of CB_ARENAACTION callbacks.
 * @param a the arena that something is happening to
 * @param action what is happening. @see AA_CREATE, etc. */
typedef void (*ArenaActionFunc)(Arena *arena, int action);
/* pycb: arena, int */

/** arena states */
enum
{
	/** someone wants to enter the arena. first, the config file must be
	 ** loaded and callbacks called. */
	ARENA_DO_INIT0,

	/** waiting for first round of callbacks */
	ARENA_WAIT_HOLDS0,

	/** attaching and more callbacks */
	ARENA_DO_INIT1,

	/** waiting on modules to do init work. */
	ARENA_WAIT_HOLDS1,

	/** load persistent data. */
	ARENA_DO_INIT2,

	/** waiting on the database or callbacks. */
	ARENA_WAIT_SYNC1,

	/** now the arena is fully created. core can now send the arena
	 ** responses to players waiting to enter this arena. */
	ARENA_RUNNING,

	/** the arena is running for a little while, but isn't accepting new
	 ** players. */
	ARENA_CLOSING,

	/** the arena is being reaped, first put info in database */
	ARENA_DO_WRITE_DATA,

	/** waiting on the database to finish before we can unregister
	 ** modules. */
	ARENA_WAIT_SYNC2,

	/** arena destroy callbacks. */
	ARENA_DO_DESTROY1,

	/** waiting for modules to do destroy work. */
	ARENA_WAIT_HOLDS2,

	/** finish destroy process. */
	ARENA_DO_DESTROY2
};


/** the interface id for arenaman */
#define I_ARENAMAN "arenaman-9"

/** the arenaman interface struct */
typedef struct Iarenaman
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Tells the player that he's entering an arena.
	 * This should only be called at the appropriate time from the core
	 * module. */
	void (*SendArenaResponse)(Player *p);
	/** Tells the player that he's leaving an arena.
	 * This should only be called at the appropriate time from the core
	 * module. */
	void (*LeaveArena)(Player *p);

	/** Recycles an arena by suspending all the players, unloading and
	 ** reloading the arena, and then letting the players back in. */
	int (*RecycleArena)(Arena *a);
	/* pyint: arena -> int */

	/** Moves a player into a specific arena.
	 * Works on Continuum clients only.
	 * @param p the player to move
	 * @param aname the arena to send him to
	 * @param spawnx the x coord he should spawn at, or 0 for default
	 * @param spawny the y coord he should spawn at, or 0 for default
	 */
	void (*SendToArena)(Player *p, const char *aname, int spawnx, int spawny);
	/* pyint: player, string, int, int -> void */

	/** This is a multi-purpose function for locating and counting
	 ** arenas.
	 * Given a name, it returns either an arena (if some arena by that
	 * name is running) or NULL (if not). If it's running, it also fills
	 * in the next two params with the number of players in the arena
	 * and the number of non-spec players in the arena. */
	Arena * (*FindArena)(const char *name, int *totalcount, int *playing);
	/* pyint: string, int out, int out -> arena */

	/** This counts the number of players in the server and in each
	 ** arena.
	 * It fills in its two parameters with population values for the
	 * whole server, and also fills in the total and playing fields of
	 * each Arena structure. You should be holding the arena lock when
	 * calling this. */
	void (*GetPopulationSummary)(int *total, int *playing);
	/* pyint: int out, int out -> void */

	/** Allocates space in the arena struct for per-arena data.
	 * @see Iplayerdata::AllocatePlayerData
	 * @see P_ARENA_DATA
	 * @param bytes how much space to allocate
	 * @return a key to be used in P_ARENA_DATA, or -1 on failure
	 */
	int (*AllocateArenaData)(size_t bytes);
	/** Frees per-arena space.
	 * @param key a key obtained from Iarenaman::AllocateArenaData.
	 */
	void (*FreeArenaData)(int key);

	/** Locks the global arena lock.
	 * There is a lock protecting the arena list, which you need to hold
	 * whenever you use FOR_EACH_ARENA or otherwise access
	 * Iarenaman::arenalist. Call this before you start, and Unlock when
	 * you're done.
	 */
	void (*Lock)(void);
	/* pyint: void -> void */

	/** Unlocks the global arena lock.
	 * Use this whenever you used Iarenaman::Lock.
	 */
	void (*Unlock)(void);
	/* pyint: void -> void */

	/** Puts a "hold" on an arena, preventing it from proceeding to the
	 ** next stage in initialization until the hold is removed.
	 * This can be used to do some time-consuming work during arena
	 * creation asynchronously, e.g. in another thread. It may only be
	 * used in CB_ARENAACTION callbacks, only for AA_PRECREATE,
	 * AA_CREATE and AA_DESTROY actions.
	 */
	void (*Hold)(Arena *a);
	/* pyint: arena -> void */
	/** Removes a "hold" on an arena.
	 * This must be called exactly once for each time Hold is called. It
	 * may be called from any thread.
	 */
	void (*Unhold)(Arena *a);
	/* pyint: arena -> void */

	/** This is a list of all the arenas the server knows about.
	 * Don't forget the lock. You shouldn't use this directly, but use
	 * these macros instead:
	 * @see FOR_EACH_ARENA
	 * @see FOR_EACH_ARENA_P
	 */
	LinkedList arenalist;
} Iarenaman;


/** This will tell you if an arena is considered a "public" arena. */
#define ARENA_IS_PUBLIC(a) (strcmp((a)->basename, AG_PUBLIC) == 0)

/** Use this to access per-arena data.
 * @param a an arena pointer
 * @param mykey a key to per-arena data obtained from
 * Iarenaman::AllocateArenaData.
 */
#define P_ARENA_DATA(a, mykey) ((void*)((a)->arenaextradata+mykey))

/** Iterate through all the arenas in the server.
 * You'll need a Link * named "link" and an Iarenaman * named "aman" in
 * the current scope. Don't forget the necessary locking.
 * @param a the variable that will hold each successive arena
 * @see Iarenaman::Lock
 */
#define FOR_EACH_ARENA(a) \
	for ( \
			link = LLGetHead(&aman->arenalist); \
			link && ((a = link->data, link = link->next) || 1); )

/** Iterate through all the arenas in the server, with an extra pointer
 ** to per-arena data.
 * Just like FOR_EACH_ARENA, except another variable will point to the
 * result of P_ARENA_DATA(a, key) each time.
 * @param a the variable that will hold each successive arena
 * @param d the variable that will point to the per-arena data
 * @param key a key obtained from Iarenaman::AllocateArenaData
 * @see Iarenaman::Lock
 * @see Iarenaman::AllocateArenaData
 */
#define FOR_EACH_ARENA_P(a, d, key) \
	for ( \
			link = LLGetHead(&aman->arenalist); \
			link && ((a = link->data, \
			          d = P_ARENA_DATA(a, key), \
			          link = link->next) || 1); )


/** the interface id for arenaplace */
#define I_ARENAPLACE "arenaplace-2"

/** the arenaplace interface struct.
 * You should register an interface of this type if you want to control
 * which arena players get placed in when they connect without a
 * preference.
 */
typedef struct Iarenaplace
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */

	/** Place a player in an arena.
	 * This will be called when a player requests to join an arena
	 * without indicating any preference. To place the player, you
	 * should copy an arena name into the buffer pointed to by name, of
	 * size namelen bytes, and return true. Optionally, put some spawn
	 * coordinates at the locations pointed to by spawnx and spawny.
	 * @param name a buffer to place the arena name in
	 * @param namelen the length of the buffer
	 * @param spawnx a pointer to a place to put spawn coordinates, if desired
	 * @param spawny a pointer to a place to put spawn coordinates, if desired
	 * @param p the player being placed
	 * @return true if name was filled in, false on any error
	 */
	int (*Place)(char *name, int namelen, int *spawnx, int *spawny, Player *p);
	/* pyint: string out, int buflen, int out, int out, player -> int */
} Iarenaplace;


#endif

