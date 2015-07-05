#include "asss.h"

local Ilogman *lm;
local Imapdata *mapdata;
local Icmdman *cmd;
local Ichat *chat;

local helptext_t crash_help =
"Targets: none\n"
"Args: <region>\n"
"Crashes the zone with divide by zero.\n";

local void Ccrash(const char *command, const char *params, Player *p, const Target *target)
{
	volatile int i = *((int*)NULL);
	(void)i;
}

EXPORT const char info_hs_crash[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_crash(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);

		if (!lm || !cmd || !chat || !mapdata)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(mapdata);
			return MM_FAIL;
		}

		cmd->AddCommand("crash", Ccrash, ALLARENAS, crash_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("crash", Ccrash, ALLARENAS);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(mapdata);

		return MM_OK;
	}
	return MM_FAIL;
}

