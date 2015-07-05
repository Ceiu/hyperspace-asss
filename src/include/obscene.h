
/* dist: public */

#ifndef __OBSCENE_H
#define __OBSCENE_H

#define I_OBSCENE "obscene-2"

typedef struct Iobscene
{
	INTERFACE_HEAD_DECL

	/* Filters a line of text for obscene words. If any are found, they
	 * will be replaced by garbage characters, and Filter will return
	 * true. If not, the line will be unmodified and Filter will return
	 * false. */
	int (*Filter)(char *line);
} Iobscene;


#endif

