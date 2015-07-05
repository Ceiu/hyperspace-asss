
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"

typedef struct adata
{
	HashTable timeouts;
} adata;

local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iarenaman *aman;
local Iflagcore *flagcore;
local Imainloop *ml;

local int adkey;

local pthread_mutex_t timeoutmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&timeoutmtx)
#define UNLOCK() pthread_mutex_unlock(&timeoutmtx)

local void set_timeout(Player *p)
{
	adata *adata = P_ARENA_DATA(p->arena, adkey);
	/* cfghelp: Flag:TeamChangeGrace, arena, int, def: 1000
	 * Period of time during which players are allowed to switch
	 * back to the winning team after leaving it. */
	ticks_t new_time = current_ticks() + cfg->GetInt(p->arena->cfg, "Flag", "TeamChangeGrace", 1000);
	ticks_t *timeout;

	LOCK();
	timeout = HashGetOne(&adata->timeouts, p->name);

	if (!timeout)
	{
		timeout = amalloc(sizeof(*timeout));
		HashAdd(&adata->timeouts, p->name, timeout);
	}
	*timeout = new_time;
	UNLOCK();
}

local int CanChangeFreq(Player *p, int new_freq, int is_changing, char *err_buf, int buf_len)
{
	adata *adata = P_ARENA_DATA(p->arena, adkey);
	ticks_t *timeout;

	LOCK();
	timeout = HashGetOne(&adata->timeouts, p->name);
	if (timeout)
	{
		if (TICK_GT(*timeout, current_ticks()))
		{
			UNLOCK();
			return TRUE;
		}
	}
	UNLOCK();

	if (flagcore->IsWinning(p->arena, new_freq))
	{

		if (err_buf)
			snprintf(err_buf, buf_len, "Freq %d has all of the flags.", new_freq);
		return FALSE;
	}

	return TRUE;
}

local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_LEAVEARENA)
	{
		if (p->arena && flagcore->IsWinning(p->arena, p->p_freq))
		{
			set_timeout(p);
		}
	}
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	if (flagcore->IsWinning(p->arena, oldfreq))
	{
		set_timeout(p);
	}
}

local int prune_enum_cb(const char *key, void *val, void *clos)
{
	ticks_t *timeout = (ticks_t*)val;
	if (TICK_GT(current_ticks(), *timeout))
	{
		afree(timeout);
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

local int prune_timeouts(void *clos)
{
	Arena *arena = (Arena*)clos;
	adata *adata = P_ARENA_DATA(arena, adkey);

	LOCK();
	HashEnum(&adata->timeouts, prune_enum_cb, NULL);
	UNLOCK();

	return TRUE;
}

local void clear_timeouts(Arena *arena)
{
	adata *adata = P_ARENA_DATA(arena, adkey);

	LOCK();
	HashEnum(&adata->timeouts, hash_enum_afree, NULL);
	HashDeinit(&adata->timeouts);
	UNLOCK();
}

local Aenforcer myadv =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
  NULL,
  NULL,
  NULL,
  CanChangeFreq,
  NULL
};

EXPORT const char info_enf_flagwin[] = CORE_MOD_INFO("enf_flagwin");

EXPORT int MM_enf_flagwin(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!lm || !cfg || !aman || !flagcore || !ml) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		aman->FreeArenaData(adkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(ml);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		adata *adata = P_ARENA_DATA(arena, adkey);

		HashInit(&adata->timeouts);

		mm->RegAdviser(&myadv, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_PLAYERACTION, paction, arena);

		ml->SetTimer(prune_timeouts, 100, 100, arena, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		ml->ClearTimer(prune_timeouts, arena);

		mm->UnregAdviser(&myadv, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_PLAYERACTION, paction, arena);

		clear_timeouts(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
