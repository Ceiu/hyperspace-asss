/*
 * localtime
 * displays the local time of the asss server
 *
 * based off smong's moveto.c (coz i'm too dumb to figure out the whole thing)
 *
 */

#include <stdlib.h>
#include <time.h>
#include "asss.h"

local Imodman *mm;
local Inet *net;
local Icmdman *cmd;
local Ichat *chat;

local helptext_t localtime_help =
"Targets: none\n"
"Args: none\n"
"Displays the server's local time.\n";

local void Clocaltime(const char *command, const char *params, Player *p, const Target *target)
{
	time_t t1;
	char time_output[230];

	time(&t1);

	const char *format = "%a, %d %b %Y %H:%M:%S %Z";


	if (*params)
	{
		format = params;
	}

	strftime(time_output, sizeof(time_output), format, localtime(&t1));

	time_output[229] = '\0';

	chat->SendMessage(p, "%s", time_output);
}

EXPORT const char info_localtime[] = "localtime by edtheinvincible, modified by Dr Brain";

EXPORT int MM_localtime(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!net || !cmd || !chat) return MM_FAIL;

		cmd->AddCommand("localtime", Clocaltime, ALLARENAS, localtime_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("localtime", Clocaltime, ALLARENAS);

		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);

		return MM_OK;
	}

	return MM_FAIL;
}
