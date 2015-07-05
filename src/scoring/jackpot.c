
/* dist: public */

#include "asss.h"
#include "jackpot.h"
#include "persist.h"

#define KEY_JACKPOT 12

local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ipersist *persist;

local pthread_mutex_t mtx;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

typedef struct
{
	int jp;
	int percent;
} jpdata;

local int jpkey;


local void ResetJP(Arena *arena)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	LOCK();
	jpd->jp = 0;
	UNLOCK();
}

local void AddJP(Arena *arena, int pts)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	LOCK();
	jpd->jp += pts;
	UNLOCK();
}

local int GetJP(Arena *arena)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	int jp;
	LOCK();
	jp = jpd->jp;
	UNLOCK();
	return jp;
}

local void SetJP(Arena *arena, int pts)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	LOCK();
	jpd->jp = pts;
	UNLOCK();
}


local int get_data(Arena *arena, void *d, int len, void *v)
{
	int jp = GetJP(arena);
	if (jp)
	{
		*(int*)d = jp;
		return sizeof(int);
	}
	else
		return 0;
}

local void set_data(Arena *arena, void *d, int len, void *v)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	if (len == sizeof(int))
	{
		LOCK();
		jpd->jp = *(int*)d;
		UNLOCK();
	}
	else
		ResetJP(arena);
}

local void clear_data(Arena *arena, void *v)
{
	ResetJP(arena);
}

local ArenaPersistentData persistdata =
{
	KEY_JACKPOT, INTERVAL_GAME, PERSIST_ALLARENAS,
	get_data, set_data, clear_data
};


local void mykill(Arena *arena, int killer, int killed,
		int bounty, int flags, int *pts, int *green)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	LOCK();
	if (jpd->percent)
		jpd->jp += bounty * jpd->percent / 1000;
	UNLOCK();
}

local void aaction(Arena *arena, int action)
{
	jpdata *jpd = P_ARENA_DATA(arena, jpkey);
	LOCK();
	/* cfghelp: Kill:JackpotBountyPercent, arena, int, def: 0
	 * The percent of a player's bounty added to the jackpot on each
	 * kill. Units: 0.1%. */
	if (action == AA_CREATE || action == AA_CONFCHANGED)
		jpd->percent = cfg->GetInt(arena->cfg, "Kill", "JackpotBountyPercent", 0);
	UNLOCK();
}


local Ijackpot jpint =
{
	INTERFACE_HEAD_INIT(I_JACKPOT, "jackpot")
	ResetJP, AddJP, GetJP, SetJP
};

EXPORT const char info_jackpot[] = CORE_MOD_INFO("jackpot");

EXPORT int MM_jackpot(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (!aman || !cfg || !persist) return MM_FAIL;

		jpkey = aman->AllocateArenaData(sizeof(jpdata));
		if (jpkey == -1) return MM_FAIL;

		pthread_mutex_init(&mtx, NULL);

		mm->RegCallback(CB_KILL, mykill, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);

		persist->RegArenaPD(&persistdata);

		mm->RegInterface(&jpint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&jpint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_KILL, mykill, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		persist->UnregArenaPD(&persistdata);
		aman->FreeArenaData(jpkey);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(persist);
		pthread_mutex_destroy(&mtx);
		return MM_OK;
	}
	return MM_FAIL;
}

