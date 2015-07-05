#ifndef SELFPOS_H
#define SELFPOS_H

#include "packets/ppk.h"

#define I_SELFPOS "selfpos-2"

typedef struct Iselfpos
{
	INTERFACE_HEAD_DECL

	/** Warps the player to the specified coordinates. Allows one to set
	 * the players velocity and angle.
	 *
	 * @param p the player
	 * @param dest_x the x component of the position in pixels
	 * @param dest_y the y component of the position in pixels
	 * @param v_x the velocity in the x direction in pixels/1000 ticks
	 * @param v_y the velocity in the y direction in pixels/1000 ticks
	 * @param rotation the rotation (0-40)
	 * @param delta_t how much to advance the packet in ticks. Negative
	 * 		values appear earlier in time.
	 */
	void (*WarpPlayer)(Player *p, int dest_x, int dest_y, int v_x, int v_y,
		int rotation, int delta_t);

	/** "Warps" the player with the given weapon packet.
	 *
	 * @param p the player
	 * @param dest_x the x component of the position in pixels
	 * @param dest_y the y component of the position in pixels
	 * @param v_x the velocity in the x direction in pixels/1000 ticks
	 * @param v_y the velocity in the y direction in pixels/1000 ticks
	 * @param rotation the rotation (0-40)
	 * @param delta_t how much to advance the packet in ticks. Negative
	 * 		values appear earlier in time.
	 * @param weapon the weapon struct to use in the position packet
	 */
	void (*WarpPlayerWithWeapon)(Player *p, int dest_x, int dest_y,
		int v_x, int v_y, int rotation, int delta_t,
		struct Weapons *weapon);

	/** Sets the bounty of a player by sending them a position packet.
	 * This will attempt to coalesce sequential bounty updates into one
	 * update.
	 *
	 * @param p the player
	 * @param bounty the new bounty of the player
	 */
	void (*SetBounty)(Player *p, int bounty);

	/** Sets the status field of a player by sending them a position
	 * packet. This will attempt to coalesce sequential status updates
	 * into one update.
	 *
	 * @param p the player
	 * @param status the new status of the player
	 */
	void (*SetStatus)(Player *p, int status);
} Iselfpos;

#endif /* SELFPOS */
