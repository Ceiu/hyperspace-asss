#include "asss.h"
#include "hscore.h"
#include "packets/ppk.h"

local Ilogman *lm;
local Inet *net;
local Igame *game;
local Ihscoredatabase *database;

local int edit_individual_ppk(Player *p, Player *t, struct C2SPosition *pos, int *extralen)
{
	if (p->p_freq != t->p_freq)
	{
		if (database)
			pos->bounty = database->getExp(p);
		else
			pos->bounty = 1337;

		return 1;
	}

	return 0;
}

local Appk myadv =
{
	ADVISER_HEAD_INIT(A_PPK)
	NULL, edit_individual_ppk
};

EXPORT const char info_hs_bountytest[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_bountytest(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !net || !game)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(game);
			mm->ReleaseInterface(database);
			return MM_FAIL;
		}

		mm->RegAdviser(&myadv, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregAdviser(&myadv, ALLARENAS);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	return MM_FAIL;
}

