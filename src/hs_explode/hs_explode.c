#include "asss.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Icmdman *cmd;
local Inet *net;
local Igame *game;

local helptext_t explodeHelp =
"Targets: player\n"
"Args: none\n"
"KABOOM!\n";

local void explodeCommand(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER)
	{
		chat->SendMessage(p, "You must use ?explode on a player");
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
			p->pid, t->position.xspeed, 0, p->position.status, 0,
			t->position.y, p->position.bounty
		};

		struct Weapons weapon = {
			W_THOR,	//u16 type,;
			3,		//u16 level : 2;
			0,		//u16 shrapbouncing : 1;
			0,		//u16 shraplevel : 2;
			0,		//u16 shrap : 5;
			0		//u16 alternate : 1;
		};

		packet.weapon = weapon;

		game->DoWeaponChecksum(&packet);

		net->SendToOne(t, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
	}
}

EXPORT const char info_hs_explode[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_explode(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!lm || !chat || !cmd || !net || !game)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(game);

			return MM_FAIL;
		}

		cmd->AddCommand("explode", explodeCommand, ALLARENAS, explodeHelp);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("explode", explodeCommand, ALLARENAS);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(game);

		return MM_OK;
	}
	return MM_FAIL;
}
