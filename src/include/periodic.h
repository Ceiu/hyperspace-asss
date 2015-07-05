
/* dist: public */

#ifndef __PERIODIC_H
#define __PERIODIC_H


#define I_PERIODIC_POINTS "periodic-points-1"

typedef struct Iperiodicpoints
{
	INTERFACE_HEAD_DECL
	/* pyint: use, impl */
	int (*GetPoints)(
			Arena *arena,
			int freq,
			int freqplayers,
			int totalplayers,
			int flagsowned);
	/* this will be called by the periodic timer module for each freq
	 * that exists when points should be awarded. it should figure out
	 * how many points to give to that freq, and return that value.
	 * the parameters passed (other than arena and freq), are just for
	 * utilty. feel free to query any other information about the freq
	 * necessary to determine reward points. */
	/* pyint: arena, int, int, int, int -> int */
} Iperiodicpoints;

#endif

