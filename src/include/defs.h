
/* dist: public */

#ifndef __DEFS_H
#define __DEFS_H

/** @file
 * this header file contains various constants and definitions that are
 * useful for all modules and core parts of asss.
 */

#include <stddef.h>

#undef ATTR_FORMAT
#undef ATTR_MALLOC
#undef ATTR_UNUSED
#if defined(__GNUC__) && __GNUC__ >= 3
#define ATTR_FORMAT(type, idx, first) __attribute__ ((format (type, idx, first)))
#define ATTR_MALLOC() __attribute__ ((malloc))
#define ATTR_UNUSED() __attribute__ ((unused))
#else
#define ATTR_FORMAT(type, idx, first)
#define ATTR_MALLOC()
#define ATTR_UNUSED()
#endif

/** a version number, represented as a string */
#define ASSSVERSION "1.5.0"
/** a version number, represented as an integer */
#define ASSSVERSION_NUM 0x00010500
#define BUILDDATE __DATE__ " " __TIME__

#define CORE_MOD_INFO(module) (module " (" ASSSVERSION ", " BUILDDATE ")")


/** a useful typedef */
typedef unsigned char byte;


/* platform-specific stuff */

#if defined(_MSC_VER)
#define USING_MSVC 1

#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
/* #define HAVE_STDINT_H 1 */
#define HAVE_STDLIB_H 1
/* #define HAVE_INTTYPES_H 1 */
#define HAVE_LOCALE_H 1
#define HAVE_LOCALECONV 1
#define HAVE_LCONV_DECIMAL_POINT 1
#define HAVE_LCONV_THOUSANDS_SEP 1
#define HAVE_LONG_DOUBLE 1
/*
#define HAVE_LONG_LONG_INT 1
#define HAVE_UNSIGNED_LONG_LONG_INT 1
#define HAVE_INTMAX_T 1
#define HAVE_UINTMAX_T 1
#define HAVE_UINTPTR_T 1
#define HAVE_PTRDIFF_T 1
*/
/* 
#define HAVE_VA_COPY 1
#define HAVE___VA_COPY 1
*/
#endif

#if (defined(_WIN32) || defined(__WIN32__) || defined(__NT__)) && !defined(WIN32)
#define WIN32 /* for Borland and Watcom */
#endif

#ifndef WIN32
/** this macro should expand to whatever is necessary to make the
 ** compiler export a function definition from a shared library. */
#define EXPORT
#define TRUE (1)
#define FALSE (0)
/** a little macro for win32 compatibility */
#define closesocket(a) close(a)
#define O_BINARY 0
#else
#include "win32compat.h"
#endif


#include "sizes.h"

#include "util.h"


/** an alias to use to keep stuff local to modules */
#define local static


/* bring in local config options */
#include "param.h"


/** hopefully useful exit codes */
enum
{
	EXIT_NONE    = 0, /**< an exit from *shutdown */
	EXIT_RECYCLE = 1, /**< an exit from *recycle */
	EXIT_GENERAL = 2, /**< a general 'something went wrong' error */
	EXIT_MEMORY  = 3, /**< we ran out of memory */
	EXIT_MODCONF = 4, /**< the initial module file is missing */
	EXIT_MODLOAD = 5, /**< an error loading initial modules */
	EXIT_CHROOT  = 6  /**< can't chroot or setuid */
};

/** ship names */
enum
{
	/* pyconst: enum, "SHIP_*" */
	SHIP_WARBIRD = 0,
	SHIP_JAVELIN,
	SHIP_SPIDER,
	SHIP_LEVIATHAN,
	SHIP_TERRIER,
	SHIP_WEASEL,
	SHIP_LANCASTER,
	SHIP_SHARK,
	SHIP_SPEC
};


/** ship mask values, notably for use with freqman enforcers */
/* pyconst: define int, "SHIPMASK_*" */
#define SHIPMASK_NONE 0
#define SHIPMASK_WARBIRD 1
#define SHIPMASK_JAVELIN 2
#define SHIPMASK_SPIDER 4
#define SHIPMASK_LEVIATHAN 8
#define SHIPMASK_TERRIER 16
#define SHIPMASK_WEASEL 32
#define SHIPMASK_LANCASTER 64
#define SHIPMASK_SHARK 128
#define SHIPMASK_ALL 255

/** used to create a ship mask from a ship */
#define SHIPMASK(ship) (1 << ship)

/** use this macro to check whether a certain ship is marked in the mask */
#define SHIPMASK_HAS(ship, mask) ((mask) & (1 << (ship)))

/* type to hold shipmasks. The width may change for future clients. */
typedef u32 shipmask_t;


/** sound constants */
enum
{
	/* pyconst: enum, "SOUND_*" */
	SOUND_NONE = 0,
	SOUND_BEEP1,
	SOUND_BEEP2,
	SOUND_NOTATT,
	SOUND_VIOLENT,
	SOUND_HALLELLULA,
	SOUND_REAGAN,
	SOUND_INCONCEIVABLE,
	SOUND_CHURCHILL,
	SOUND_LISTEN,
	SOUND_CRYING,
	SOUND_BURP,
	SOUND_GIRL,
	SOUND_SCREAM,
	SOUND_FART1,
	SOUND_FART2,
	SOUND_PHONE,
	SOUND_WORLDATTACK,
	SOUND_GIBBERISH,
	SOUND_OOO,
	SOUND_GEE,
	SOUND_OHH,
	SOUND_AWW,
	SOUND_GAMESUCKS,
	SOUND_SHEEP,
	SOUND_CANTLOGIN,
	SOUND_BEEP3,
	SOUND_MUSICLOOP = 100,
	SOUND_MUSICSTOP,
	SOUND_MUSICONCE,
	SOUND_DING,
	SOUND_GOAL
};


/** prize constants */
enum
{
	/* pyconst: enum, "PRIZE_*" */
	PRIZE_RECHARGE = 1,
	PRIZE_ENERGY,
	PRIZE_ROTATION,
	PRIZE_STEALTH,
	PRIZE_CLOAK,
	PRIZE_XRADAR,
	PRIZE_WARP,
	PRIZE_GUN,
	PRIZE_BOMB,
	PRIZE_BOUNCE,
	PRIZE_THRUST,
	PRIZE_SPEED,
	PRIZE_FULLCHARGE,
	PRIZE_SHUTDOWN,
	PRIZE_MULTIFIRE,
	PRIZE_PROX,
	PRIZE_SUPER,
	PRIZE_SHIELD,
	PRIZE_SHRAP,
	PRIZE_ANTIWARP,
	PRIZE_REPEL,
	PRIZE_BURST,
	PRIZE_DECOY,
	PRIZE_THOR,
	PRIZE_MULTIPRIZE,
	PRIZE_BRICK,
	PRIZE_ROCKET,
	PRIZE_PORTAL
};


/** forward declaration for struct Arena */
typedef struct Arena Arena;
/** forward declaration for struct Player */
typedef struct Player Player;

/** this struct/union thing can be used to refer to a set of players */
typedef struct
{
	/** which type of target is this? */
	enum
	{
		T_NONE, /**< refers to no players */
		T_PLAYER, /**< refers to one single player (u.p must be filled in) */
		T_ARENA, /**< refers to a whole arena (u.arena must be filled in) */
		T_FREQ, /**< refers to one freq (u.freq must be filled in) */
		T_ZONE, /**< refers to the whole zone */
		T_LIST /**< refers to an arbitrary set of players (u.list) */
	} type;
	/** the union that contains the actual data */
	union
	{
		Player *p; /**< the player, if type == T_PLAYER */
		Arena *arena; /**< the arena, if type == T_ARENA */
		struct { Arena *arena; int freq; } freq;
			/**< the arena and freq, if type == T_FREQ */
		LinkedList list; /**< a list of players, if type == T_LIST */
	} u;
} Target;


#include "packets/types.h"

#include "packets/simple.h"


#endif

