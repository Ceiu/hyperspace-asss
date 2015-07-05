
/* dist: public */

#ifndef __WATCHDAMAGE_H
#define __WATCHDAMAGE_H

#include "packets/watchdamage.h"

/* called when get player damage */
#define CB_PLAYERDAMAGE "playerdamage"
typedef void (*PlayerDamage)(Arena *arena, Player *p, struct S2CWatchDamage *damage, int count);


#define I_WATCHDAMAGE "watchdamage-2"

typedef struct Iwatchdamage
{
	INTERFACE_HEAD_DECL

	int (*AddWatch)(Player *p, Player *target);
	/* adds a watch from player on target */

	void (*RemoveWatch)(Player *p, Player *target);
	/* removes a watch from player on target */

	void (*ClearWatch)(Player *p, int himtoo);
	/* removes watches on player, both to and from, including modules */

	void (*ModuleWatch)(Player *p, int on);
	/* toggles if a module wants to watch player */

	int (*WatchCount)(Player *p);
	/* tells how many are watching this player, including modules */
} Iwatchdamage;

#endif

