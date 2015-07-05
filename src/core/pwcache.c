
/* dist: public */

#include <string.h>

#include "asss.h"
#include "persist.h"
#include "pwcache.h"
#include "md5.h"

#define KEY_PWCACHE 0x5057
#define NAMELEN 24
#define PWLEN 24
#define HASHLEN 16


local Ipersist *persist;


local void hash_password(const char *name, const char *pwd,
		unsigned char out[HASHLEN], char work[NAMELEN+PWLEN])
{
	struct MD5Context ctx;

	memset(work, 0, NAMELEN+PWLEN);
	astrncpy(work, name, NAMELEN);
	ToLowerStr(work);
	astrncpy(work + NAMELEN, pwd, PWLEN);

	MD5Init(&ctx);
	MD5Update(&ctx, (unsigned char *)work, NAMELEN+PWLEN);
	MD5Final(out, &ctx);
}


local void Set(const char *name, const char *pwd)
{
	unsigned char hash[HASHLEN];
	char work[NAMELEN+PWLEN];
	hash_password(name, pwd, hash, work);
	persist->PutGeneric(
			KEY_PWCACHE,
			work, NAMELEN,
			hash, HASHLEN,
			NULL, NULL);
}


typedef struct check_state_t
{
	unsigned char given[HASHLEN];
	unsigned char expected[HASHLEN];
	void (*done)(void *clos, int result);
	void *clos;
} check_state_t;

local void check_done(void *clos, int present)
{
	check_state_t *st = clos;
	if (present)
	{
		if (memcmp(st->given, st->expected, HASHLEN) == 0)
			st->done(st->clos, MATCH);
		else
			st->done(st->clos, MISMATCH);
	}
	else
		st->done(st->clos, NOT_FOUND);
	afree(st);
}

local void Check(const char *name, const char *pwd,
		void (*done)(void *clos, int success), void *clos)
{
	char work[NAMELEN+PWLEN];
	check_state_t *st = amalloc(sizeof(*st));
	st->done = done;
	st->clos = clos;
	hash_password(name, pwd, st->given, work);
	persist->GetGeneric(
			KEY_PWCACHE,
			work, NAMELEN,
			st->expected, HASHLEN,
			check_done, st);
}

local Ipwcache pwint =
{
	INTERFACE_HEAD_INIT(I_PWCACHE, "pwcache")
	Set, Check
};

EXPORT const char info_pwcache[] = CORE_MOD_INFO("pwcache");

EXPORT int MM_pwcache(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (!persist)
			return MM_FAIL;
		mm->RegInterface(&pwint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&pwint, ALLARENAS))
			return MM_FAIL;
		return MM_OK;
	}
	return MM_FAIL;
}

