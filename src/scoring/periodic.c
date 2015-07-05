
/* dist: public */
#include <stdio.h>

#include "asss.h"

/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Imainloop *ml;
local Iflagcore *flagcore;
local Iconfig *cfg;
local Inet *net;
local Istats *stats;

typedef struct periodic_settings
{
	Arena *arena;
	int delay;
	int minplayers;
	int sendzero;
	Iperiodicpoints *pp;
	/* only for duration of timer: */
	int totalplayers;
	byte *pkt;
	int pktfreqs;
} periodic_settings;


typedef struct freq_data
{
	TreapHead head; /* key is freq */
	int players;
	int flags;
	int points;
} freq_data;


local void reward_enum(TreapHead *node, void *clos)
{
	freq_data *fd = (freq_data*)node;
	periodic_settings *set = clos;
	int points, freq = node->key;

	/* enter in packet */
	points = set->pp->GetPoints(
			set->arena,
			freq,
			fd->players,
			set->totalplayers,
			fd->flags);

	if (points || set->sendzero)
	{
		/* enter in packet */
		*(set->pkt++) = (freq>>0) & 0xff;
		*(set->pkt++) = (freq>>8) & 0xff;
		*(set->pkt++) = (points>>0) & 0xff;
		*(set->pkt++) = (points>>8) & 0xff;
		set->pktfreqs++;
	}

	/* set in fd */
	fd->points = points;
}


local int timer(void *set_)
{
	periodic_settings *set = set_;
	int totalplayers = 0, freqcount = 0;
	int include_spec, include_safe;
	TreapHead *fdata = NULL;
	freq_data *fd;
	Player *p;
	Link *link;

	/* TODO: setup cfghelps for these */
	include_spec = cfg->GetInt(set->arena->cfg, "Periodic", "IncludeSpectators", 0);
	include_safe = cfg->GetInt(set->arena->cfg, "Periodic", "IncludeSafeZones", 0);

	/* figure out what freqs we have in this arena, how many players
	 * each has, and how many flags each owns. */
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING && IS_HUMAN(p) &&
		    p->arena == set->arena && (p->p_ship != SHIP_SPEC || include_spec))
		{
			int freq = p->p_freq;
			fd = (freq_data*)TrGet(fdata, freq);
			if (fd == NULL)
			{
				fd = amalloc(sizeof(*fd));
				fd->head.key = freq;
				fd->players = 0;
				fd->flags = flagcore->CountFreqFlags(set->arena, freq);
				TrPut(&fdata, &fd->head);
				freqcount++;
			}
			fd->players++;
			totalplayers++;
		}
	pd->Unlock();

	if (totalplayers >= set->minplayers && set->pp)
	{
		/* get packet */
		byte *pkt = amalloc(freqcount*4+1);

		pkt[0] = S2C_PERIODICREWARD;
		set->pkt = pkt + 1;
		set->pktfreqs = 0;

		/* now calculate points for each freq, filling in packet as we go */
		TrEnum(fdata, reward_enum, set);

		net->SendToArena(set->arena, NULL, pkt, set->pktfreqs*4+1, NET_RELIABLE);
		afree(pkt);
		set->pkt = NULL; /* just to be safe */

		/* now reward points to all */
		pd->Lock();
		FOR_EACH_PLAYER(p)
			if (IS_STANDARD(p) &&
			    p->status == S_PLAYING &&
			    p->arena == set->arena &&
			    (p->p_ship != SHIP_SPEC || include_spec) &&
			    (!(p->position.status & STATUS_SAFEZONE) || include_safe))
				if ((fd = (freq_data*)TrGet(fdata, p->p_freq)))
					stats->IncrementStat(p, STAT_FLAG_POINTS, fd->points);
		pd->Unlock();

		/* i think the client is smart enough that we don't need to do
		 * this:
		stats->SendUpdates();
		*/
	}

	/* free the treap we've been working with */
	TrEnum(fdata, tr_enum_afree, NULL);

	return TRUE;
}

local void cleanup(void *set_)
{
	periodic_settings *set = set_;

	/**
	 * Hack to check if the interface still exists. Fixes a segfaulting issue when the owning module
	 * is forcefully unloaded while we still hold a reference.
	 */
	Iperiodicpoints *pp = mm->GetInterface(I_PERIODIC_POINTS, ALLARENAS);

	if (pp) {
		mm->ReleaseInterface(set->pp);
		mm->ReleaseInterface(pp);
	}

	afree(set);
}



local void aaction(Arena *arena, int action)
{
	int delay;

	/* cfghelp: Periodic:RewardDelay, arena, int, def: 0
	 * The interval between periodic rewards (in ticks). Zero to disable. */
	if (action == AA_CREATE || action == AA_CONFCHANGED)
		delay = cfg->GetInt(arena->cfg, "Periodic", "RewardDelay", 0);
	else if (action == AA_DESTROY)
		delay = 0;
	else
		return;

	/* cleanup any old timers */
	ml->CleanupTimer(timer, arena, cleanup);

	/* if we need a new one... */
	if (delay)
	{
		Iperiodicpoints *pp = mm->GetInterface(I_PERIODIC_POINTS, arena);
		if (pp)
		{
			periodic_settings *set = amalloc(sizeof(*set));
			set->arena = arena;
			set->delay = delay;
			/* cfghelp: Periodic:RewardMinimumPlayers, arena, int, def: 0
			 * The minimum players necessary in the arena to give out
			 * periodic rewards. */
			set->minplayers = cfg->GetInt(arena->cfg, "Periodic", "RewardMinimumPlayers", 0);
			/* cfghelp: Periodic:SendZeroRewards, arena, int, def: 1
			 * Whether frequencies with zero points will still get
			 * a reward notification during the ding. */
			set->sendzero = cfg->GetInt(arena->cfg, "Periodic", "SendZeroRewards", 1);
			set->pp = pp;
			ml->SetTimer(timer, delay, delay, set, arena);
			mm->ReleaseInterface(pp);
		}
	}
}

EXPORT const char info_periodic[] = CORE_MOD_INFO("periodic");

EXPORT int MM_periodic(int action, Imodman *mm_, Arena *arena)
{
	// Arena *arena;
	// Link *link;

	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		if (!pd || !aman || !flagcore || !cfg || !stats) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		ml->CleanupTimer(timer, NULL, cleanup);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(stats);
		return MM_OK;
	}
	return MM_FAIL;
}


