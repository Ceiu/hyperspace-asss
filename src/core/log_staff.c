
/* dist: public */

#include <string.h>
#include <stdlib.h>

#include "asss.h"


local Iconfig *cfg;
local Ichat *chat;

local HashTable *cmdstolog;
local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;


local void log_staff(const char *line)
{
	const char *t;
	char buf[64];

	/* quick filter to get rid of most lines */
	if (strncmp(line, "I <cmdman> {", 12) != 0)
		return;

	/* look for command name */
	t = strchr(line, ':');
	if (!t || t[1] != ' ')
		return;

	delimcpy(buf, t+2, sizeof(buf), ' ');

	pthread_mutex_lock(&mtx);
	t = HashGetOne(cmdstolog, buf);
	pthread_mutex_unlock(&mtx);

	if (t) chat->SendModMessage("%s", line+11);
}


local void init_commands(void)
{
	const char *cmds_str;
	char buf[64];
	const char *tmp = NULL;

	pthread_mutex_lock(&mtx);

	if (cmdstolog)
		HashFree(cmdstolog);

	cmdstolog = HashAlloc();

	/* cfghelp: log_staff:commands, arena, string, \
	 * def: 'warn kick setcm', mod: log_staff
	 * A list of commands that trigger messages to all logged-in staff. */
	cmds_str = cfg->GetStr(GLOBAL, "log_staff", "commands");
	if (!cmds_str)
		cmds_str = "warn kick setcm";

	while (strsplit(cmds_str, " ,:;", buf, sizeof(buf), &tmp))
		HashReplace(cmdstolog, ToLowerStr(buf), (const void*)1);

	pthread_mutex_unlock(&mtx);
}

EXPORT const char info_log_staff[] = CORE_MOD_INFO("log_staff");

EXPORT int MM_log_staff(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		if (!cfg || !chat) return MM_FAIL;
		init_commands();
		mm->RegCallback(CB_LOGFUNC, log_staff, ALLARENAS);
		mm->RegCallback(CB_GLOBALCONFIGCHANGED, init_commands, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_LOGFUNC, log_staff, ALLARENAS);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		HashFree(cmdstolog);
		cmdstolog = NULL;
		return MM_OK;
	}
	return MM_FAIL;
}

