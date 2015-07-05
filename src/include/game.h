
/* dist: public */

#ifndef __GAME_H
#define __GAME_H

#include "mapdata.h"

/** @file
 * various functions and interfaces related to the game. important stuff
 * managed by this module includes ships and freqs, kills, position
 * packet routing, and prizes.
 */

/** this callback is be called whenever a kill occurs */
#define CB_KILL "kill-2"
/** the type of CB_KILL.
 * @param arena the arena the kill took place in
 * @param killer the player who made the kill
 * @param killed the player who got killed
 * @param bounty the number displayed in the kill message (not
 * necessarily equal to the killed->position.bounty)
 * @param flags the number of flags the killed player was carrying
 * @param pts a pointer to the total number of points this kill was worth.
 * NOTE: pts should not be altered. Use the kill adviser for changing 
 * the points from a kill.
 * @param green a pointer to the the kill green that was placed for this death.
 * NOTE: green should not be altered. Use the killgreen interface for selecting 
 * the kill green.
 */
typedef void (*KillFunc)(Arena *arena, Player *killer, Player *killed,
		int bounty, int flags, int *pts, int *green);
/* pycb: arena, player, player, int, int, int inout, int inout */


#define I_KILL_GREEN "kill-green-1"

/** implement this interface to determine greens given out to players after death */
typedef struct Ikillgreen
{
	INTERFACE_HEAD_DECL
	/* pyint: use */
	
	/** This function should return the green to be used in the kill packet
	 * @param arena the arena the kill took place in
	 * @param killer the player who made the kill
	 * @param killed the player who got killed
	 * @param bounty the number displayed in the kill message
	 * @param flags the number of flags the killed player was carrying
	 * @param pts The total number of points this kill was worth
	 * @param green The default kill green
	 */
	int (*KillGreen)(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int pts, int green);
	/* pyint: arena, player, player, int, int, int, int -> int */
} Ikillgreen;

#define A_KILL "kill-2"
typedef struct Akill
{
	ADVISER_HEAD_DECL

	/** Modifies the amount of points a player will receive for making a kill.
	 * This function should return the number of points to be added to the total
	 *
	 * @param arena the arena the kill took place in
	 * @param killer the player who made the kill
	 * @param killed the player who got killed
	 * @param bounty the number displayed in the kill message 
	 * @param flags the number of flags the killed player was carrying
	 */
	int (*KillPoints)(Arena *arena, Player *killer, Player *killed, int bounty, int flags);

	/** Modifies a player's death packet before it is sent out.
	 *  killer may be set to NULL to drop the death packet
	 *  Make sure the killer is in the same arena.
	 *
	 * @param arena the arena the kill took place in
	 * @param killer the player who made the kill
	 * @param killed the player who got killed
	 * @param bounty the number displayed in the kill message
	 */
	void (*EditDeath)(Arena *arena, Player **killer, Player **killed, int *bounty);
	
} Akill;

/** this callback is to be called when a player changes ship or freq.
 * intended for internal or core use only. no recursive shipchanges should
 * happen as a result of this callback. */
#define CB_PRESHIPFREQCHANGE "preshipfreqchange"
/* the type of CB_PRESHIPFREQCHANGE
 * @param p the player changing ship or freq
 * @param newship the player's new ship number
 * @param oldship the player's old ship number
 * @param newfreq the player's new frequency
 * @param oldfreq the player's old frequency
 */
typedef void (*PreShipFreqChangeFunc)(Player *p, int newship, int oldship, int newfreq, int oldfreq);
/* pycb: player, int, int, int, int */


/** this callback is to be called when a player changes ship or freq. */
#define CB_SHIPFREQCHANGE "shipfreqchange"
/* the type of CB_SHIPFREQCHANGE
 * @param p the player changing ship or freq
 * @param newship the player's new ship number
 * @param oldship the player's old ship number
 * @param newfreq the player's new frequency
 * @param oldfreq the player's old frequency
 */
typedef void (*ShipFreqChangeFunc)(Player *p, int newship, int oldship, int newfreq, int oldfreq);
/* pycb: player, int, int, int, int */


/** this callback will be called when the player respawns after dying, or enters the game.
 * 'reason' is a bitwise combination of the applicable SPAWN_ values, so use something like
 * (reason & SPAWN_SHIPRESET) to check, rather than ==. */
#define CB_SPAWN "spawn"
/** the type of CB_SPAWN
 * @param p the player who has spawned
 * @param reason a bitwise combination of applicable SPAWN_ values
 */
typedef void (*SpawnFunc)(Player *p, int reason);
/* pycb: player, int */

/* pyconst: define int, "SPAWN_*" */
/* the player died and is respawning now */
#define SPAWN_AFTERDEATH 1
/* a shipreset or similar functionality was applied on the user */
#define SPAWN_SHIPRESET 2
/* a flag victory triggered this spawn */
#define SPAWN_FLAGVICTORY 4
/* changing ships triggered this spawn */
#define SPAWN_SHIPCHANGE 8
/* this is the first spawn since leaving spec, or entering the arena */
#define SPAWN_INITIAL 16


/** this callback called when the game timer expires. */
#define CB_TIMESUP "timesup"
/** the type of CB_TIMESUP
 * @param arena the arena whose timer is ending
 */
typedef void (*GameTimerFunc)(Arena *arena);
/* pycb: arena */


/** this callback is called when someone enters or leaves a safe zone.
 * be careful about what you do in here; this runs in the unreliable
 * packet handler thread (for now). */
#define CB_SAFEZONE "safezone"
/** the type of CB_SAFEZONE
 * @param p the player entering/leaving the safe zone
 * @param x the player's location in pixels, x coordinate
 * @param y the player's location in pixels, y coordinate
 * @param entering true if entering, false if exiting
 */
typedef void (*SafeZoneFunc)(Player *p, int x, int y, int entering);
/* pycb: player, int, int, int */


/** this callback is called when someone enters or leaves a region. */
#define CB_REGION "region-1"
/** the type of CB_REGION
 * @param p the player entering/leaving the region
 * @param rgn the region being entered or left
 * @param x the player's location in pixels, x coordinate
 * @param y the player's location in pixels, y coordinate
 * @param entering true if entering, false if exiting
 */
typedef void (*RegionFunc)(Player *p, Region *rgn, int x, int y, int entering);
/* NOTYETpycb: player, void, int, int, int */


/** this callback is called whenever someone picks up a green. */
#define CB_GREEN "green-1"
/** the type of CB_GREEN
 * @param p the player who picked up the green
 * @param x the x coordinate of the player in tiles
 * @param y the y coordinate of the player in tiles
 * @param prize the type of green picked up
 */
typedef void (*GreenFunc)(Player *p, int x, int y, int prize);
/* pycb: player, int, int, int */


/** this callback is called whenever someone attaches or detaches. */
#define CB_ATTACH "attach-1"
/** the type of CB_ATTACH
 * @param p the player who is attaching or detaching
 * @param to the player being attached to, or NULL when detaching
 */
typedef void (*AttachFunc)(Player *p, Player *to);
/* pycb: player, player */

/** this calllback is called whenever a position packet is handled.
 * Note that this callback is not called for spectators.*/
#define CB_PPK "cbppk-1"
/** the type of CB_PPK
 * @param p the player that the position packet belongs to
 * @param pos the position packet
 */
typedef void (*PPKFunc)(Player *p, const struct C2SPosition *pos);


enum { ENERGY_SEE_NONE, ENERGY_SEE_ALL, ENERGY_SEE_TEAM, ENERGY_SEE_SPEC };


#define A_PPK "ppk-1"

/** the position packet adviser struct */
typedef struct Appk
{
	ADVISER_HEAD_DECL

	/** Modifies a player's position packet before it is sent out.
	 * The adviser may edit the packet, but should not rely on
	 * this function for notification, as other advisers may be
	 * consulted. Instead the CB_PPK callback should be used for
	 * notification.
	 * @param p the player that the position packet belongs to
	 * @param pos a pointer to the position packet
	 */
	void (*EditPPK)(Player *p, struct C2SPosition *pos);
	/** Modifies a player's position packet before it is sent to a specific player.
	 * @param p the player that the position packet belongs to
	 * @param t the player that the position packet will be sent to
	 * @param pos a pointer to the position packet
	 * @param extralen the extra length of the position packet (0 = none, 2 = energy, 10 = epd)
	 * @return TRUE if this function modified the packet, FALSE otherwise
	 */
	int (*EditIndividualPPK)(Player *p, Player *t, struct C2SPosition *pos, int *extralen);
} Appk;

/** the game interface id */
#define I_GAME "game-9"

/** the game interface struct */
typedef struct Igame
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Changes a player's freq.
	 * This is an unconditional change; it doesn't go through the freq
	 * manager.
	 * @param p the player to change
	 * @param freq the freq to change to
	 */
	void (*SetFreq)(Player *p, int freq);
	/* pyint: player, int -> void */

	/** Changes a player's ship.
	 * This is an unconditional change; it doesn't go through the freq
	 * manager.
	 * @param p the player to change
	 * @param ship the freq to change to
	 */
	void (*SetShip)(Player *p, int ship);
	/* pyint: player, int -> void */

	/** Changes a player's ship and freq together.
	 * This is an unconditional change; it doesn't go through the freq
	 * manager.
	 * @param p the player to change
	 * @param ship the ship to change to
	 * @param freq the freq to change to
	 */
	void (*SetShipAndFreq)(Player *p, int ship, int freq);
	/* pyint: player, int, int -> void */

	/** Moves a set of playes to a specific location.
	 * This uses the Continuum warp feature, so it causes the little
	 * flashy thing, and the affected ships won't be moving afterwards.
	 * @see Target
	 * @param target the things to warp
	 * @param x the destination of the warp in tiles, x coordinate
	 * @param y the destination of the warp in tiles, y coordinate
	 */
	void (*WarpTo)(const Target *target, int x, int y);
	/* pyint: target, int, int -> void */

	/** Gives out prizes to a set of players.
	 * @param target the things to give prizes to
	 * @param type the type of the prizes to give, or 0 for random
	 * @param count the number of prizes to give
	 */
	void (*GivePrize)(const Target *target, int type, int count);
	/* pyint: target, int, int -> void */

	/** Locks a set of players to their ship and freq.
	 * Note that this doesn't modify the default lock state for the
	 * arena, so this will not affect newly entering players at all. Use
	 * LockArena for that.
	 * @param t the players to lock
	 * @param notify whether to notify the affected players (with an
	 * arena message) if their lock status has changed
	 * @param spec whether to spec them before locking them to their
	 * ship/freq
	 * @param timeout the number of sections the lock should be
	 * effectice for, or zero for indefinite.
	 */
	void (*Lock)(const Target *t, int notify, int spec, int timeout);
	/* pyint: target, int, int, int -> void */

	/** Undoes the effect of Lock, allowing players to change ship and
	 ** freq again.
	 * Again, note that this doesn't affect the default lock state for
	 * the arena.
	 * @param t the players to unlock
	 * @param notify whether to notify the affected players if their
	 * lock status changed
	 */
	void (*Unlock)(const Target *t, int notify);
	/* pyint: target, int -> void */

	/** Locks all players in the arena to spectator mode, or to their
	 ** current ships.
	 * This modifies the arena lock state, and also has the effect of
	 * calling Lock on all the players in the arena.
	 * @param a the arena to apply changes to
	 * @param notify whether to notify affected players of their change
	 * in state
	 * @param onlyarenastate whether to apply changes to the default
	 * arena lock state only, and not change the state of current
	 * players
	 * @param initial whether entering players are only locked to their
	 * inital ships, rather than being forced into spectator mode and
	 * then being locked
	 * @param spec whether to force all current players into spec before
	 * locking them
	 */
	void (*LockArena)(Arena *a, int notify, int onlyarenastate, int initial, int spec);
	/* pyint: arena, int, int, int, int -> void */

	/** Undoes the effect of LockArena by changing the arena lock state
	 ** and unlocking current players.
	 * @param a the arena to apply changes to
	 * @param notify whether to notify affected players of their change
	 * in state
	 * @param onlyarenastate whether to apply changes to the default
	 */
	void (*UnlockArena)(Arena *a, int notify, int onlyarenastate);
	/* pyint: arena, int, int -> void */

	void (*FakePosition)(Player *p, struct C2SPosition *pos, int len);
	void (*FakeKill)(Player *killer, Player *killed, int pts, int flags);

	double (*GetIgnoreWeapons)(Player *p);
	void (*SetIgnoreWeapons)(Player *p, double proportion);

	/** Resets the target's ship(s).
	 * @param target the players to shipreset
	 */
	void (*ShipReset)(const Target *target);
	/* pyint: target -> void */

	void (*IncrementWeaponPacketCount)(Player *p, int packets);

	void (*SetPlayerEnergyViewing)(Player *p, int value);
	void (*SetSpectatorEnergyViewing)(Player *p, int value);
	void (*ResetPlayerEnergyViewing)(Player *p);
	void (*ResetSpectatorEnergyViewing)(Player *p);

	void (*DoWeaponChecksum)(struct S2CWeapons *pkt);

	/**
	 * Checks if a player is antiwarped and optionally gives a list of
	 * the antiwarping players. Don't forget to empty the list afterwards.
	 * The list pointer may be NULL.
	 *
	 * @param p the player to check
	 * @param players a list to fill with antiwarping players
	 * @return TRUE if antiwarped, FALSE otherwise
	 */
	int (*IsAntiwarped)(Player *p, LinkedList *players);
} Igame;


#endif

