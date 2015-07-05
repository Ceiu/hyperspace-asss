
/* dist: public */

#ifndef __MAPDATA_H
#define __MAPDATA_H

/** @file
 * the mapdata module manages the contents of lvl files. other modules
 * that need information about the location of objects on the map should
 * use it.
 *
 * internally, the map file is represented as a sparse array using a
 * two-dimensional trie structure. it uses about 200k per map, which is
 * 1/5 of the space a straight bitmap would use, but retains efficient
 * access speeds.
 */

/** return codes for GetTile() */
enum map_tile_t
{
	/* standard tile types */
	TILE_NONE           = 0,

	TILE_START          = 1,
	/* map borders are not included in the .lvl files */
	TILE_BORDER         = 20,
	TILE_END            = 160,
	/* tiles up to this point are part of security checksum */

	TILE_V_DOOR_START   = 162,
	TILE_V_DOOR_END     = 165,

	TILE_H_DOOR_START   = 166,
	TILE_H_DOOR_END     = 169,

	TILE_TURF_FLAG      = 170,

	/* only other tile included in security checksum */
	TILE_SAFE           = 171,

	TILE_GOAL           = 172,

	/* fly-over */
	TILE_OVER_START     = 173,
	TILE_OVER_END       = 175,

	/* fly-under */
	TILE_UNDER_START    = 176,
	TILE_UNDER_END      = 190,

	TILE_TINY_ASTEROID  = 216,
	TILE_BIG_ASTEROID   = 217,
	TILE_TINY_ASTEROID2 = 218,

	TILE_STATION        = 219,

	TILE_WORMHOLE       = 220,

	/* internal tile types */
	TILE_BRICK          = 250,
};


typedef struct Region Region;
/* pytype: opaque, Region *, region */


struct mapdata_memory_stats_t
{
	int lvlbytes, lvlblocks, rgnbytes, rgnblocks;
	int reserved[8];
};


/** interface id for Imapdata */
#define I_MAPDATA "mapdata-9"

/** interface struct for Imapdata
 * you should use this to figure out what's going on in the map in a
 * particular arena.
 */
typedef struct Imapdata
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** finds the file currently used as this arena's map.
	 * you should use this function and not try to figure out the map
	 * filename yourself based on arena settings.
	 * @param arena the arena whose map we want
	 * @param buf where to put the resulting filename
	 * @param buflen how big buf is
	 * @param mapname null if you're looking for an lvl, or the name of
	 * an lvz file.
	 * @return true if it could find a lvl or lvz file, buf will contain
	 * the result. false if it failed.
	 */
	int (*GetMapFilename)(Arena *arena, char *buf, int buflen, const char *mapname);
	/* pyint: arena, string out, int buflen, zstring -> int */

	/** gets the named attribute for the arena's map.
	 * @param arena the arena whose map we care about.
	 * @param key the attribute key to retrieve.
	 * @return the key's value, or NULL if not present
	 */
	const char * (*GetAttr)(Arena *arena, const char *key);
	/* pyint: arena, string -> string */

	/** like RegionChunk, but for the map itself. */
	int (*MapChunk)(Arena *arena, u32 ctype, const void **datap, int *sizep);
	/* pyint: arena, int, bufp out, int buflen out -> void */

	/** returns the number of flags on the map in this arena. */
	int (*GetFlagCount)(Arena *arena);
	/* pyint: arena -> int */

	/** returns the contents of a single tile of the map. */
	enum map_tile_t (*GetTile)(Arena *arena, int x, int y);
	/* pyint: arena, int, int -> int */

	/* the following three functions are in this module because of
	 * efficiency concerns. */

	/** finds the tile nearest to the given tile that is appropriate for
	 ** placing a flag. */
	void (*FindEmptyTileNear)(Arena *arena, int *x, int *y);
	/* pyint: arena, int inout, int inout -> void */

	u32 (*GetChecksum)(Arena *arena, u32 key);

	/* don't use this. */
	void (*DoBrick)(Arena *arena, int drop, int x1, int y1, int x2, int y2);

	void (*GetMemoryStats)(Arena *arena, struct mapdata_memory_stats_t *stats);

	/* new region interface */

#define MAKE_CHUNK_TYPE(s) (*(u32*)#s)

/* region chunk types */
/* pyconst: define int, "RCT_*" */
#define RCT_ISBASE          MAKE_CHUNK_TYPE(rBSE)
#define RCT_NOANTIWARP      MAKE_CHUNK_TYPE(rNAW)
#define RCT_NOWEAPONS       MAKE_CHUNK_TYPE(rNWP)
#define RCT_NOFLAGS         MAKE_CHUNK_TYPE(rNFL)
#define RCT_NORECVANTI      MAKE_CHUNK_TYPE(rNRA)
#define RCT_NORECVWEPS       MAKE_CHUNK_TYPE(rNRW)

	/** finds the region with a particular name.
	 * @param arena the arena that contains the map we want to look for
	 * the region in.
	 * @param name the name of the region we want. for now, this is
	 * case-insensitive.
	 * @return a handle for the specified region, or NULL if not found.
	 */
	Region * (*FindRegionByName)(Arena *arena, const char *name);
	/* pyint: arena, string -> region */

	/** gets the name of a region.
	 * @param region a region handle.
	 * @return the region's name. never NULL.
	 */
	const char * (*RegionName)(Region *rgn);
	/* pyint: region -> string */

	/** gets chunk data for a particular region.
	 * this checks if the given region has a chunk of the specified
	 * type, and optionally returns a pointer to its data.
	 * @param rgn a region handle.
	 * @param ctype the chunk type to look for. you probably want to use
	 * the MAKE_CHUNK_TYPE macro.
	 * @param datap if this isn't NULL, and the chunk is found, it will
	 * be filled in with a pointer to the chunk's data.
	 * @param sizep if this isn't NULL, and the chunk is found, it will
	 * be filled in with the length of the chunk's data.
	 * @return true if the chunk was found, false if not.
	 */
	int (*RegionChunk)(Region *rgn, u32 ctype, const void **datap, int *sizep);
	/* pyint: region, int, bufp out, int buflen out -> void */

	/** checks if the specified point is in the specified region.
	 * @param rgn a region handle.
	 * @param x the x coordinate
	 * @param y the y coordinate
	 * @return true if the point is contained, false if not
	 */
	int (*Contains)(Region *rgn, int x, int y);
	/* pyint: region, int, int -> int */

	/** calls the specified callback for each region defined in the map
	 ** that contains the given point.
	 * note that you can pass a LinkedList * as the closure arg and
	 * LLAdd as the callback to get a list of containing regions.
	 * @param arena the arena whose map we're dealing with.
	 * @param x the x coordinate, or -1 for all regions
	 * @param y the y coordinate, or -1 for all regions
	 * @param cb a callback that will get called once for each region
	 * that contains the point.
	 * @param clos a closure argument for the callback.
	 */
	void (*EnumContaining)(Arena *arena, int x, int y,
			void (*cb)(void *clos, Region *rgn), void *clos);
	/* pyint: arena, int, int, (clos, region -> void), clos -> void */

	/** finds some region containing the given point.
	 * @param arena the arena whose map we're dealing with.
	 * @param x the x coordinate
	 * @param y the y coordinate
	 * @return a region that contains the given point, or NULL if it
	 * isn't contained in any region.
	 */
	Region * (*GetOneContaining)(Arena *arena, int x, int y);
	/* pyint: arena, int, int -> region */

	void (*EnumLVZFiles)(Arena *arena,
			void (*func)(const char *fn, int optional, void *clos),
			void *clos);

	/** sets x and y to random coordinates within the specified region
	 * or -1 if there's an error.
	 * @param rgn the Region to find a random point within
	 * @param x pointer to the x coordinate
	 * @param y pointer to the y coordinate
	 */
	void (*FindRandomPoint)(Region *rgn, int *x, int *y);
} Imapdata;

#endif

