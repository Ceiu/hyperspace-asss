
/* dist: public */

#ifndef __BRICKWRITER_H
#define __BRICKWRITER_H

#define I_BRICKWRITER "brickwriter-2"

typedef struct Ibrickwriter
{
	INTERFACE_HEAD_DECL
	/* pyint: use */
	void (*BrickWrite)(Arena *arena, int freq, int x, int y, const char *text);
	/* pyint: arena, int, int, int, string -> void */
} Ibrickwriter;

#endif

