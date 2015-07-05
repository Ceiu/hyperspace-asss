#include <stdlib.h>

#include "asss.h"
#include "hscore.h"

/* structs */
#include "packets/brick.h"

//modules
local Imodman *mm;
local Ichat *chat;
local Icmdman *cmd;
local Inet *net;

local void Ctest(const char *command, const char *params, Player *p, const Target *target)
{
	struct S2CBrickPacket pkt;

	pkt.type = S2C_BRICK;
	pkt.x1 = (p->position.x >> 4) - 3;
	pkt.y1 = (p->position.y >> 4) - 3;
	pkt.x2 = (p->position.x >> 4) + 3;
	pkt.y2 = (p->position.y >> 4) + 3;
	pkt.freq = p->p_freq;
	pkt.brickid = 1337;
	pkt.starttime = current_ticks() - 1500;

	net->SendToArena(p->arena, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE | NET_URGENT);
}

EXPORT const char info_hs_test[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_test(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);

		if (!chat || !cmd || !net)
		{
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(net);

			return MM_FAIL;
		}

		cmd->AddCommand("test", Ctest, ALLARENAS, NULL);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);

		cmd->RemoveCommand("test", Ctest, ALLARENAS);

		return MM_OK;
	}
	return MM_FAIL;
}
