
/* dist: public */

#ifndef __CAPMAN_H
#define __CAPMAN_H

/** @file
 * This file describes the Icapman interface, which is used by other
 * modules to check whether players can do various actions. It also
 * describes Igroupman, which is implemented by the default capability
 * manger, but might not always be available.
 *
 * Capabilities are named in the following way:
 *
 * If a player has the capabilty "cmd_<some command>", he can use that
 * command "untargetted" (that is, typed as a public message).
 *
 * If a player has the capability "privcmd_<some command>", he can use
 * that command directed at a player or freq (private or team messages).
 *
 * Other capabilites (e.g., "seeprivarenas") don't follow any special
 * naming convention.
 *
 * Note that some zones might want to use alternate implementations of
 * the capability manager interface, which might not have the concept of
 * groups at all, and thus won't support Igroupman. For almost all
 * purposes, you should use Icapman and not Igroupman, to reduce
 * dependence on the specific capman implementation present.
 */


/** the interface id for Icapman */
#define I_CAPMAN "capman-4"

/** the interface struct for Icapman */
typedef struct Icapman
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */

	/** Check if a player has a given capability.
	 * Some common capabilities are defined as macros at the end of this
	 * file. Capabilities for commands are all named "cmd_foo" for ?foo,
	 * and "privcmd_foo" for /?foo.
	 * @param p the player to check
	 * @param cap the capability to check for
	 * @return true if the player has the capability
	 */
	int (*HasCapability)(Player *p, const char *cap);
	/* pyint: player, string -> int */

	/** Check if a player has a given capability, using a name instead
	 ** a player pointer.
	 * This works like HasCapability, but is intended to be used before
	 * the player's name has been assigned. You shouldn't have to use
	 * this.
	 */
	int (*HasCapabilityByName)(const char *name, const char *cap);
	/* pyint: string, string -> int */

	/** Checks if a player has a given capability in an arena other than
	 ** the one he's currently in.
	 * @param p the player
	 * @param a the arena to check in
	 * @param cap the capability to check for
	 * @return true if the player would have the requested capability,
	 * if he were in that arena
	 */
	int (*HasCapabilityInArena)(Player *p, Arena *a, const char *cap);
	/* pyint: player, arena, string -> int */

	/** Determines if a player can perform actions on another player.
	 * For certain actions (e.g., /?kick), you need to know if a player
	 * is at a "higher" level than another. Use this function to tell.
	 * The exact meaning of "higher" is determined by the capability
	 * manger, but it should at least be transitive.
	 * @param a a player
	 * @param b a player
	 * @return true if a is higher than b, false if not
	 */
	int (*HigherThan)(Player *a, Player *b);
	/* pyint: player, player -> int */
} Icapman;


/** the interface id for Igroupman */
#define I_GROUPMAN "groupman-3"

/** the interface struct for Igroupman.
 * you probably shouldn't use this.
 */
typedef struct Igroupman
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Returns the group that the player is currently in.
	 * The group might change if the player moves to another arena.
	 * Don't overwrite the string pointed to by the return value. Make a
	 * copy if you need to refer to it later.
	 */
	const char *(*GetGroup)(Player *p);
	/* pyint: player -> string */

	/** Changes a player's group (permanently).
	 * info is as in Iconfig::SetStr
	 */
	void (*SetPermGroup)(Player *p, const char *group, int global, const char *info);
	/* pyint: player, string, int, zstring -> void */

	/** Changes a players group (temporarily, until arena change). */
	void (*SetTempGroup)(Player *p, const char *group);
	/* pyint: player, string -> void */

	/** Changes a player's group back to the default (permanently).
	 * info is as in Iconfig::SetStr
	 */
	void (*RemoveGroup)(Player *p, const char *info);
	/* pyint: player, zstring -> void */

	/** Checks if a group password is correct.
	 * @return true if pwd is the password for group, false if not
	 */
	int (*CheckGroupPassword)(const char *group, const char *pwd);
	/* pyint: string, string -> int */
} Igroupman;

/** the maximum length of a group name */
#define MAXGROUPLEN 32

/* some standard capability names */

/* pyconst: define string, "CAP_*" */
/** if a player can see mod chat messages */
#define CAP_MODCHAT               "seemodchat"
/** if a player can send mod chat messages */
#define CAP_SENDMODCHAT           "sendmodchat"
/** if a player can send voice messages */
#define CAP_SOUNDMESSAGES         "sendsoundmessages"
/** if a player can upload files (note that this is separate from
 ** cmd_putfile, and both are required to use ?putfile) */
#define CAP_UPLOADFILE            "uploadfile"
/** if a player can see urgent log messages from all arenas */
#define CAP_SEESYSOPLOGALL        "seesysoplogall"
/** if a player can see urgent log messages from the arena he's in */
#define CAP_SEESYSOPLOGARENA      "seesysoplogarena"
/** if a player can see private arenas (in ?arena, ?listmod, etc.) */
#define CAP_SEEPRIVARENA          "seeprivarena"
/** if a player can see private freqs */
#define CAP_SEEPRIVFREQ           "seeprivfreq"
/** if a security warnings are suppressed for the player */
#define CAP_SUPRESS_SECURITY      "supresssecurity"
/** if a player can stay connected despite security checksum failures */
#define CAP_BYPASS_SECURITY       "bypasssecurity"
/** if a client can send object change broadcast packets (as some bots
 ** might want to do) */
#define CAP_BROADCAST_BOT         "broadcastbot"
/** if a client can send arbitrary broadcast packets (shouldn't ever
 ** give this out) */
#define CAP_BROADCAST_ANY         "broadcastany"
/** if a player can avoid showing up in ?spec output */
#define CAP_INVISIBLE_SPECTATOR   "invisiblespectator"
/** if a client can escape chat flood detection */
#define CAP_CANSPAM               "unlimitedchat"
/** if a client can use the settings change packet (note this is
 ** separate from cmd_quickfix/cmd_getsettings and both are required to
 ** use ?quickfix/?getsettings) */
#define CAP_CHANGESETTINGS        "changesettings"
/** if a player shows up in ?listmod output */
#define CAP_IS_STAFF              "isstaff"
/** if a player can sees all non-group-default players even if they lack isstaff */
#define CAP_SEE_ALL_STAFF         "seeallstaff"
/** if a player always forces a change with setship or setfreq instead of going by the arena freqman */
#define CAP_FORCE_SHIPFREQCHANGE  "forceshipfreqchange"

#endif

