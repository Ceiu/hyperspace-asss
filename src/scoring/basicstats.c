
/* dist: public */

#include "asss.h"
#include "fg_turf.h"


local Iplayerdata *pd;
local Istats *stats;


local void mypa(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
		stats->StartTimer(p, STAT_ARENA_TOTAL_TIME);
	else if (action == PA_LEAVEARENA)
		stats->StopTimer(p, STAT_ARENA_TOTAL_TIME);
}

local void mykill(Arena *arena, Player *killer, Player *killed,
		int bounty, int flags, int *pts, int *green)
{
	stats->IncrementStat(killer, STAT_KILLS, 1);
	stats->IncrementStat(killed, STAT_DEATHS, 1);

	if (killer->p_freq == killed->p_freq)
	{
		stats->IncrementStat(killer, STAT_TEAM_KILLS, 1);
		stats->IncrementStat(killed, STAT_TEAM_DEATHS, 1);
	}

	if (flags)
	{
		stats->IncrementStat(killer, STAT_FLAG_KILLS, 1);
		stats->IncrementStat(killed, STAT_FLAG_DEATHS, 1);
	}
}


local void myflaggain(Arena *arena, Player *p, int fid, int how)
{
	if (how == FLAGGAIN_PICKUP)
		stats->IncrementStat(p, STAT_FLAG_PICKUPS, 1);

	/* always do this */
	stats->StartTimer(p, STAT_FLAG_CARRY_TIME);
}

local void myflaglost(Arena *arena, Player *p, int how)
{
	/* only stop it if he lost his last flag */
	if (p->pkt.flagscarried == 0)
		stats->StopTimer(p, STAT_FLAG_CARRY_TIME);
	/* this stuff may not be accurate: flag games can decide to make
	 * various things regular or neuted drops, or something else
	 * entirely. this is a rough approximation good for most purposes,
	 * though. */
	switch (how)
	{
		case CLEANUP_DROPPED:
		case CLEANUP_INSAFE:
			stats->IncrementStat(p, STAT_FLAG_DROPS, 1);
			break;
		case CLEANUP_SHIPCHANGE:
		case CLEANUP_FREQCHANGE:
		case CLEANUP_LEFTARENA:
			stats->IncrementStat(p, STAT_FLAG_NEUT_DROPS, 1);
			break;
		case CLEANUP_KILL_NORMAL:
		case CLEANUP_KILL_TK:
		case CLEANUP_KILL_CANTCARRY:
		case CLEANUP_KILL_FAKE:
		case CLEANUP_OTHER:
			break;
	}
}

local void myturftag(Arena *a, Player *p, int oldfreq, int newfreq)
{
	stats->IncrementStat(p, STAT_TURF_TAGS, 1);
}

local void myflagreset(Arena *arena, int freq, int points)
{
	if (freq >= 0 && points > 0)
	{
		Player *p;
		Link *link;

		pd->Lock();
		FOR_EACH_PLAYER(p)
			if (p->status == S_PLAYING &&
			    p->arena == arena &&
			    p->p_ship != SHIP_SPEC)
			{
				if (p->p_freq == freq)
				{
					stats->IncrementStat(p, STAT_FLAG_GAMES_WON, 1);
					/* only do flag reward points if not in safe zone */
					if (!(p->position.status & STATUS_SAFEZONE))
						stats->IncrementStat(p, STAT_FLAG_POINTS, points);
				}
				else
					stats->IncrementStat(p, STAT_FLAG_GAMES_LOST, 1);
			}
		pd->Unlock();
	}
}


local void mybpickup(Arena *arena, Player *p, int bid)
{
	stats->StartTimer(p, STAT_BALL_CARRY_TIME);
	stats->IncrementStat(p, STAT_BALL_CARRIES, 1);
}

local void mybfire(Arena *arena, Player *p, int bid)
{
	stats->StopTimer(p, STAT_BALL_CARRY_TIME);
}

local void mygoal(Arena *arena, Player *p, int bid, int x, int y)
{
	stats->IncrementStat(p, STAT_BALL_GOALS, 1);
}

EXPORT const char info_basicstats[] = CORE_MOD_INFO("basicstats");

EXPORT int MM_basicstats(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		if (!pd || !stats) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, mypa, ALLARENAS);
		mm->RegCallback(CB_KILL, mykill, ALLARENAS);

		mm->RegCallback(CB_FLAGRESET, myflagreset, ALLARENAS);
		mm->RegCallback(CB_FLAGGAIN, myflaggain, ALLARENAS);
		mm->RegCallback(CB_FLAGLOST, myflaglost, ALLARENAS);
		mm->RegCallback(CB_TURFTAG, myturftag, ALLARENAS);

		mm->RegCallback(CB_BALLPICKUP, mybpickup, ALLARENAS);
		mm->RegCallback(CB_BALLFIRE, mybfire, ALLARENAS);
		mm->RegCallback(CB_GOAL, mygoal, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, mypa, ALLARENAS);
		mm->UnregCallback(CB_KILL, mykill, ALLARENAS);
		mm->UnregCallback(CB_FLAGRESET, myflagreset, ALLARENAS);
		mm->UnregCallback(CB_FLAGGAIN, myflaggain, ALLARENAS);
		mm->UnregCallback(CB_FLAGLOST, myflaglost, ALLARENAS);
		mm->UnregCallback(CB_TURFTAG, myturftag, ALLARENAS);
		mm->UnregCallback(CB_BALLPICKUP, mybpickup, ALLARENAS);
		mm->UnregCallback(CB_BALLFIRE, mybfire, ALLARENAS);
		mm->UnregCallback(CB_GOAL, mygoal, ALLARENAS);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(stats);
		return MM_OK;
	}
	return MM_FAIL;
}

