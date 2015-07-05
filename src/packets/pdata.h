
/* dist: public */

#ifndef __PACKETS_PDATA_H
#define __PACKETS_PDATA_H

#pragma pack(push,1)

/** @file
 ** the player data packet */

/** this is the literal packet that gets sent to standard clients.
 * some data the server uses is kept in here directly. */
typedef struct PlayerData
{
	u8 pktype;          /**< the type byte */
	i8 ship;            /**< which ship this player is in */
	u8 acceptaudio;     /**< whether this player wants voice messages */
	char name[20];      /**< the player's name (may not be nul terminated) */
	char squad[20];     /**< the player's squad (may not be nul terminated) */
	i32 killpoints;     /**< kill points (not authoritative) */
	i32 flagpoints;     /**< flag points (not authoritative) */
	i16 pid;            /**< the player id number */
	i16 freq;           /**< frequency */
	i16 wins;           /**< kill count (not authoritative) */
	i16 losses;         /**< death count (not authoritative) */
	i16 attachedto;     /**< the pid of the player this one is attached to (-1 for nobody) */
	i16 flagscarried;   /**< how many flags are being carried (not authoritative) */
	u8 miscbits;        /**< misc. bits (see below) */
} PlayerData;


/* flag bits for the miscbits field */

/** whether the player has a crown */
#define F_HAS_CROWN 0x01
#define SET_HAS_CROWN(pid) (p->pkt.miscbits |= F_HAS_CROWN)
#define UNSET_HAS_CROWN(pid) (p->pkt.miscbits &= ~F_HAS_CROWN)

/** whether clients should send data for damage done to this player. */
/* FIXME: not implemented in continuum yet */
#define F_SEND_DAMAGE 0x02
#define SET_SEND_DAMAGE(pid) /* (p->pkt.miscbits |= F_SEND_DAMAGE) */
#define UNSET_SEND_DAMAGE(pid) /* (p->pkt.miscbits &= ~F_SEND_DAMAGE) */

#pragma pack(pop)

#endif


