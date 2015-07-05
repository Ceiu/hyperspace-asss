
/* dist: public */

#ifndef __OBJECTS_H
#define __OBJECTS_H

/* Iobjects
 * this module will handle all object-related packets
 */

#define I_OBJECTS "objects-3"

typedef struct Iobjects
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/* sends the current LVZ object state to a player */
	void (*SendState)(Player *p);
	/* pyint: player -> void */

	/* if target is an arena, the defaults are changed */
	void (*Toggle)(const Target *t, int id, int on);
	/* pyint: target, int, int -> void */
	void (*ToggleSet)(const Target *t, short *id, char *ons, int size);

	/* use last two parameters for rel_x, rel_y when it's a screen object */
	void (*Move)(const Target *t, int id, int x, int y, int rx, int ry);
	/* pyint: target, int, int, int, int, int -> void */
	void (*Image)(const Target *t, int id, int image);
	/* pyint: target, int, int -> void */
	void (*Layer)(const Target *t, int id, int layer);
	/* pyint: target, int, int -> void */
	void (*Timer)(const Target *t, int id, int time);
	/* pyint: target, int, int -> void */
	void (*Mode)(const Target *t, int id, int mode);
	/* pyint: target, int, int -> void */
} Iobjects;

#endif

