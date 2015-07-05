
/* dist: public */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "asss.h"


local void Log(char, const char *, ...);
local void LogA(char level, const char *mod, Arena *arena, const char *format, ...);
local void LogP(char level, const char *mod, Player *p, const char *format, ...);
local int FilterLog(const char *, const char *);

local void * LoggingThread(void *);


local MPQueue queue;
local pthread_t thd;
local Imodman *mm;

/* don't load these during initialization, it would make a cycle. the
 * noinit is a hint to detect-cycles.py that these interfaces aren't
 * requested during initialization. */
local /* noinit */ Iconfig *cfg;

local Ilogman logint =
{
	INTERFACE_HEAD_INIT(I_LOGMAN, "logman")
	Log, LogA, LogP, FilterLog
};

EXPORT const char info_logman[] = CORE_MOD_INFO("logman");

EXPORT int MM_logman(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = NULL;
		MPInit(&queue);
		mm->RegInterface(&logint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pthread_create(&thd, NULL, LoggingThread, NULL);
	}
	else if (action == MM_PREUNLOAD)
	{
		mm->ReleaseInterface(cfg); cfg = NULL;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&logint, ALLARENAS))
			return MM_FAIL;
		MPAdd(&queue, NULL);
		pthread_join(thd, NULL);
		MPDestroy(&queue);
		return MM_OK;
	}
	return MM_FAIL;
}


void * LoggingThread(void *dummy)
{
	char *ll;

	for (;;)
	{
		ll = MPRemove(&queue);
		if (ll == NULL)
			return NULL;

		DO_CBS(CB_LOGFUNC, ALLARENAS, LogFunc, (ll));
		afree(ll);
	}
}


void Log(char level, const char *format, ...)
{
	int len, async = 1;
	va_list argptr;
	char buf[1024];

	if (level & L_SYNC)
	{
		level &= 0x7f;
		async = 0;
	}

	buf[0] = level;
	buf[1] = ' ';

	va_start(argptr, format);
	len = vsnprintf(buf+2, 1022, format, argptr);
	va_end(argptr);

	if (len > 0)
	{
		if (async)
			MPAdd(&queue, astrdup(buf));
		else
			DO_CBS(CB_LOGFUNC, ALLARENAS, LogFunc, (buf));
	}
}


void LogA(char level, const char *mod, Arena *a, const char *format, ...)
{
	int len, async = 1;
	va_list argptr;
	char buf[1024];

	if (level & L_SYNC)
	{
		level &= 0x7f;
		async = 0;
	}

	len = snprintf(buf, 256, "%c <%s> {%s} ",
			level,
			mod,
			a ? a->name : "(bad arena)");
	if (len > 255)
		len = 255;

	va_start(argptr, format);
	len += vsnprintf(buf + len, 1024 - len, format, argptr);
	va_end(argptr);

	if (len > 0)
	{
		if (async)
			MPAdd(&queue, astrdup(buf));
		else
			DO_CBS(CB_LOGFUNC, ALLARENAS, LogFunc, (buf));
	}
}

void LogP(char level, const char *mod, Player *p, const char *format, ...)
{
	int len, async = 1;
	Arena *arena;
	va_list argptr;
	char buf[1024], buf2[16];

	if (level & L_SYNC)
	{
		level &= 0x7f;
		async = 0;
	}

	if (!p)
		len = snprintf(buf, 256, "%c <%s> [(null player)] ",
				level,
				mod);
	else
	{
		char *name = p->name;

		if (name[0] == '\0')
		{
			name = buf2;
			snprintf(name, 16, "pid=%d", p->pid);
		}

		arena = p->arena;
		if (arena)
			len = snprintf(buf, 256, "%c <%s> {%s} [%s] ",
					level,
					mod,
					arena->name,
					name);
		else
			len = snprintf(buf, 256, "%c <%s> [%s] ",
					level,
					mod,
					name);
	}

	if (len > 255)
		len = 255;

	va_start(argptr, format);
	len += vsnprintf(buf + len, 1024 - len, format, argptr);
	va_end(argptr);

	if (len > 0)
	{
		if (async)
			MPAdd(&queue, astrdup(buf));
		else
			DO_CBS(CB_LOGFUNC, ALLARENAS, LogFunc, (buf));
	}
}



int FilterLog(const char *line, const char *modname)
{
	const char *res;
	char origin[32], level;

	/* if there's no config manager, disable filtering */
	if (!cfg || !line || !modname)
		return TRUE;

	level = line[0];
	line += 2;

	if (line[0] == '<')
	{
		/* copy into origin until closing > */
		char *d = origin;
		const char *s = line+1;
		while (*s && *s != '>' && (d-origin) < 30)
			*d++ = *s++;
		*d = 0;
	}
	else
	{
		/* unknown module */
		astrncpy(origin, "unknown", 32);
	}

	res = cfg->GetStr(GLOBAL, modname, origin);
	if (!res)
	{
		/* try 'all' */
		res = cfg->GetStr(GLOBAL, modname, "all");
	}
	if (!res)
	{
		/* if no match for 'all', disable filtering */
		return TRUE;
	}
	if (strchr(res, level))
		return TRUE;
	else
		return FALSE;
}


