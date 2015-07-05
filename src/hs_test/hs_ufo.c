#include "asss.h"

local Ilogman *lm;
local Inet *net;
local Icmdman *cmd;
local Ichat *chat;

local helptext_t ufo_help =
"Targets: player\n"
"Args: none\n"
"Toggles UFO on a player.\n";

local void Cufo(const char *command, const char *params, Player *p, const Target *target)
{
	unsigned char currentUFOStatus;

	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;

		currentUFOStatus = t->position.status & STATUS_UFO;

		if (t->pkt.ship != SHIP_SPEC)
		{
			struct SimplePacket ufo = { S2C_UFO, !currentUFOStatus };

			net->SendToOne(t, (byte*)&ufo, 2, NET_RELIABLE);

			chat->SendMessage(p, "UFO set to %d for %s", !currentUFOStatus, t->name);
		}
		else
		{
			chat->SendMessage(p, "Cannot set UFO on a spectator.");
		}
	}
}

EXPORT const char info_hs_ufo[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_ufo(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);

		if (!lm || !cmd || !chat || !net)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(net);
			return MM_FAIL;
		}

		cmd->AddCommand("ufo", Cufo, ALLARENAS, ufo_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("ufo", Cufo, ALLARENAS);


		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);

		return MM_OK;
	}
	return MM_FAIL;
}

