
/* dist: public */

#include <stdlib.h>

#include "asss.h"
#include "fake.h"

local Imodman *mm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Icmdman *cmd;
local Ichat *chat;
local Igame *game;
local Ifake *fake;


local void Ccreate(const char *command, const char *params, Player *p, const Target *target)
{
	Player *fakep;

	int specfreq = p->arena->specfreq;

	fakep = fake->CreateFakePlayer(params, p->arena, SHIP_SPEC, specfreq);
}


local void Cdestroy(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;

		fake->EndFaked(t);
	}
}

local void Csay(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;
		LinkedList set = LL_INITIALIZER;
		Link *link;
		Player *player;
		pd->Lock();
		FOR_EACH_PLAYER(player)
			if (player->status == S_PLAYING && player->arena == t->arena)
				LLAdd(&set, player);
		pd->Unlock();

		chat->SendAnyMessage(&set, MSG_PUB, 0, t, "%s", params);
	}
}

EXPORT const char info_hs_lanpic[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_lanpic(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);

		if (!pd || !cfg || !cmd || !chat || !game || !fake)
		{
			return MM_FAIL;
		}

		cmd->AddCommand("say", Csay, ALLARENAS, NULL);
		cmd->AddCommand("create", Ccreate, ALLARENAS, NULL);
		cmd->AddCommand("destroy", Cdestroy, ALLARENAS, NULL);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("say", Csay, ALLARENAS);
		cmd->RemoveCommand("create", Ccreate, ALLARENAS);
		cmd->RemoveCommand("destroy", Cdestroy, ALLARENAS);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(fake);
		return MM_OK;
	}
	return MM_FAIL;
}
