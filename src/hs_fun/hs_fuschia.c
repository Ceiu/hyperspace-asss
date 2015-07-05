#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"

// 79 is best
// 33, 44-51
// 75-79,86,88,93,95,100,105,117
// 125,141,143,159,162,165
// 179,183,198,203,212,219,222
// 228,236

//modules
local Imodman *mm;
local Ichat *chat;
local Icmdman *cmd;
local Iplayerdata *pd;

local void send_message(Arena *arena, const char *message)
{
	Link *link;
	Player *p;

	LinkedList standard, chatnet;
	
	LLInit(&standard);
	LLInit(&chatnet);

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		if (p->arena == arena)
		{
			if (IS_STANDARD(p))
				LLAdd(&standard, p);
			else if (IS_CHAT(p))
				LLAdd(&chatnet, p);
		}
	}
	pd->Unlock();

	chat->SendAnyMessage(&standard, 79, 0, NULL, "%s", message);
	chat->SendAnyMessage(&chatnet, MSG_ARENA, 0, NULL, "<FUSCHIA> %s", message);

	LLEmpty(&standard);
	LLEmpty(&chatnet);
}

local void Cfuschiaa(const char *command, const char *params, Player *p, const Target *target)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s  -%s", params, p->name);
	send_message(p->arena, buf);
}

local void Cfuschiaaa(const char *command, const char *params, Player *p, const Target *target)
{
	char buf[256];
	snprintf(buf, sizeof(buf), "%s", params);
	send_message(p->arena, buf);
}

EXPORT const char info_hs_fuschia[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_fuschia(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

		if (!chat || !cmd || !pd)
		{
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(pd);

			return MM_FAIL;
		}

		cmd->AddCommand("fuschiaaa", Cfuschiaaa, ALLARENAS, NULL);
		cmd->AddCommand("fuschiaa", Cfuschiaa, ALLARENAS, NULL);
		
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);

		cmd->RemoveCommand("fuschiaaa", Cfuschiaaa, ALLARENAS);
		cmd->RemoveCommand("fuschiaa", Cfuschiaa, ALLARENAS);
		
		return MM_OK;
	}
	return MM_FAIL;
}
