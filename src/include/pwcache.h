
/* dist: public */

#ifndef __PWCACHE_H
#define __PWCACHE_H

#define I_PWCACHE "pwcache-2"

enum PWCacheResult
{
	MATCH,      /* player has entry, matches */
	MISMATCH,   /* player has entry, but wrong password */
	NOT_FOUND   /* player has no entry in cache */
};

typedef struct Ipwcache
{
	INTERFACE_HEAD_DECL

	void (*Set)(const char *name, const char *pwd);
	void (*Check)(const char *name, const char *pwd,
			void (*done)(void *clos, int result), void *clos);
} Ipwcache;

#endif

