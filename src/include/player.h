
/* dist: public */

#ifndef __PLAYER_H
#define __PLAYER_H

/** @file
 * this file contains definitions to do with players.
 * the playerdata module is pretty important. it's used to manage locks
 * on the player list and on individual players. it also contains
 * utility functions for going from pids, names, and Target s to
 * players.
 */

/** client types */
enum
{
	/* pyconst: enum, "T_*" */

	T_UNKNOWN,
	/* this probably won't be used */

	T_FAKE,
	/* no client, internal to server */

	T_VIE,
	/* original vie client */

	T_CONT,
	/* continuum client */

	T_CHAT,
	/* simple chat client */
};

/* macros for testing types */

/** is this player using a regular game client? */
#define IS_STANDARD(p) ((p)->type == T_CONT || (p)->type == T_VIE)
/** is this player using a tcp-based chat client? */
#define IS_CHAT(p) ((p)->type == T_CHAT)
/** is this player a human (as opposed to an internally controlled fake
 * player)? */
#define IS_HUMAN(p) (IS_STANDARD(p) || IS_CHAT(p))


/** player status codes */
enum
{
	/* pyconst: enum, "S_*" */

	/** player was just created, and isn't ready to do anything yet */
	/* transitions to: connected */
	S_UNINITIALIZED,

	/** player is connected (key exchange completed) but has not logged
	 ** in yet */
	/* transitions to: need_auth or leaving_zone */
	S_CONNECTED,

	/** player sent login, auth request will be sent */
	/* transitions to: wait_auth */
	S_NEED_AUTH,

	/** waiting for auth response */
	/* transitions to: connected or need_global_sync */
	S_WAIT_AUTH,

	/** auth done, will request global sync */
	/* transitions to: wait_global_sync1 */
	S_NEED_GLOBAL_SYNC,

	/** waiting for sync global persistent data to complete */
	/* transitions to: do_global_callbacks */
	S_WAIT_GLOBAL_SYNC1,

	/** global sync done, will call global player connecting callbacks */
	/* transitions to: send_login_response */
	S_DO_GLOBAL_CALLBACKS,

	/** callbacks done, will send arena response */
	/* transitions to: loggedin */
	S_SEND_LOGIN_RESPONSE,

	/** player is finished logging in but is not in an arena yet status
	 ** returns here after leaving an arena, also */
	/* transitions to: do_freq_and_arena_sync or leaving_zone */
	S_LOGGEDIN,

/* p->arena is valid starting here */

	/** player has requested entering an arena, needs to be assigned a
	 ** freq and have arena data syched */
	/* transitions to: wait_arena_sync1 (or loggedin) */
	S_DO_FREQ_AND_ARENA_SYNC,

	/** waiting for scores sync */
	/* transitions to: send_arena_response (or do_arena_sync2) */
	S_WAIT_ARENA_SYNC1,

	/** done with scores, needs to send arena response and run arena
	 ** entering callbacks */
	/* transitions to: playing (or do_arena_sync2) */
	S_ARENA_RESP_AND_CBS,

	/** player is playing in an arena. typically the longest state */
	/* transitions to: leaving_arena */
	S_PLAYING,

	/** player has left arena, callbacks need to be called */
	/* transitions to: do_arena_sync2 */
	S_LEAVING_ARENA,

	/** need to sync in the other direction */
	/* transitions to: wait_arena_sync2 */
	S_DO_ARENA_SYNC2,

	/** waiting for scores sync, other direction */
	/* transitions to: loggedin */
	S_WAIT_ARENA_SYNC2,

/* p->arena is no longer valid after this point */

	/** player is leaving zone, call disconnecting callbacks and start
	 ** global sync */
	/* transitions to: wait_global_sync2 */
	S_LEAVING_ZONE,

	/** waiting for global sync, other direction */
	/* transitions to: timewait */
	S_WAIT_GLOBAL_SYNC2,

	/** the connection is all set to be ended. the network layer will
	 ** free the player after this. */
	/* transitions to: (none) */
	S_TIMEWAIT
};


#include "packets/pdata.h"
#include "packets/ppk.h"
#include "packets/login.h"

struct Arena;

/** this encapsulates a bunch of the typical position information about
 ** players in standard clients.
 */
struct PlayerPosition
{
	int x;           /**< x coordinate of current position in pixels */
	int y;           /**< y coordinate of current position in pixels */
	int xspeed;      /**< velocity in positive x direction (pixels/second) */
	int yspeed;      /**< velocity in positive y direction (pixels/second) */
	int rotation;    /**< rotation value (0-63) */
	unsigned bounty; /**< current bounty */
	unsigned status; /**< status bitfield */
	int energy;      /**< current energy */
	ticks_t time;    /**< time of last position packet */
};

/* pyconst: define int, "STATUS_*" */
/** whether stealth is on */
#define STATUS_STEALTH  0x01U
/** whether cloak is on */
#define STATUS_CLOAK    0x02U
/** whether xradar is on */
#define STATUS_XRADAR   0x04U
/** whether antiwarp is on */
#define STATUS_ANTIWARP 0x08U
/** whether to display the flashing image for a few frames */
#define STATUS_FLASH    0x10U
/** whether the player is in a safezone */
#define STATUS_SAFEZONE 0x20U
/** whether the player is a ufo */
#define STATUS_UFO      0x40U
/** whether or not the player is idle (not rotating or changing velocity) */
#define STATUS_IDLE     0x80U


/** this struct holds everything we know about a player */
struct Player
{
	/** this is the packet that gets sent to clients. some info is kept
	 ** in here */
	PlayerData pkt;

	/** make pkt.ship look like a regular field */
#define p_ship pkt.ship
	/** make pkt.freq look like a regular field */
#define p_freq pkt.freq
	/** make pkt.attachedto look like a regular field */
#define p_attached pkt.attachedto

	/** the player id (equal to pkt.pid) */
	int pid;
	/** the client type (see type enum above) */
	int type;
	/** the state code (see status code enum above) */
	int status;
	/** which state to move to after returning to S_LOGGEDIN */
	int whenloggedin;
	/** the player's current arena, or NULL if not in an arena yet */
	Arena *arena;
	/** the arena the player is trying to enter */
	Arena *newarena;
	/** the player's name (nul terminated) */
	char name[24];
	/** the player's squad (nul terminated) */
	char squad[24];
	/** screen resolution, for standard clients */
	i16 xres, yres;
	/** the time (in ticks) that this player first connected */
	ticks_t connecttime;
	/** contains some recent information about the player's position */
	struct PlayerPosition position;
	/** the player's machine id, for standard clients. */
	u32 macid, permid;
	/** a text representation of the ip address the player is connecting
	 ** from. */
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif
	char ipaddr[INET_ADDRSTRLEN];
	/** if the player has connected through a port that sets a default
	 ** arena, that will be stored here. */
	const char *connectas;
	/** a text representation of the client connecting */
	char clientname[32];
	/* misc data about the player */
	/* the server recorded time of the last death */
	ticks_t last_death;
	/* when the server expects this player to respawn, is last_death+Kill:EnterDelay */
	ticks_t next_respawn;
	/** some extra flags that don't have a better place to go */
	struct
	{
		/** if the player has been authenticated by either a billing
		 ** server or a password file */
		u32 authenticated : 1;
		/** set when the player has changed freqs or ships, but before he
		 ** has acknowleged it */
		u32 during_change : 1;
		/** if player wants optional .lvz files */
		u32 want_all_lvz : 1;
		/** if player is waiting for db query results */
		u32 during_query : 1;
		/** if the player's lag is too high to let him be in a ship */
		u32 no_ship : 1;
		/** if the player's lag is too high to let him have flags or
		 ** balls */
		u32 no_flags_balls : 1;
		/** if the player has sent a position packet since entering the
		 ** arena */
		u32 sent_ppk : 1;
		/** if the player has sent a position packet with a weapon since
		 ** this flag was reset */
		u32 sent_wpn : 1;
		/** if the player is a bot who wants all position packets */
		u32 see_all_posn : 1;
		/** if the player is a bot who wants his own position packets */
		u32 see_own_posn : 1;
		/** if the player needs to transition to a leaving arena state
		 ** while wainting for the database to return */
		u32 leave_arena_when_done_waiting : 1;
		/** if the player's obscenity filter is on */
		u32 obscenity_filter : 1;
		/** if the player has died but not yet respawned */
		u32 is_dead : 1;
		/** fill this up to 32 bits */
		u32 padding : 19;
	} flags;

	/** space for private data associated with this player */
	byte playerextradata[0];
};


/** this callback is called whenever a Player struct is allocated or
 ** deallocated. in general you probably want to use CB_PLAYERACTION
 ** instead of this callback for general initialization tasks. */
#define CB_NEWPLAYER "newplayer"
/** the type of CB_NEWPLAYER
 * @param p the player struct being allocated/deallocated
 * @param isnew true if being allocated, false if being deallocated
 */
typedef void (*NewPlayerFunc)(Player *p, int isnew);


/** the interface id for playerdata */
#define I_PLAYERDATA "playerdata-8"

/** the playerdata interface struct */
typedef struct Iplayerdata
{
	INTERFACE_HEAD_DECL
	/* pyint: use  */

	/** Creates a new player.
	 * This is called by the network modules when they get a new
	 * connection.
	 * @param type the type of player to create
	 * @return the newly allocated player struct
	 */
	Player * (*NewPlayer)(int type);

	/** Frees memory associated with a player.
	 * This is called by the network modules when a connection has
	 * terminated.
	 * @param p the player to free
	 */
	void (*FreePlayer)(Player *p);

	/** Disconnects a player from the server.
	 * This does most of the work of disconnecting a player. The
	 * player's state will be transitioned to S_TIMEWAIT, at which point
	 * one of the network modules must take responsibility for final
	 * cleanup.
	 * @param p the player to kick
	 */
	void (*KickPlayer)(Player *p);
	/* pyint: player -> void */


	/** Finds the player with the given pid.
	 * @param pid the pid to find
	 * @return the player with the given pid, or NULL if not found
	 */
	Player * (*PidToPlayer)(int pid);
	/* pyint: int -> player */

	/** Finds the player with the given name.
	 * The name is matched case-insensitively.
	 * @param name the name to match
	 * @return the player with the given name, or NULL if not found
	 */
	Player * (*FindPlayer)(const char *name);
	/* pyint: string -> player */

	/** Converts a Target to a specific list of players.
	 * The players represented by the target will be added to the given
	 * list. Don't forget to free the list elements when you're done
	 * with them.
	 * @param target the target to convert
	 * @param set the list to add players to
	 */
	void (*TargetToSet)(const Target *target, LinkedList *set);


	/** Allocates space in the player struct for private data.
	 * The player struct contains a dynamically sized chunk of memory
	 * after the members defined in this header file. Modules can
	 * allocate pieces of memory in that chunk to use for keeping track
	 * of data that must be kept per-player. Space is allocated with
	 * this function and freed with Iplayerdata::FreePlayerData. The
	 * space is represented by a key, which is actually just an offset
	 * from the start of playerextradata.
	 *
	 * This is generally called by modules when they load, although it's
	 * sometimes used at other times as well.
	 *
	 * @see PPDATA
	 * @param bytes how many bytes to allocate
	 * @return a key to be used in PPDATA, or -1 on failure
	 */
	int (*AllocatePlayerData)(size_t bytes);

	/** Frees a chunk of per-player memory.
	 * This is generally called during module unloading.
	 * @param key the key that was obtained from
	 * Iplayerdata::AllocatePlayerData
	 */
	void (*FreePlayerData)(int key);

	/** Locks the global player lock (shared/read-only).
	 * There is one global player lock which protects the list of
	 * players and the status values. Before using FOR_EACH_PLAYER or
	 * FOR_EACH_PLAYER_P, you need to call this function, and
	 * you need to call Unlock when you're done.
	 */
	void (*Lock)(void);
	/** Locks the global player lock (exclusive/read-write).
	 * Before modifying player status values, you should use this to
	 * acquire an exclusive lock on the global player lock.
	 */
	void (*WriteLock)(void);
	/** Unlocks the global player lock (shared/read-only).
	 * Use this whenever you used Iplayerdata::Lock.
	 */
	void (*Unlock)(void);
	/** Unlocks the global player lock (exclusive/read-write).
	 * Use this whenever you used Iplayerdata::WriteLock.
	 */
	void (*WriteUnlock)(void);

	/** This list contains all the players the server knows about.
	 * Don't forget the necessary locking. You don't want to use this
	 * directly, but should use these macros instead:
	 * @see FOR_EACH_PLAYER
	 * @see FOR_EACH_PLAYER_P
	 */
	LinkedList playerlist;
} Iplayerdata;


/** use this to access per-player data.
 * @see Iplayerdata::AllocatePlayerData
 * @param p the player to access data in
 * @param mykey a per-player data key obtained from
 * Iplayerdata::AllocatePlayerData
 * @return a pointer to the per-player data for p specified by mykey.
 * the return type is void *, so it can be assigned to any pointer
 * without casting.
 */
#define PPDATA(p, mykey) ((void*)((p)->playerextradata+mykey))

/** This is the basic iterating over players macro.
 * It requires a Link * named "link" and an Iplayerdata * named "pd" in
 * the current scope. The body of the loop will be run with the provided
 * Player * set to each player in turn. You need to call pd->Lock first.
 * @see Iplayerdata::Lock
 * @param p the Player * which will hold successive players
 */
#define FOR_EACH_PLAYER(p) \
	for ( \
			link = LLGetHead(&pd->playerlist); \
			link && ((p = link->data, link = link->next) || 1); )

/** This is similar to FOR_EACH_PLAYER, but only looks at players in the
 * Arena * a. This macro has all the same needs as FOR_EACH_PLAYER,
 * namely that it requires a Link * named "link" and an Iplayerdata *
 * named "pd" in the current scope. Again, you need to call pd->Lock
 * first.
 * @see Iplayerdata::Lock
 * @param p the Player * which will hold successive players
 * @param a the Arena * within which to find players
 */
#define FOR_EACH_PLAYER_IN_ARENA(p, a) FOR_EACH_PLAYER(p) if (p->arena == a)

/** This is a slightly fancier iterating over players macro.
 * It requires a Link * named "link" and an Iplayerdata * named "pd" in
 * the current scope. The body of the loop will be run with the provided
 * Player * set to each player in turn, and also will set d to the value
 * of PPDATA(p, key). Again, you need to call pd->Lock first.
 * @see Iplayerdata::Lock
 * @see PPDATA
 * @param p the Player * which will hold successive players
 * @param d the poniter which will point to the private space of the
 * current player.
 * @param key the per-player data key obtained from
 * Iplayerdata::AllocatePlayerData.
 */
#define FOR_EACH_PLAYER_P(p, d, key) \
	for ( \
			link = LLGetHead(&pd->playerlist); \
			link && ((p = link->data, \
			          d = PPDATA(p, key), \
			          link = link->next) || 1); )

#endif

