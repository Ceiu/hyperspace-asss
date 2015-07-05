
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"


/* structs */
typedef struct cmddata_t
{
	CommandFunc func;
	Arena *arena;
	helptext_t helptext;
} cmddata_t;


/* prototypes */

local void AddCommand(const char *, CommandFunc, Arena *, helptext_t);
local void RemoveCommand(const char *, CommandFunc, Arena *);
local void Command(const char *, Player *, const Target *, int);
local helptext_t GetHelpText(const char *, Arena *);
local void AddUnlogged(const char *);
local void RemoveUnlogged(const char *);
local void init_dontlog(void);
local void uninit_dontlog(void);

/* static data */
local Iplayerdata *pd;
local Ilogman *lm;
local Icapman *capman;
local Iconfig *cfg;
local Imodman *mm;

local pthread_mutex_t cmdmtx;
local HashTable *cmds;
local HashTable *dontlog_table;
local CommandFunc defaultfunc;

local Icmdman _int =
{
	INTERFACE_HEAD_INIT(I_CMDMAN, "cmdman")
	AddCommand, RemoveCommand,
	Command, GetHelpText,
	AddUnlogged, RemoveUnlogged
};

EXPORT const char info_cmdman[] = CORE_MOD_INFO("cmdman");

EXPORT int MM_cmdman(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&cmdmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		cmds = HashAlloc();
		init_dontlog();

		defaultfunc = NULL;

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		uninit_dontlog();
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(cfg);
		HashFree(cmds);
		pthread_mutex_destroy(&cmdmtx);
		return MM_OK;
	}
	return MM_FAIL;
}


void AddCommand(const char *cmd, CommandFunc f, Arena *arena,
		helptext_t helptext)
{
	if (!cmd)
		defaultfunc = f;
	else
	{
		cmddata_t *data = amalloc(sizeof(*data));
		data->func = f;
		data->arena = arena;
		data->helptext = helptext;
		pthread_mutex_lock(&cmdmtx);
		HashAdd(cmds, cmd, data);
		pthread_mutex_unlock(&cmdmtx);
	}
}


void RemoveCommand(const char *cmd, CommandFunc f, Arena *arena)
{
	if (!cmd)
	{
		if (defaultfunc == f)
			defaultfunc = NULL;
	}
	else
	{
		LinkedList lst = LL_INITIALIZER;
		Link *l;

		pthread_mutex_lock(&cmdmtx);
		HashGetAppend(cmds, cmd, &lst);
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			cmddata_t *data = l->data;
			if (data->func == f && data->arena == arena)
			{
				HashRemove(cmds, cmd, data);
				pthread_mutex_unlock(&cmdmtx);
				LLEmpty(&lst);
				afree(data);
				return;
			}
		}
		pthread_mutex_unlock(&cmdmtx);
		LLEmpty(&lst);
	}
}


local void init_dontlog()
{
	dontlog_table = HashAlloc();

	/* billing commands that shouldn't be logged */
	HashAdd(dontlog_table, "chat", (void*)1);
	HashAdd(dontlog_table, "password", (void*)1);
	HashAdd(dontlog_table, "squadcreate", (void*)1);
	HashAdd(dontlog_table, "squadjoin", (void*)1);
	HashAdd(dontlog_table, "addop", (void*)1);
	HashAdd(dontlog_table, "adduser", (void*)1);
	HashAdd(dontlog_table, "changepassword", (void*)1);
	HashAdd(dontlog_table, "login", (void*)1);
	HashAdd(dontlog_table, "blogin", (void*)1);
	HashAdd(dontlog_table, "bpassword", (void*)1);
	HashAdd(dontlog_table, "squadpassword", (void*)1);
	HashAdd(dontlog_table, "message", (void*)1);
}


local void uninit_dontlog()
{
	HashFree(dontlog_table);
}


local inline int dontlog(const char *cmd)
{
	void *result = HashGetOne(dontlog_table, cmd);
	return result == (void*)1;
}


local void AddUnlogged(const char *cmdname)
{
	HashAdd(dontlog_table, cmdname, (void*)1);
}


local void RemoveUnlogged(const char *cmdname)
{
	HashRemove(dontlog_table, cmdname, (void*)1);
}


local void log_command(Player *p, const Target *target, const char *cmd, const char *params)
{
	char t[32];

	if (!lm) return;

	/* don't log the params to some commands */
	if (dontlog(cmd))
		params = "...";

	if (target->type == T_ARENA)
		astrncpy(t, "(arena)", 32);
	else if (target->type == T_FREQ)
		snprintf(t, 32, "(freq %d)", target->u.freq.freq);
	else if (target->type == T_PLAYER)
		snprintf(t, 32, "to [%s]", target->u.p->name);
	else
		astrncpy(t, "(other)", 32);

	if (*params)
		lm->LogP(L_INFO, "cmdman", p, "command %s: %s %s",
				t, cmd, params);
	else
		lm->LogP(L_INFO, "cmdman", p, "command %s: %s",
				t, cmd);
}


local int allowed(Player *p, const char *cmd, const char *prefix,
		Arena *remarena)
{
	char cap[40];

	if (!capman)
	{
#ifdef ALLOW_ALL_IF_CAPMAN_IS_MISSING
		lm->Log(L_WARN, "<cmdman> the capability manager isn't loaded, allowing all commands");
		return TRUE;
#else
		lm->Log(L_WARN, "<cmdman> the capability manager isn't loaded, disallowing all commands");
		return FALSE;
#endif
	}

	snprintf(cap, sizeof(cap), "%s_%s", prefix, cmd);
	if (remarena)
		return capman->HasCapabilityInArena(p, remarena, cap);
	else
		return capman->HasCapability(p, cap);
}


void Command(const char *line, Player *p, const Target *target, int sound)
{
	LinkedList lst = LL_INITIALIZER;
	char cmd[40], *t;
	int skiplocal = FALSE;
	const char *origline, *prefix;
	Arena *remarena = NULL;

	/* almost all commands assume p->arena is non-null */
	if (p->arena == NULL)
		return;

	if (line[0] == '\\')
	{
		line++;
		skiplocal = TRUE;
	}
	origline = line;

	/* find end of command */
	t = cmd;
	while (*line && *line != ' ' && *line != '=' && (t-cmd) < 30)
		*t++ = *line++;
	/* close it off and add sound hack */
	*t++ = 0;
	*t++ = (char)sound;
	/* skip spaces */
	while (*line && (*line == ' ' || *line == '='))
		line++;

	if (target->type == T_ARENA || target->type == T_NONE)
		prefix = "cmd";
	else if (target->type == T_PLAYER)
	{
		if (target->u.p->arena == p->arena)
			prefix = "privcmd";
		else
		{
			remarena = target->u.p->arena;
			prefix = "rprivcmd";
		}
	}
	else
		prefix = "privcmd";

	pthread_mutex_lock(&cmdmtx);
	HashGetAppend(cmds, cmd, &lst);

	if (skiplocal || LLIsEmpty(&lst))
	{
		/* we don't know about this, send it to the biller */
		if (defaultfunc)
			defaultfunc(cmd, origline, p, target);
	}
	else if (allowed(p, cmd, prefix, remarena))
	{
		Link *l;
		log_command(p, target, cmd, line);
		for (l = LLGetHead(&lst); l; l = l->next)
		{
			cmddata_t *data = l->data;
			if (data->arena != ALLARENAS && data->arena != p->arena)
				continue;
			else if (data->func)
				data->func(cmd, line, p, target);
		}
	}
#ifdef CFG_LOG_ALL_COMMAND_DENIALS
	else
		lm->Log(L_DRIVEL, "<cmdman> [%s] permission denied for %s",
				p->name, cmd);
#endif

	pthread_mutex_unlock(&cmdmtx);
	LLEmpty(&lst);
}


helptext_t GetHelpText(const char *cmd, Arena *a)
{
	helptext_t ret = NULL;
	LinkedList lst = LL_INITIALIZER;
	Link *l;

	pthread_mutex_lock(&cmdmtx);
	HashGetAppend(cmds, cmd, &lst);
	for (l = LLGetHead(&lst); l; l = l->next)
	{
		cmddata_t *cd = l->data;
		if (cd->arena == ALLARENAS || cd->arena == a)
			ret = cd->helptext;
	}
	pthread_mutex_unlock(&cmdmtx);

	return ret;
}

