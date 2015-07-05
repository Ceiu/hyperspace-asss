#include <stdlib.h>

#include "asss.h"
#include "selfpos.h"

local Iselfpos *selfpos;
local Icmdman *cmd;
local Ichat *chat;

local helptext_t warpself_help =
"Targets: player\n"
"Args: <xcoord> <ycoord>\n"
"Warps target player to coordinate x,y with no speed loss\n";

local void Cwarpself(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		char *next;
		int x, y;
		Player *t = target->u.p;

		x = strtol(params, &next, 0);
		if (next == params)
		{
			chat->SendMessage(p, "Usage: ?warpself <x> <y>");
			return;
		}
		while (*next == ',' || *next == ' ') next++;
		y = strtol(next, NULL, 0);

		selfpos->WarpPlayer(t, x, y, t->position.xspeed, t->position.yspeed, t->position.rotation, 0);
	}
}

local helptext_t warpweap_help =
"Targets: player\n"
"Args: <weap>\n"
"Warps target player with a weapon.\n"
"1 = bullet, 2 = bounce, 3 = bomb, 4 = prox\n"
"5 = repel, 6 = decoy, 7 = burst, 8 = thor\n";

local void Cwarpweap(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		int weap = atoi(params);
		struct Weapons weapon = {
			weap, 0, 0, 0, 0, 0
		};
		Player *t = target->u.p;

		if (weap == 0)
		{
			chat->SendMessage(p, "Usage: ?warpweap <weap>");
			return;
		}

		selfpos->WarpPlayerWithWeapon(t, t->position.x, t->position.y,
				t->position.xspeed, t->position.yspeed,
				t->position.rotation, 0, &weapon);
	}
}

EXPORT const char info_selfpos_cmd[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_selfpos_cmd(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!cmd || !selfpos || !chat)
		{
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(selfpos);
			mm->ReleaseInterface(chat);
			return MM_FAIL;
		}

		cmd->AddCommand("warpself", Cwarpself, ALLARENAS, warpself_help);
		cmd->AddCommand("warpweap", Cwarpweap, ALLARENAS, warpweap_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("warpweap", Cwarpweap, ALLARENAS);
		cmd->RemoveCommand("warpself", Cwarpself, ALLARENAS);

		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(chat);

		return MM_OK;
	}
	return MM_FAIL;
}

