
/* dist: public */

#ifndef __PERSIST_H
#define __PERSIST_H

/*
 * Ipersist - score manager
 *
 * this manages all player scores (and other persistent data, if there
 * ever is any).
 *
 * it works like this: other modules register Player/ArenaPersistentData
 * descriptors with persist. key is a unique number that will identify
 * the type of data. length is the number of bytes you want to store, up
 * to CFG_MAX_PERSIST_LENGTH. scope describes where this data is valid,
 * and interval describes how many copies of it there are.
 *
 * when a player connects to the server, GetPlayer will be called which
 * will read that player's persistent information from the file and call
 * the SetData of all registered PlayerPersistentData descriptors.
 * PutPlayer will be called when a player is disconnecting, which will
 * call each data descriptor's GetData function to get the data to write
 * to the file. when switching arenas, the previous arena's data will be
 * synced to the file, and then the new arena's information synced from
 * it.
 *
 * a few things to keep in mind: the *PersistentData structure should
 * never change while the program is running. furthermore, the key,
 * length, scope, and interval fields should never change at all, even
 * across runs, or any previously created files will become useless. the
 * player will be locked before any of the Get/Set/ClearData functions
 * are called.
 *
 * StabilizeScores can be called with an integer argument to encure the
 * score files will be in a consistent state for that many seconds. This
 * can be used to perform backups while the server is running.
 *
 */


#include "statcodes.h"
#include "db_layout.h"


typedef enum persist_scope_t
{
	/* pyconst: enum, "PERSIST_*" */

	PERSIST_ALLARENAS,
	/* using this for scope means per-player data in every arena */
	/* using this for scope means per-arena data will be stored
	 * per-arena */

	PERSIST_GLOBAL
	/* using this for scope means per-player data shared among all arenas */
	/* using this for scope means per-arena data will be shared among
	 * all arenas (so it will effectively be global data). */
} persist_scope_t;



typedef struct PlayerPersistentData
{
	int key, interval;
	persist_scope_t scope;
	int (*GetData)(Player *p, void *data, int len, void *clos);
	void (*SetData)(Player *p, void *data, int len, void *clos);
	void (*ClearData)(Player *p, void *clos);
	void *clos;
} PlayerPersistentData;

typedef struct ArenaPersistentData
{
	int key, interval;
	persist_scope_t scope;
	int (*GetData)(Arena *a, void *data, int len, void *clos);
	void (*SetData)(Arena *a, void *data, int len, void *clos);
	void (*ClearData)(Arena *a, void *clos);
	void *clos;
} ArenaPersistentData;

/* for per-player data, any data in the forever and reset intervals will
 * be shared among arenas with the same arenagroup. data in game
 * intervals is never shared among arenas. per-arena data is also never
 * shared among arenas.
 */


/* this will be called after any interval is ended; it will probably be
 * preceeded by a lot of ClearData calls. */
#define CB_INTERVAL_ENDED "endinterval"
typedef void (*EndIntervalFunc)(void);
/* pycb: void */


#define I_PERSIST "persist-7"

typedef struct Ipersist
{
	INTERFACE_HEAD_DECL

	void (*RegPlayerPD)(const PlayerPersistentData *pd);
	void (*UnregPlayerPD)(const PlayerPersistentData *pd);

	void (*RegArenaPD)(const ArenaPersistentData *pd);
	void (*UnregArenaPD)(const ArenaPersistentData *pd);

	void (*PutPlayer)(Player *p, Arena *a, void (*callback)(Player *p));
	void (*GetPlayer)(Player *p, Arena *a, void (*callback)(Player *p));
	/* a == NULL means global data */

	void (*PutArena)(Arena *a, void (*callback)(Arena *a));
	void (*GetArena)(Arena *a, void (*callback)(Arena *a));

	void (*EndInterval)(const char *arenagrp_or_arena_name, Arena *or_arena, int interval);
	/* only specify one of the first two params */

	void (*StabilizeScores)(int seconds, int query, void (*callback)(Player *dummy));

	/* generic db interface */

	/** Read key/value pair.
	 * callback(clos, TRUE) will be called when if the key is present
	 * and val will be filled in.
	 * callback(clos, FALSE) will be called if it's not found.
	 * key/keylen do not have to be preserve once this returns.
	 */
	void (*GetGeneric)(
			int typekey,
			void *key, int keylen,
			void *val, int vallen,
			void (*callback)(void *clos, int present),
			void *clos);
	/** Write key/value pair.
	 * callback(clos, _) will be called when the value is written.
	 * key/keylen/val/vallen do not have to be preserved once this
	 * returns.
	 */
	void (*PutGeneric)(
			int typekey,
			void *key, int keylen,
			void *val, int vallen,
			void (*callback)(void *clos, int unused),
			void *clos);
} Ipersist;


#endif

