
/* dist: public */

#ifndef __FG_WZ_H
#define __FG_WZ_H

#define CB_WARZONEWIN "warzonewin"
typedef void (*WarzoneWinFunc)(Arena *a, int freq, int *points);
/* pycb: arena, int, int inout */
/* called to calculate the points awarded for a warzone-type flag game
 * win. handlers should increment *points. */

#endif

