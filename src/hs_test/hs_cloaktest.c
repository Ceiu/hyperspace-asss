#include "asss.h"

local Ilogman *lm;
local Inet *net;
local Icmdman *cmd;
local Ichat *chat;
local Igame *game;

local helptext_t cloak_help =
"Targets: player\n"
"Args: none\n"
"Experimental command.\n";

local void Ccloak(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER)
	{
		chat->SendMessage(p, "You must use ?cloak on a player");
	}
	else if (p->p_ship == SHIP_SPEC)
	{
		chat->SendMessage(p, "You cannot be in spec. Sorry.");
	}
	else
	{
		Player *t = target->u.p;

		struct S2CWeapons packet = {
			S2C_WEAPON, t->position.rotation, current_ticks() & 0xFFFF, t->position.x, t->position.yspeed,
			t->pid, t->position.xspeed, 0, t->position.status ^ STATUS_SAFEZONE, 0,
			t->position.y, t->position.bounty
		};

		game->DoWeaponChecksum(&packet);

		net->SendToOne(t, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
	}
}

EXPORT const char info_hs_cloaktest[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_cloaktest(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!lm || !cmd || !chat || !net || !game)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(game);
			return MM_FAIL;
		}

		cmd->AddCommand("cloak", Ccloak, ALLARENAS, cloak_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("cloak", Ccloak, ALLARENAS);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(game);

		return MM_OK;
	}
	return MM_FAIL;
}

