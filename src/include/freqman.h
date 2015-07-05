
/* dist: public */

#ifndef __FREQMAN_H
#define __FREQMAN_H

/** the interface id for Ibalancer */
#define I_BALANCER "balancer-2"

/** The interface struct for Ibalancer.
 * This struct is designed to be unique per arena (unlike enforcer).
 * The balancer defines how teams should be balanced, but leaves the
 * actual balancing code in the freqman.
 */
typedef struct Ibalancer
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */

	/**
	 * Return an integer representing the player's balance metric.
	 * Freqman will use this to ensure the teams are balanced.
	 */
	int (*GetPlayerMetric)(Player *p);

	/**
	 * TODO
	 */
	int (*GetMaxMetric)(Arena *arena, int freq);

	/**
	 * This must yield the same value when freq1 and freq2 are interchanged.
	 * TODO
	 */
	int (*GetMaximumDifference)(Arena *arena, int freq1, int freq2);
} Ibalancer;

/** the adviser id for Aenforcer */
#define A_ENFORCER "enforcer-2"

/** The adviser struct for the enforcers.
 * These are designed to be implemented by non-core modules and
 * registered on a per-arena basis.
 */
typedef struct Aenforcer
{
	ADVISER_HEAD_DECL
	/* pyadv: use, impl */

	/**
	 * Returns a boolean indicating whether the player can change from his
	 * current ship/freq or not.
	 * Only write to err_buf if it's non-null
	 */
	 int (*IsUnlocked)(Player *p, int is_changing, char *err_buf, int buf_len);

	/**
	 * Returns a boolean indicating whether the player may enter the game at all
	 * This is called before the frequency they are landing on is decided, so
	 * p->p_freq should not be checked. Is only called if the player is in
	 * spectator mode.
	 * Only write to err_buf it it's non-null
	 */
	int (*CanEnterGame)(Player *p, int is_changing, char *err_buf, int buf_len);

	/**
	 * Called when checking if a player is allowed to change to the specified ship.
	 *
	 * @param *p
	 *	The player intending to change ships.
	 *
	 * @param new_ship
	 *	The ship to which the player intends to change.
	 *
	 * @param is_changing
	 *	Whether or not the player is actively changing ships, or simply checking if allowed.
	 *
	 * @param *err_buf
	 *	[In/Out] A buffer to which an adviser may write a message if a change is to be denied.
	 *
	 * @param buf_len
	 *	The length of the error message buffer.
	 *
	 * @return boolean
	 *	True if the player is allowed to change to the specified ship; false otherwise.
	 */
	int (*CanChangeToShip)(Player *p, int new_ship, int is_changing, char *err_buf, int buf_len);

	/**
	 * Called when checking if a player is allowed to change to the specified frequency.
	 *
	 * @param *p
	 *	The player intending to change ships.
	 *
	 * @param new_freq
	 *	The frequency the player intends to join.
	 *
	 * @param is_changing
	 *	Whether or not the player is actively changing frequencies, or simply checking if allowed.
	 *
	 * @param *err_buf
	 *	[In/Out] A buffer to which an adviser may write a message if a change is to be denied.
	 *
	 * @param buf_len
	 *	The length of the error message buffer.
	 *
	 * @return bool
	 *	True if the player is allowed to change to the specified frequency; false otherwise.
	 */
	int (*CanChangeToFreq)(Player *p, int new_freq, int is_changing, char *err_buf, int buf_len);

	/**
	 * Called when checking what ships are allowed on the specified frequency. The shipmask returned
	 * by this function should respect the same checks that would be run if CanChangeToShip were
	 * to be called. That is, if CanChangeToShip would return false, that ship should not be included
	 * in the resultant shipmask.
	 */
	shipmask_t (*GetAllowableShips)(Player *p, int freq, char *err_buf, int buf_len);
} Aenforcer;

/** the interface id for Ifreqman */
#define I_FREQMAN "freqman-3"

/** The interface struct for Ifreqman.
 * This interface is the second revision of Ifreqman. This implementation
 * is designed to use the above Ienforcer and Ibalancer. The actual freqman
 * module is implemented as a core module and shouldn't need to be replaced
 * for most tasks. Instead it uses the enforcers and balancers to implement
 * non-core functionality.
 */
typedef struct Ifreqman
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */

	/** called when a player connects and needs to be assigned to a freq.
	 */
	void (*Initial)(Player *p, int *ship, int *freq);

	/**
	 * Checks if the player can change to the specified ship. Does not actually perform a ship change.
	 *
	 * @param *p
	 *	The player to check.
	 *
	 * @param ship
	 *	The ship to which the player would be changing.
	 *
	 * @param *err_buf
	 *	[In/Out] A pointer to a buffer that may receive a message indicating why the ship change is
	 *	not allowed. If null, no message will be retrieved.
	 *
	 * @param buf_len
	 *	The length of the error buffer. Ignored if err_buf is null.
	 *
	 * @return
	 *	True if the player would be allowed to change to the specified ship; false otherwise.
	 */
	int (*CanChangeToShip)(Player *p, int ship, char *err_buf, int buf_len);

	/** called when a player requests a ship change.
	 * ship will initially contain the ship request, and freq will
	 * contain the player's current freq.
	 *
	 * @return
	 *	True if the ship change was successful; false otherwise.
	 */
	int (*ShipChange)(Player *p, int requested_ship, char *err_buf, int buf_len);

	/**
	 * Checks if the player can change to the specified frequency. Does not actually perform a
	 * frequency change.
	 *
	 * @param *p
	 *	The player for which to check if a frequency change would be allowed.
	 *
	 * @param freq
	 *	The frequency to which the player would be changing.
	 *
	 * @param *err_buf
	 *	[In/Out] A pointer to a buffer that may receive a message indicating why the frequency change
	 *	is not allowed. If null, no message will be retrieved.
	 *
	 * @param buf_len
	 *	The length of the error buffer. Ignored if err_buf is null.
	 *
	 * @return
	 *	True if the player would be allowed to change to the specified frequency; false otherwise.
	 */
	int (*CanChangeToFreq)(Player *p, int freq, char *err_buf, int buf_len);

	/**
	 * Checks if the player could change to the specified frequency if they were using the given ship.
	 * Does not actually perform a frequency change.
	 *
	 * @param Player *p
	 *	The player for which to check if a frequency change would be allowed.
	 *
	 * @param int freq
	 *	The frequency to which the player would be changing.
	 *
	 * @param char *err_buf
	 *	[In/Out] A pointer to a buffer that may receive a message indicating why the frequency change
	 *	is not allowed. If null, no message will be retrieved.
	 *
	 * @param int buf_len
	 *	The length of the error buffer. Ignored if err_buf is null.
	 *
	 * @return bool
	 *	True if the player would be allowed to change to the specified frequency; false otherwise.
	 */
	int (*CanChangeToFreqWithShip)(Player *p, int freq, int ship, char *err_buf, int buf_len);

	/** called when a player requests a freq change.
	 * ship will initially contain the player's ship, and freq will
	 * contain the requested freq.
	 *
	 * @return
	 *	True if the frequency change was successful; false otherwise.
	 */
	int (*FreqChange)(Player *p, int requested_freq, char *err_buf, int buf_len);

	/**
	 * Attempts to find a frequency on which to place a player who is entering the game from spectator
	 * mode. If a frequency cannot be found, this function returns the spectator frequency.
	 *
	 * @param *p
	 *	The player for which to find an entry frequency.
	 *
	 * @param is_changing
	 *	Whether or not the player should be considered to be changing during this check.
	 *
	 * @param char *err_buf
	 *	[In/Out] A pointer to a buffer that may receive a message indicating why the frequency change
	 *	is not allowed. If null, no message will be retrieved.
	 *
	 * @param int buf_len
	 *	The length of the error buffer. Ignored if err_buf is null.
	 */
	int (*FindEntryFreq)(Player *p, int is_changing, char *err_buf, int buf_len);

	/**
	 * Retrieves a shipmask representing the ships the player is allowed to use while on the specified
	 * frequency.
	 *
	 * @param *p
	 *	The player for which to retrieve the allowed ships.
	 *
	 * @param freq
	 *	The frequency for which to retrieve the allowed ships.
	 *
	 * @return
	 *	A shipmask representing the ships the player is allowed to use while on the specified
	 *	frequency.
	 */
	shipmask_t (*GetAllowableShips)(Player *p, int freq);
} Ifreqman;

#endif

