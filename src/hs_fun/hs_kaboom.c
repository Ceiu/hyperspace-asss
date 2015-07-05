#include "asss.h"
#include "hscore.h"

#define CROSS_CALLBACK_ID 100
#define KABOOM_CALLBACK_ID 102
#define SQUARE_BRICK_CALLBACK_ID 103
#define MEGA_BRICK_CALLBACK_ID 104

//modules
local Imodman *mm;
local Inet *net;
local Ibricks *bricks;
local Ichat *chat;
local Igame *game;

local void eventCallback(Player *p, int eventID)
{
	if (eventID == KABOOM_CALLBACK_ID)
	{
		//do it!

		struct S2CWeapons packet = {
			S2C_WEAPON, p->position.rotation, (current_ticks() + 50) & 0xFFFF, p->position.x, p->position.yspeed,
			p->pid, p->position.xspeed, 0, p->position.status, 0,
			p->position.y, p->position.bounty
		};

		struct Weapons weapon = {
			W_THOR,	//u16 type,;
			3,		//u16 level : 2;
			1,		//u16 shrapbouncing : 1;
			3,		//u16 shraplevel : 2;
			31,		//u16 shrap : 5;
			0		//u16 alternate : 1;
		};

		packet.weapon = weapon;

		game->DoWeaponChecksum(&packet);

		net->SendToArena(p->arena, NULL, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);

		chat->SendMessage(p, "Kaboom event callback! %d %d %d %d %d", p->position.x, p->position.y, p->position.xspeed, p->position.yspeed, p->position.status);
	}
	else if (eventID == CROSS_CALLBACK_ID)
	{
		int x = p->position.x >> 4;
		int y = p->position.y >> 4;

		int top = y - 2;
		int bottom = y + 5;
		int left = x - 2;
		int right = x + 2;

		bricks->DropBrick(p->arena, p->p_freq, left, y, right, y); //horizontal
		bricks->DropBrick(p->arena, p->p_freq, x, top, x, bottom); //vertical
	}
	else if (eventID == SQUARE_BRICK_CALLBACK_ID)
	{
		int x = p->position.x >> 4;
		int y = p->position.y >> 4;

		bricks->DropBrick(p->arena, p->p_freq, x-2, y-2, x+2, y+2);
	}
	else if (eventID == MEGA_BRICK_CALLBACK_ID)
	{
		bricks->DropBrick(p->arena, p->p_freq, 134997216, 134538562, -1234017384, -1234015312);
	}
}

EXPORT const char info_hs_kaboom[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_kaboom(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		net = mm->GetInterface(I_NET, ALLARENAS);
		bricks = mm->GetInterface(I_BRICKS, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!net || !bricks || !chat || !game)
		{
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(bricks);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(game);

			return MM_FAIL;
		}

		mm->RegCallback(CB_EVENT_ACTION, eventCallback, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_EVENT_ACTION, eventCallback, ALLARENAS);

		mm->ReleaseInterface(net);
		mm->ReleaseInterface(bricks);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(game);

		return MM_OK;
	}
	return MM_FAIL;
}
