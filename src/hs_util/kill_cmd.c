#include "asss.h"
#include "kill.h"

typedef struct AData
{
	HashTable *loaded_killers;
} AData;

local Ikill *kill;
local Icmdman *cmd;
local Iarenaman *aman;
local Ichat *chat;

local int adata_key;

local pthread_mutex_t mymutex = PTHREAD_MUTEX_INITIALIZER;

#define LOCK pthread_mutex_lock(&mymutex)
#define UNLOCK pthread_mutex_unlock(&mymutex);

local helptext_t kill_help =
"Targets: player\n"
"Args: killer\n"
"Kills a player with a blast that affects all nearby players.\n";

local void Ckill(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		AData *adata = P_ARENA_DATA(p->arena, adata_key);
		Player *victim = target->u.p;
		Killer *killer = HashGetOne(adata->loaded_killers, params);
		if (killer != NULL)
		{
			kill->Kill(victim, killer, 4, 0);
		}
		else
		{
			chat->SendMessage(p, "That killer does not exist!");
		}
	}
}

local helptext_t loadkiller_help =
"Targets: none\n"
"Args: killer name\n"
"Loads a killer.\n";

local void Cloadkiller(const char *command, const char *params, Player *p, const Target *target)
{
	AData *adata = P_ARENA_DATA(p->arena, adata_key);
	Killer *killer = HashGetOne(adata->loaded_killers, params);
	if (killer == NULL)
	{

		killer = kill->LoadKiller(params, p->arena, 0, 9999);

		LOCK;
		HashAdd(adata->loaded_killers, params, killer);
		UNLOCK;
	}
	else
	{
		chat->SendMessage(p, "That killer already exists!");
	}
}

local helptext_t unloadkiller_help =
"Targets: none\n"
"Args: killer name\n"
"Unloads a killer.\n";

local void Cunloadkiller(const char *command, const char *params, Player *p, const Target *target)
{
	AData *adata = P_ARENA_DATA(p->arena, adata_key);
	Killer *killer = HashGetOne(adata->loaded_killers, params);
	if (killer != NULL)
	{
		LOCK;
		HashRemove(adata->loaded_killers, params, killer);
		UNLOCK;

		kill->UnloadKiller(killer);
	}
	else
	{
		chat->SendMessage(p, "That killer does not exist!");
	}

}

local int free_killer_enum(const char *key, void *val, void *clos)
{
	Killer *killer = val;
	kill->UnloadKiller(killer);

	return TRUE;
}

local void free_killers(Arena *arena)
{
	AData *adata = P_ARENA_DATA(arena, adata_key);
	LOCK;
	HashEnum(adata->loaded_killers, free_killer_enum, NULL);
	UNLOCK;
}

local void free_all_killers()
{
	Link *link;
	Arena *arena;
	aman->Lock();
	FOR_EACH_ARENA(arena)
	{
		free_killers(arena);
	}
	aman->Unlock();
}

local void init_arena(Arena *arena)
{
	AData *adata = P_ARENA_DATA(arena, adata_key);
	LOCK;
	adata->loaded_killers = HashAlloc();
	UNLOCK
}

local void init_arenas()
{
	Link *link;
	Arena *arena;
	aman->Lock();
	FOR_EACH_ARENA(arena)
	{
		init_arena(arena);
	}
	aman->Unlock();
}

local void arena_action(Arena *arena, int action)
{
	if (action == AA_CREATE)
	{
		init_arena(arena);
	}
	else if (action == AA_DESTROY)
	{
		free_killers(arena);
	}
}

EXPORT const char info_kill_cmd[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_kill_cmd(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		kill = mm->GetInterface(I_KILL, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!cmd || !kill || !aman || !chat)
		{
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(kill);
			mm->ReleaseInterface(aman);
			mm->ReleaseInterface(chat);
			return MM_FAIL;
		}

		adata_key = aman->AllocateArenaData(sizeof(AData));
		if (adata_key == -1) return MM_FAIL;

		init_arenas();

		cmd->AddCommand("kill", Ckill, ALLARENAS, kill_help);
		cmd->AddCommand("loadkiller", Cloadkiller, ALLARENAS, loadkiller_help);
		cmd->AddCommand("unloadkiller", Cunloadkiller, ALLARENAS, unloadkiller_help);

		mm->RegCallback(CB_ARENAACTION, arena_action, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("kill", Ckill, ALLARENAS);
		cmd->RemoveCommand("loadkiller", Cloadkiller, ALLARENAS);
		cmd->RemoveCommand("unloadkiller", Cunloadkiller, ALLARENAS);

		mm->UnregCallback(CB_ARENAACTION, arena_action, ALLARENAS);

		free_all_killers();

		aman->FreeArenaData(adata_key);

		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(kill);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(chat);

		return MM_OK;
	}
	return MM_FAIL;
}

