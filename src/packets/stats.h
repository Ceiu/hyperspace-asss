
#ifndef __PACKETS_STATS_H
#define __PACKETS_STATS_H

#pragma pack(push,1)

/* NOTE: the protocol described by this file isn't implemented yet! */

/* this stuff is for the new stats display protocol */

/* basic ideas:
 *
 * the server keeps track of lots of stats for each player for each
 * arena. each stat is identified by a "stat id", an unsigned integer
 * that fits in 16 bits. each stat has a value, which is a signed 32-bit
 * integer.
 *
 * the file statcodes.h contains the definitions of the stat ids. when
 * adding new stat ids to that file, make sure that the ids of existing
 * stats are not altered.
 *
 * when entering an arena, the server sends a few packets to the client
 * describing what the F2 boxes look like for that arena. there may be
 * more than one stat box. each S2C_STATBOX packet defines a single one.
 * each box has a name, that the client can display to help the user
 * differentiate them.
 *
 * each box can have several columns, each with a name. to get a value
 * to display for a particular column, the client is required to do a
 * tiny bit of processing: each column has an operation, and two stat
 * ids. for OP_ADD, the client should take the values of the stats given
 * by the stat ids, and add them. for OP_DIV, it divides them. for
 * OP_ONE, it simply takes the value given by the first stat. for
 * OP_RATING, it uses specific (hardcoded, not sent in the packet) stat
 * ids, and puts them in the classic rating forumla. for OP_NAME and
 * OP_SQUAD, it doesn't display a number, but instead the player's name
 * and squad.
 *
 * each box also has a sort mode, described in the comments below.
 *
 * the server can send S2C_STATUPDATE packets at any time. each one may
 * contain data for any number of players.
 *
 * if the stat box requires the client to display a piece of data that
 * the server has not sent it yet, it should display a zero.
 *
 */

#define S2C_STATBOX 0xFF
#define S2C_STATUPDATE 0xFF


enum sort_by
{
	SORT_ALPHA_TEAM, /* sort alphabetically, player's team on top */
	SORT_ALPHA, /* sort everyone alphabetically */
	SORT_TEAM, /* sort by team */
	SORT_COLUMN_0, /* sort based on the values of the first column */
};

enum stat_op
{
	OP_ONE, /* a */
	OP_ADD, /* a + b */
	OP_DIV, /* a / b */
	OP_RATING, /* the rating forumula */
	OP_NAME, /* display the player's name */
	OP_SQUAD, /* display the player's squad */
};

struct S2CStatBox
{
	u8 type; /* S2C_STATBOX */
	char boxname[24];
	u8 sortby;
	/* there can be any number of these */
	struct Column
	{
		u8 op;
		u16 a_stat;
		u8 a_interval;
		u16 b_stat;
		u8 b_interval;
		char name[16];
	} columns[0];
};

struct S2CStatUpdate
{
	u8 type; /* S2C_STATUPDATE */
	/* any number of these */
	struct StatUpdate
	{
		u16 pid; /* if pid == -1, apply this update to all players */
		u16 statid; /* if statid == -1, apply this update to all stats for this pid */
		u8 interval; /* if interval == -1, INCREMENT stats in all
		                invervals by newval */
		i32 newval;
	} updates[0];
};

#pragma pack(pop)

#endif

