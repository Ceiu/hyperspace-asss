
/* dist: public */

#ifndef __BRICKS_H
#define __BRICKS_H

/* these should be mostly self-explanatory. */

#define BRICK_MODE_MAP(F) \
	F(BRICK_VIE)     /* standard VIE brick drop mode */  \
	F(BRICK_AHEAD)   /* brick dropped ahead in direction of player */  \
	F(BRICK_LATERAL) /* brick dropped across player ship,  \
	                  * perpendicular to direction */  \
	F(BRICK_CAGE)    /* cage is created out of 4 bricks */

DEFINE_ENUM(BRICK_MODE_MAP)

#define CB_DOBRICKMODE "dobrickmode"

typedef void (*DoBrickModeFunction)(Arena *arena, int brickmode, int dropx,
		int dropy, int direction, int length, LinkedList *bricks);

typedef struct Brick
{
	int x1, y1, x2, y2;
} Brick;

#define I_BRICKS "bricks-2"

typedef struct Ibricks
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*DropBrick)(Arena *arena, int freq, int x1, int y1, int x2, int y2);
	/* pyint: arena, int, int, int, int, int -> void */
} Ibricks;

#define I_BRICK_HANDLER "bh-2"

typedef struct Ibrickhandler
{
	INTERFACE_HEAD_DECL

	/* Should return a set of Brick structs in the bricks list. The
	 * brick module will handle deleting them. */
	void (*HandleBrick)(Player *p, int x, int y, LinkedList *bricks);
} Ibrickhandler;

#endif

