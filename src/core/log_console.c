
/* dist: public */

#include <stdio.h>

#include "asss.h"

local Ilogman *lm;

local void LogConsole(char *s)
{
	if (lm->FilterLog(s, "log_console"))
		puts(s);
}

EXPORT const char info_log_console[] = CORE_MOD_INFO("log_console");

EXPORT int MM_log_console(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!lm) return MM_FAIL;
		mm->RegCallback(CB_LOGFUNC, LogConsole, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_LOGFUNC, LogConsole, ALLARENAS);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

