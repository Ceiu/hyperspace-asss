
/* dist: public */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <unistd.h>
#endif


#include "asss.h"

#include "pathutil.h"

#include "app.h"


/* structs */

struct Entry
{
	const char *keystr, *val, *info;
};

typedef struct ConfigFile
{
	pthread_mutex_t mutex; /* recursive */
	LinkedList handles;
	HashTable *table;
	StringChunk *strings;
	LinkedList dirty;
	int anychanged;
	time_t lastmod;
	char *filename, *arena, *name;
} ConfigFile;

struct ConfigHandle_
{
	ConfigFile *file;
	ConfigChangedFunc func;
	void *clos;
};


/* globals */

local ConfigHandle global;

local HashTable *opened;
local LinkedList files;
local pthread_mutex_t cfgmtx; /* protects opened and files */

local Imodman *mm;
local Ilogman *lm;
local Imainloop *ml;


/* functions */


/* escapes the config file syntactic characters. currently just = and \. */
local void escape_string(char *dst, int dstlen, const char *src)
{
	char *t = dst;
	while (*src && (t - dst + 2) < dstlen)
	{
		if (*src == '=' || *src == '\\')
			*t++ = '\\';
		*t++ = *src++;
	}
	*t = '\0';
}

local char * unescape_string(char *dst, int dstlen, char *src, char stopon)
{
	char *t = dst;
	while((t-dst+1) < dstlen &&
	      *src != '\0' &&
	      *src != stopon)
		if (*src == '\\')
		{
			src++;
			if (*src != '\0')
				*t++ = *src++;
		}
		else
			*t++ = *src++;
	*t = '\0';
	return src;
}


local ConfigFile *new_file()
{
	ConfigFile *f;
	pthread_mutexattr_t attr;

	f = amalloc(sizeof(*f));

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&f->mutex, &attr);
	pthread_mutexattr_destroy(&attr);

	LLInit(&f->handles);
	f->table = HashAlloc();
	f->strings = SCAlloc();
	LLInit(&f->dirty);
	f->anychanged = FALSE;

	return f;
}

local void free_file(ConfigFile *cf)
{
	if (LLCount(&cf->handles))
	{
		/* this can only happen when we're unloading this module, in
		 * which case it doesn't really matter if we delete these or
		 * not, but it's nice to be complete. */
		LLEnum(&cf->handles, afree);
		LLEmpty(&cf->handles);
	}

	SCFree(cf->strings);
	HashFree(cf->table);
	pthread_mutex_destroy(&cf->mutex);
	afree(cf->filename);
	afree(cf->arena);
	afree(cf->name);
	afree(cf);
}


/* call with cf->mutex held */
local void write_dirty_values_one(ConfigFile *cf, int call_cbs)
{
	char buf[CFG_MAX_LINE];
	FILE *fp;
	Link *l;

	l = LLGetHead(&cf->dirty);
	if (l)
	{
		if (lm)
			lm->Log(L_INFO, "<config> writing dirty settings: %s",
					cf->filename);

		if ((fp = fopen(cf->filename, "a")))
		{
			struct stat st;
			for (; l; l = l->next)
			{
				struct Entry *e = l->data;
				if (e->info) fprintf(fp, "; %s\n", e->info);
				/* escape keystr and val */
				escape_string(buf, sizeof(buf), e->keystr);
				fputs(buf, fp);
				fputs(" = ", fp);
				escape_string(buf, sizeof(buf), e->val);
				fputs(buf, fp);
				fputs("\n\n", fp);
				afree(e->keystr);
				afree(e->info);
				afree(e);
			}
			fclose(fp);
			/* set lastmod so that we don't think it's dirty */
			if (stat(cf->filename, &st) == 0)
				cf->lastmod = st.st_mtime;
		}
		else
			if (lm)
				lm->Log(L_WARN, "<config> failed to write dirty values: %s",
						cf->filename);

		LLEmpty(&cf->dirty);
	}

	if (cf->anychanged)
	{
		cf->anychanged = FALSE;
		/* call changed callbacks */
		for (l = LLGetHead(&cf->handles); call_cbs && l; l = l->next)
		{
			ConfigHandle ch = l->data;
			if (ch->func)
				ch->func(ch->clos);
		}
	}
}

local int write_dirty_values(void *dummy)
{
	Link *l, *next;

	pthread_mutex_lock(&cfgmtx);
	for (l = LLGetHead(&files); l; l = next)
	{
		ConfigFile *cf = l->data;
		next = l->next;

		pthread_mutex_lock(&cf->mutex);
		if (LLIsEmpty(&cf->handles))
		{
			write_dirty_values_one(cf, FALSE);
			pthread_mutex_unlock(&cf->mutex);

			HashRemove(opened, cf->filename, cf);
			LLRemove(&files, cf);
			free_file(cf);
		}
		else
		{
			write_dirty_values_one(cf, TRUE);
			pthread_mutex_unlock(&cf->mutex);
		}
	}
	pthread_mutex_unlock(&cfgmtx);

	return TRUE;
}

local void FlushDirtyValues(void)
{
	write_dirty_values(NULL);
}


local void report_error(const char *error)
{
	if (lm)
		lm->Log(L_WARN, "<config> %s", error);
	else
		fprintf(stderr, "W <config> %s\n", error);
}

local int locate_config_file(char *dest, int destlen, const char *arena, const char *name)
{
	const char *path = CFG_CONFIG_SEARCH_PATH;
	struct replace_table repls[] =
		{ { 'n', name }, { 'b', arena } };

	if (!name)
		repls[0].with = arena ? "arena.conf" : "global.conf";

	return find_file_on_path(dest, destlen, path, repls, arena ? 3 : 1);
}


#define LINESIZE CFG_MAX_LINE

/* call with cf->mutex held */
local void do_load(ConfigFile *cf, const char *arena, const char *name)
{
	APPContext *ctx;
	char line[LINESIZE], *buf, *t;
	char key[MAXSECTIONLEN+MAXKEYLEN+3], *thespot = key;
	char val[LINESIZE];

	ctx = APPInitContext(locate_config_file, report_error, arena);

	/* the actual file */
	APPAddFile(ctx, name);

	while (APPGetLine(ctx, line, LINESIZE))
	{
		buf = line;
		/* kill leading spaces */
		while (*buf != '\0' && isspace(*buf)) buf++;
		/* kill trailing spaces */
		t = buf + strlen(buf) - 1;
		while (t >= buf && isspace(*t)) t--;
		*++t = 0;

		if (*buf == '[')
		{
			/* new section: copy to key name */
			/* skip leading brackets/spaces */
			while (*buf == '[' || isspace(*buf)) buf++;
			/* get rid of trailing spaces or brackets */
			t = buf + strlen(buf) - 1;
			while (*t == ']' || isspace(*t)) *t-- = 0;
			/* copy section name into key */
			strncpy(key, buf, MAXSECTIONLEN);
			strcat(key, ":");
			thespot = key + strlen(key);
		}
		else
		{
			thespot[0] = '\0';
			buf = unescape_string(thespot, MAXKEYLEN, buf, '=');

			/* empty key */
			if (thespot[0] == '\0')
				continue;

			if (*buf == '=')
			{
				const char *data;

				/* kill trailing whitespace on the key */
				t = thespot + strlen(thespot) - 1;
				while (t >= thespot && isspace(*t)) *t-- = 0;
				/* kill whitespace before value */
				buf++;
				while (isspace(*buf)) buf++;

				unescape_string(val, sizeof(val), buf, 0);

				data = SCAdd(cf->strings, val);

				if (strchr(thespot, ':'))
					/* this syntax lets you specify a section and key on
					 * one line. it does _not_ modify the "current
					 * section" */
					HashReplace(cf->table, thespot, data);
				else if (thespot > key)
					HashReplace(cf->table, key, data);
				else
					report_error("ignoring value not in any section");
			}
			else
				/* there is no value for this key, so enter it with the
				 * empty string. */
				HashReplace(cf->table, key, "");
		}
	}

	APPFreeContext(ctx);
}


/* call with cf->mutex not held */
local void reload_file(ConfigFile *cf)
{
	struct stat st;
	Link *l;

	if (lm)
		lm->Log(L_INFO, "<config> reloading file from disk: %s",
				cf->filename);

	pthread_mutex_lock(&cf->mutex);

	/* just in case */
	write_dirty_values_one(cf, FALSE);

	/* free this stuff, then create it again */
	SCFree(cf->strings);
	HashFree(cf->table);
	cf->table = HashAlloc();
	cf->strings = SCAlloc();

	/* now load file again */
	do_load(cf, cf->arena, cf->name);

	cf->lastmod = stat(cf->filename, &st) == 0 ? st.st_mtime : 0;

	/* call changed callbacks */
	for (l = LLGetHead(&cf->handles); l; l = l->next)
	{
		ConfigHandle ch = l->data;
		if (ch->func)
			ch->func(ch->clos);
	}

	pthread_mutex_unlock(&cf->mutex);
}

local void ReloadConfigFile(ConfigHandle ch)
{
	reload_file(ch->file);
}

local ConfigHandle new_handle(ConfigFile *cf, ConfigChangedFunc func, void *clos)
{
	ConfigHandle ch = amalloc(sizeof(*ch));
	ch->file = cf;
	ch->func = func;
	ch->clos = clos;
	pthread_mutex_lock(&cf->mutex);
	LLAdd(&cf->handles, ch);
	pthread_mutex_unlock(&cf->mutex);
	return ch;
}

local ConfigHandle AddRef(ConfigHandle ch)
{
	return new_handle(ch->file, NULL, NULL);
}


struct maybe_reload_data
{
	const char *pathname;
	void (*callback)(const char *pathname, void *clos);
	void *clos;
};

local int maybe_reload_files(void *v)
{
	struct maybe_reload_data *data = v;
	Link *l;
	ConfigFile *cf;
	struct stat st;

	pthread_mutex_lock(&cfgmtx);
	for (l = LLGetHead(&files); l; l = l->next)
	{
		cf = l->data;
		if (data ?
		    (!data->pathname || strstr(cf->filename, data->pathname)) :
		    (stat(cf->filename, &st) == 0 && st.st_mtime != cf->lastmod))
		{
			/* this calls changed callbacks */
			reload_file(cf);
			/* this calls informative callbacks to let the caller know
			 * which files are being reloaded */
			if (data && data->callback)
				data->callback(cf->filename, data->clos);
		}
	}
	pthread_mutex_unlock(&cfgmtx);

	return TRUE;
}

local void CheckModifiedFiles(void)
{
	maybe_reload_files(NULL);
}

local void ForceReload(const char *pathname,
		void (*cb)(const char *path, void *clos), void *clos)
{
	struct maybe_reload_data data = { pathname, cb, clos };
	maybe_reload_files(&data);
}


local ConfigHandle OpenConfigFile(const char *arena, const char *name,
		ConfigChangedFunc func, void *clos)
{
	ConfigHandle ch;
	ConfigFile *cf;
	char fname[PATH_MAX];
	struct stat st;

	/* make sure at least the base file exists */
	if (locate_config_file(fname, sizeof(fname), arena, name) == -1)
		return NULL;

	/* first try to get it out of the table */
	pthread_mutex_lock(&cfgmtx);
	cf = HashGetOne(opened, fname);
	if (!cf)
	{
		/* if not, make a new one */
		cf = new_file();
		cf->filename = astrdup(fname);
		cf->arena = astrdup(arena);
		cf->name = astrdup(name);
		cf->lastmod = stat(fname, &st) == 0 ? st.st_mtime : 0;

		/* load the settings */
		do_load(cf, arena, name);

		/* add this to the opened table */
		HashAdd(opened, fname, cf);
		LLAdd(&files, cf);
	}
	/* create handle while holding cfgmtx so that the file doesn't get
	 * garbage collected before it has a reference */
	ch = new_handle(cf, func, clos);
	pthread_mutex_unlock(&cfgmtx);

	return ch;
}

local void CloseConfigFile(ConfigHandle ch)
{
	int removed;

	ConfigFile *cf;
	if (!ch) return;
	cf = ch->file;

	pthread_mutex_lock(&cf->mutex);
	removed = LLRemove(&cf->handles, ch);
	pthread_mutex_unlock(&cf->mutex);
	assert(removed);
	afree(ch);
}


local const char *GetStr(ConfigHandle ch, const char *sec, const char *key)
{
	char keystring[MAXSECTIONLEN+MAXKEYLEN+2];
	const char *res;
	ConfigFile *cf;

	if (!ch) return NULL;
	if (ch == GLOBAL) ch = global;
	cf = ch->file;

	pthread_mutex_lock(&cf->mutex);
	if (sec && key)
	{
		snprintf(keystring, MAXSECTIONLEN+MAXKEYLEN+1, "%s:%s", sec, key);
		res = HashGetOne(cf->table, keystring);
	}
	else if (sec)
		res = HashGetOne(cf->table, sec);
	else if (key)
		res = HashGetOne(cf->table, key);
	else
		res = NULL;
	pthread_mutex_unlock(&cf->mutex);

	return res;
}

local int GetInt(ConfigHandle ch, const char *sec, const char *key, int def)
{
	char *next;
	const char *str;
	int ret;

	str = GetStr(ch, sec, key);
	if (!str) return def;
	ret = strtol(str, &next, 0);
	return str != next ? ret : str[0] == 'y' || str[0] == 'Y';
}


local void SetStr(ConfigHandle ch, const char *sec, const char *key,
		const char *val, const char *info, int perm)
{
	char keystring[MAXSECTIONLEN+MAXKEYLEN+2];
	const char *res, *data;
	ConfigFile *cf;

	if (!ch || !val) return;
	if (ch == GLOBAL) ch = global;
	cf = ch->file;

	if (sec && key)
		snprintf(keystring, MAXSECTIONLEN+MAXKEYLEN+1, "%s:%s", sec, key);
	else if (sec)
		astrncpy(keystring, sec, MAXSECTIONLEN+MAXKEYLEN+1);
	else if (key)
		astrncpy(keystring, key, MAXSECTIONLEN+MAXKEYLEN+1);
	else
		return;

	pthread_mutex_lock(&cf->mutex);

	/* check it against the current value */
	res = HashGetOne(cf->table, keystring);
	if (res && !strcmp(res, val))
	{
		pthread_mutex_unlock(&cf->mutex);
		return;
	}

	data = SCAdd(cf->strings, val);
	HashReplace(cf->table, keystring, data);
	cf->anychanged = TRUE;
	if (perm)
	{
		/* make a dirty list entry for it */
		struct Entry *e = amalloc(sizeof(*e));
		e->keystr = astrdup(keystring);
		e->info = astrdup(info);
		e->val = data;
		LLAdd(&cf->dirty, e);
	}
	pthread_mutex_unlock(&cf->mutex);
}

local void SetInt(ConfigHandle ch, const char *sec, const char *key,
		int value, const char *info, int perm)
{
	char num[16];
	snprintf(num, 16, "%d", value);
	SetStr(ch, sec, key, num, info, perm);
}


local void set_timers()
{
	int dirty, files;

	/* cfghelp: Config:FlushDirtyValuesInterval, global, int, def: 500
	 * How often to write modified config settings back to disk (in
	 * ticks). */
	dirty = GetInt(global, "Config", "FlushDirtyValuesInterval", 500);
	/* cfghelp: Config:CheckModifiedFilesInterval, global, int, def: 1500
	 * How often to check for modified config files on disk (in ticks). */
	files = GetInt(global, "Config", "CheckModifiedFilesInterval", 1500);

	ml->ClearTimer(write_dirty_values, NULL);
	if (dirty)
		ml->SetTimer(write_dirty_values, 700, dirty, NULL, NULL);

	ml->ClearTimer(maybe_reload_files, NULL);
	if (files)
		ml->SetTimer(maybe_reload_files, 1500, files, NULL, NULL);
}


local void global_changed(void *dummy)
{
	DO_CBS(CB_GLOBALCONFIGCHANGED, ALLARENAS, GlobalConfigChangedFunc, ());
	/* we'd like to call this, in case this setting change changed these
	 * timers, but unfortunately we can't, because we're getting called
	 * _from_ one of these timer events, and you can't cancel a timer
	 * from inside of it, at least with the current timer interface. if
	 * it changes, this might be re-enabled:
	 * set_timers();
	 */
}



/* interface */

local Iconfig _int =
{
	INTERFACE_HEAD_INIT(I_CONFIG, "config-file")
	GetStr, GetInt, SetStr, SetInt,
	OpenConfigFile, CloseConfigFile, ReloadConfigFile,
	AddRef,
	FlushDirtyValues, CheckModifiedFiles, ForceReload,
	{
		"Warbird",
		"Javelin",
		"Spider",
		"Leviathan",
		"Terrier",
		"Weasel",
		"Lancaster",
		"Shark"
	}
};

EXPORT const char info_config[] = CORE_MOD_INFO("config");

EXPORT int MM_config(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!ml) return MM_FAIL;

		LLInit(&files);
		opened = HashAlloc();

		pthread_mutex_init(&cfgmtx, NULL);

		global = OpenConfigFile(NULL, NULL, global_changed, NULL);
		if (!global) return MM_FAIL;

		lm = NULL;

		set_timers();

		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
	}
	else if (action == MM_PREUNLOAD)
	{
		mm->ReleaseInterface(lm);
		lm = NULL;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		CloseConfigFile(global);

		ml->ClearTimer(write_dirty_values, NULL);
		ml->ClearTimer(maybe_reload_files, NULL);
		mm->ReleaseInterface(ml);

		{
			/* free existing files */
			Link *l;
			for (l = LLGetHead(&files); l; l = l->next)
			{
				write_dirty_values_one(l->data, FALSE);
				free_file(l->data);
			}
			LLEmpty(&files);
			HashFree(opened);
		}

		pthread_mutex_destroy(&cfgmtx);

		return MM_OK;
	}
	return MM_FAIL;
}

