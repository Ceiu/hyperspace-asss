
/* dist: public */

#ifndef __FAKE_H
#define __FAKE_H


#define I_FAKE "fake-2"

typedef struct Ifake
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	Player * (*CreateFakePlayer)(const char *name, Arena *arena, int ship, int freq);
	/* pyint: string, arena, int, int -> player */
	int (*EndFaked)(Player *p);
	/* pyint: player -> int */
} Ifake;


#endif

