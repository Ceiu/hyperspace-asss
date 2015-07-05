#include <stdlib.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "selfpos.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Ihscoreitems *items;
local Iselfpos *selfpos;
local Ichat *chat;

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;

	if (len < 22)
		return;

	/* handle common errors */
	if (!p->arena)
		return;

	if (p->p_ship == SHIP_SPEC)
	{
		return;
	}

	if (pos->weapon.type == W_DECOY)
	{
		int decoysplit = items->getPropertySum(p, p->p_ship, "decoysplit", 0);
		if (decoysplit)
		{
			int i;
			int offset = 0; // rand() % 40;
			
			double splitvel = items->getPropertySum(p, p->p_ship, "splitvel", 1000);
			struct Weapons weapon = {
				W_DECOY, 0, 0, 0, 0, 0
			};
			
			for (i = 0; i < decoysplit; i++)
			{
				int direction = (offset + (i * (40 / decoysplit))) % 40;

				// 0 = north, pi/2 = east
				double phi = ((double)(direction)) * M_PI / 20.0;
				int v_x = pos->xspeed + (int)(splitvel * sin(phi));
				int v_y = pos->yspeed + (int)(splitvel * cos(phi));

				if (i + 1 < decoysplit)
				{
					chat->SendMessage(p, "Decoy %d: vx=%d vy=%d dir=%d", i, v_x, v_y, direction);
					selfpos->WarpPlayerWithWeapon(p, pos->x, pos->y, v_x, v_y, direction, i - decoysplit - 1, &weapon);
				}
				else
				{
					chat->SendMessage(p, "Final %d: vx=%d vy=%d dir=%d", i, v_x, v_y, direction);
					selfpos->WarpPlayer(p, pos->x, pos->y, v_x, v_y, direction, 0);
				}
			}
		}
	}
}

EXPORT int MM_hs_decoy(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!lm || !net || !items || !selfpos || !chat) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(chat);

		return MM_OK;
	}
	return MM_FAIL;
}

