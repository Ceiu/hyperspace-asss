
/* dist: public */

#ifndef __CORE_H
#define __CORE_H

/** @file
 * contains definitions related to player logins, events, and freq
 * management.
 */

#include "packets/login.h"


/* authentication return codes */
/* pyconst: define int, "AUTH_*" */
#define AUTH_OK              0x00   /* success */
#define AUTH_NEWNAME         0x01   /* fail */
#define AUTH_BADPASSWORD     0x02   /* fail */
#define AUTH_ARENAFULL       0x03   /* fail */
#define AUTH_LOCKEDOUT       0x04   /* fail */
#define AUTH_NOPERMISSION    0x05   /* fail */
#define AUTH_SPECONLY        0x06   /* success */
#define AUTH_TOOMANYPOINTS   0x07   /* fail */
#define AUTH_TOOSLOW         0x08   /* fail */
#define AUTH_NOPERMISSION2   0x09   /* fail */
#define AUTH_NONEWCONN       0x0A   /* fail */
#define AUTH_BADNAME         0x0B   /* fail */
#define AUTH_OFFENSIVENAME   0x0C   /* fail */
#define AUTH_NOSCORES        0x0D   /* success */
#define AUTH_SERVERBUSY      0x0E   /* fail */
#define AUTH_TOOLOWUSAGE     0x0F   /* fail */
#define AUTH_ASKDEMOGRAPHICS 0x10   /* success */
#define AUTH_TOOMANYDEMO     0x11   /* fail */
#define AUTH_NODEMO          0x12   /* fail */
#define AUTH_CUSTOMTEXT      0x13   /* fail */      /* contonly */

/** which authentication result codes result in the player moving
 ** forward in the login process. */
#define AUTH_IS_OK(a) \
	((a) == AUTH_OK || (a) == AUTH_SPECONLY || (a) == AUTH_NOSCORES || (a) == AUTH_ASKDEMOGRAPHICS)


/** an authentication module must fill in one of these structs to return
 ** an authentication response.
 * if code is a failure code, none of the other fields matter (except
 * maybe customtext, if you want to return a custom error message).
 */
typedef struct AuthData
{
	int demodata;         /**< true if registration data is requested */
	int code;             /**< the authentication code returned (AUTH_BLAH) */
	int authenticated;    /**< true if the player is placed in a group based on name */
	char name[24];        /**< the name to assign to the player */
	char sendname[20];    /**< the client-visible name (not nul-terminated) */
	char squad[24];       /**< the squad to assign to the player */
	char customtext[256]; /**< custom text to return to the player. only valid if
	                           code == AUTH_CUSTOMTEXT. */
} AuthData;


/** playeraction event codes */
enum
{
	/* pyconst: enum, "PA_*" */
	/** the player is connecting to the server. not arena-specific. */
	PA_CONNECT,
	/** the player is disconnecting from the server. not arena-specific. */
	PA_DISCONNECT,
	/** this is called at the earliest point after a player indicates an
	 ** intention to enter an arena.
	 * you can use this for some questionable stuff, like redirecting
	 * the player to a different arena. but in general it's better to
	 * use PA_ENTERARENA for general stuff that should happen on
	 * entering arenas. */
	PA_PREENTERARENA,
	/** the player is entering an arena. */
	PA_ENTERARENA,
	/** the player is leaving an arena. */
	PA_LEAVEARENA,
	/** this is called at some point after the player has sent his first
	 ** position packet (indicating that he's joined the game, as
	 ** opposed to still downloading a map). */
	PA_ENTERGAME
};

/** this callback is called at several important points, including when
 ** players connect, disconnect, enter and leave arenas.
 * most basic per-player [de]initialization should happen here.
 */
#define CB_PLAYERACTION "playeraction"
/** the type of the CB_PLAYERACTION callback.
 * @param p the player doing something
 * @param action the specific event that's happening. this will be one
 * of the PA_... values.
 * @param if the action is entering or leaving an arena, this will be
 * the arena being entered or left.
 */
typedef void (*PlayerActionFunc)(Player *p, int action, Arena *arena);
/* pycb: player, int, arena */


/** the interface id for Ifreqman */
/*#define I_FREQMAN "freqman-1"*/

/** the interface struct for Ifreqman.
 * this interface is designed to be implemented by a non-core module,
 * and probably registered per-arena (as a result of attaching a module
 * to an arena). its functions are then called by core modules when a
 * player's ship/freq need to be changed for any reason. they will see
 * the player whose ship/freq is being changed, and the requested ship
 * and freq. they may modify the requested ship and freq. if the freq
 * isn't determined yet (as for InitialFreq, freq will contain -1).
 * if you want to deny the change request, just set ship and freq to the
 * same as the current values:
 * @code
 *     *ship = p->p_ship; *freq = p->p_freq;
 * @endcode
 */
/*typedef struct Ifreqman
{
	INTERFACE_HEAD_DECL
	/ * pyint: use, impl * /


	/ ** called when a player connects and needs to be assigned to a freq.
	 * ship will initially contain the requested ship, and freq will
	 * contain -1. * /
	void (*InitialFreq)(Player *p, int *ship, int *freq);
	/ * pyint: player, int inout, int inout -> void * /

	/ ** called when a player requests a ship change.
	 * ship will initially contain the ship request, and freq will
	 * contain the player's current freq. * /
	void (*ShipChange)(Player *p, int *ship, int *freq);
	/ * pyint: player, int inout, int inout -> void * /

	/ ** called when a player requests a freq change.
	 * ship will initially contain the player's ship, and freq will
	 * contain the requested freq. * /
	void (*FreqChange)(Player *p, int *ship, int *freq);
	/ * pyint: player, int inout, int inout -> void * /
} Ifreqman;*/


/** the interface id for Iauth */
#define I_AUTH "auth-2"

/** the interface struct for Iauth.
 * the core module will call this when a player attempts to connect to
 * the server. authentication modules can be chained in a somewhat
 * fragile way by grabbing a pointer to the old value with
 * mm->GetInterface before registering your own with mm->RegInterface.
 * watch out for priorities: auth interfaces have to be declared with
 * INTERFACE_HEAD_INIT_PRI.
 */
typedef struct Iauth
{
	INTERFACE_HEAD_DECL

	/** authenticate a player.
	 * this is called when the server needs to authenticate a login
	 * request. the full login packet will be given. an implementation
	 * must called the Done callback to complete the authentication
	 * procedure, but of course it doesn't have to call it within this
	 * function.
	 * @param p the player being authenticated
	 * @param lp the login packet provided by the player
	 * @param lplen the length of the provided packet
	 * @param Done the function to call when the authentication result
	 * is known. call it with the player and a filled-in AuthData.
	 */
	void (*Authenticate)(Player *p, struct LoginPacket *lp, int lplen,
			void (*Done)(Player *p, AuthData *data));
} Iauth;


#endif

