#include <string.h>

#include "asss.h"
#include "kill.h"
#include "fake.h"
#include "selfpos.h"

// TODO: make a command to temporarily disable all killers in the arena (for ?recyclearena)

typedef struct AData
{
	LinkedList killers;
} AData;

struct Killer
{
	Player *fake;
	Arena *arena;
	int ref_count;

	// stuff needed to recreate the player later
	int ship;
	int freq;
	char name[20];
};

local Ilogman *lm;
local Imodman *mm;
local Inet *net;
local Iarenaman *aman;
local Ifake *fake;
local Iplayerdata *pd;
local Iconfig *cfg;
local Imainloop *ml;
local Iselfpos *selfpos;
local Igame *game;

local int adata_key;

local pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;

#define LOCK pthread_mutex_lock(&mymutex)
#define UNLOCK pthread_mutex_unlock(&mymutex);

local Killer *get_killer(const char *name, Arena *arena)
{
	AData *adata = P_ARENA_DATA(arena, adata_key);
	Link *link;

	if (arena == NULL)
	{
		return NULL;
	}

	LOCK;
	for (link = LLGetHead(&adata->killers); link; link = link->next)
	{
		Killer *entry = link->data;

		if (strcasecmp(entry->name, name) == 0)
		{
			UNLOCK;
			return entry;
		}
	}
	UNLOCK;

	return NULL;
}

local void do_kill(Player *victim, Player *killer, int blast, int only_team)
{
	unsigned status = STATUS_STEALTH | STATUS_CLOAK | STATUS_UFO;
	int v_x, v_y, rotation;
	int bomb_speed;

	if (victim->p_ship == SHIP_SPEC)
	{
		// TODO: should this be logged?
		return;
	}

	bomb_speed = cfg->GetInt(victim->arena->cfg, cfg->SHIP_NAMES[killer->p_ship], "BombSpeed", 0);

	// match the thor velocity with the victim's velocity
	// this is a simple way, since the killer is UFOed
	rotation = 10;
	v_x = victim->position.xspeed - bomb_speed;
	v_y = victim->position.yspeed;

	struct S2CWeapons packet = {
		S2C_WEAPON, rotation, current_ticks() & 0xFFFF,
		victim->position.x, v_y, killer->pid, v_x, 0,
		status, 0, victim->position.y, 0
	};

	// make it a thor with 31 L4 bouncing shrap
	packet.weapon.type = W_THOR;
	packet.weapon.shrapbouncing = 1;
	packet.weapon.shraplevel = 2;
	packet.weapon.shrap = 31;
	packet.weapon.alternate = 0;

	if (blast)
	{
		LinkedList list;
		Link *link;
		Player *i;

		// set the level of the thor
		packet.weapon.level = blast - 1;

		LLInit(&list);

		pd->Lock();
		FOR_EACH_PLAYER(i)
		{
			if (i != victim && i != killer && i->status == S_PLAYING && i->arena == victim->arena
				&& (!only_team || i->p_freq == victim->p_freq))
			{
				LLAdd(&list, i);
			}
		}
		pd->Unlock();

		game->DoWeaponChecksum(&packet);
		net->SendToSet(&list, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
		// TODO: increment weapon count?
	}

	// always send the player a L4 thor
	packet.weapon.level = 3;

	if (victim->position.status & STATUS_SAFEZONE)
	{
		/* cfghelp: Kill:WarpFromSafeX, arena, int, def: 40, mod: kill
		 * Where to warp players that are in a safety (in pixels)
		 * to kill them. */
		int x = cfg->GetInt(victim->arena->cfg, "Kill", "WarpFromSafeX", 40);

		/* cfghelp: Kill:WarpFromSafeY, arena, int, def: 40, mod: kill
		 * Where to warp players that are in a safety (in pixels)
		 * to kill them. */
		int y = cfg->GetInt(victim->arena->cfg, "Kill", "WarpFromSafeY", 40);;

		// they're in a safety, warp them out
		selfpos->WarpPlayer(victim, x, y, 0, 0, 0, 0);

		// and prepare to kill them at their new spot
		packet.x = x;
		packet.y = y;
		packet.xspeed = -bomb_speed;
		packet.yspeed = 0;
	}

	// kill them
	game->DoWeaponChecksum(&packet);
	net->SendToOne(victim, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
	// TODO: increment weapon count?
}

local Killer * LoadKiller(const char *name, Arena *arena, int ship, int freq)
{
	AData *adata = P_ARENA_DATA(arena, adata_key);
	Killer *killer = get_killer(name, arena);

	if (killer == NULL)
	{
		// doesn't exit, make a new one
		killer = amalloc(sizeof(*killer));

		killer->fake = fake->CreateFakePlayer(name, arena, ship, freq);
		killer->arena = arena;
		killer->ref_count = 1;

		astrncpy(killer->name, name, 20);
		killer->ship = ship;
		killer->freq = freq;

		LOCK;
		LLAdd(&adata->killers, killer);
		UNLOCK;
	}
	else
	{
		// already exists: incremenet reference count
		killer->ref_count++;
	}

	return killer;
}

local void UnloadKiller(Killer *killer)
{
	if (killer != NULL)
	{
		killer->ref_count--;

		if (killer->ref_count < 1)
		{
			// unload it
			AData *adata = P_ARENA_DATA(killer->arena, adata_key);

			LOCK;
			LLRemove(&adata->killers, killer);
			UNLOCK;

			if (killer->fake != NULL)
			{
				fake->EndFaked(killer->fake);
			}

			afree(killer);
		}
	}
}

local void Kill(Player *p, Killer *killer, int blast, int only_team)
{
	if (!p->arena)
	{
		return;
	}

	if (killer->fake == NULL)
	{
		// we don't have an actual killer. we need to make one before calling the function
		killer->fake = fake->CreateFakePlayer(killer->name, killer->arena, killer->ship, killer->freq);
	}

	do_kill(p, killer->fake, blast, only_team);
}

local void KillWithPlayer(Player *p, Player *killer, int blast, int only_team)
{
	if (!p->arena)
	{
		return;
	}

	if (p->arena != killer->arena)
	{
		lm->LogA(L_WARN, "kill", p->arena, "KillWithPlayer asked to kill with player from different arena!");
		return;
	}

	if (killer->p_ship == SHIP_SPEC)
	{
		lm->LogA(L_WARN, "kill", p->arena, "KillWithPlayer asked to kill with player in spec!");
		return;
	}

	do_kill(p, killer, blast, only_team);
}

local Player *GetKillerPlayer(Killer *killer)
{
	return killer->fake;
}

local void arena_action(Arena *arena, int action)
{
	AData *adata = P_ARENA_DATA(arena, adata_key);

	if (action == AA_PRECREATE)
	{
		LOCK;
		LLInit(&adata->killers);
		UNLOCK
	}
	else if (action == AA_POSTDESTROY)
	{
		Link *link;
		LOCK;
		for (link = LLGetHead(&adata->killers); link; link = link->next)
		{
			Killer *killer = link->data;
			if (killer->fake)
			{
				fake->EndFaked(killer->fake);
			}

			lm->LogA(L_WARN, "kill", arena, "Removing killer %s", killer->name);
			afree(killer);
		}
		LLEmpty(&adata->killers);
		UNLOCK;
	}
}

local void player_action(Player *p, int action, Arena *arena)
{
	if (action == PA_LEAVEARENA)
	{
		Link *link;
		Player *i;
		int players = 0;

		pd->Lock();
		FOR_EACH_PLAYER(i)
			if (i != p && i->arena == arena && IS_HUMAN(i))
			{
				players++;
			}
		pd->Unlock();

		if (players == 0)
		{
			// when the arena has no real players, remove all the fakes
			// to hopefully trigger an arena destroy
			// the fakes will be recreated later if necessary

			AData *adata = P_ARENA_DATA(arena, adata_key);
			Link *link;

			LOCK;
			for (link = LLGetHead(&adata->killers); link; link = link->next)
			{
				Killer *killer = link->data;
				fake->EndFaked(killer->fake);
				killer->fake = NULL;
			}
			UNLOCK;
		}
	}
}

local Ikill interface =
{
	INTERFACE_HEAD_INIT(I_KILL, "kill")
	LoadKiller, UnloadKiller, Kill, KillWithPlayer, GetKillerPlayer
};

EXPORT const char info_kill[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_kill(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!lm || !net || !aman || !fake || !pd || !ml || !selfpos || !game)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(aman);
			mm->ReleaseInterface(fake);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(selfpos);
			mm->ReleaseInterface(game);

			return MM_FAIL;
		}

		adata_key = aman->AllocateArenaData(sizeof(AData));
		if (adata_key == -1) return MM_FAIL;

		mm->RegInterface(&interface, ALLARENAS);

		mm->RegCallback(CB_PLAYERACTION, player_action, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, arena_action, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
		{
			return MM_FAIL;
		}

		mm->UnregCallback(CB_ARENAACTION, arena_action, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, player_action, ALLARENAS);

		aman->FreeArenaData(adata_key);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(fake);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(game);

		return MM_OK;
	}
	return MM_FAIL;
}
