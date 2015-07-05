
/* dist: public */

#include <stdlib.h>

#include "asss.h"
#include "koth.h"
#include "packets/koth.h"

/* King:DeathCount:::Number of deaths a player is allowed until his
 * crown is removed
 * King:ExpireTime:::Initial time given to each player at beginning of
 * 'King of the Hill' round
 * King:RewardFactor:::Number of points given to winner of 'King of the
 * Hill' round (uses FlagReward formula)
 * King:NonCrownAdjustTime:::Amount of time added for killing a player
 * without a crown
 * King:NonCrownMinimumBounty:::Minimum amount of bounty a player must
 * have in order to receive the extra time.
 * King:CrownRecoverKills:::Number of crown kills a non-crown player
 * must get in order to get their crown back.
 */

struct koth_arena_data
{
	int deathcount, expiretime, /* killadjusttime, killminbty, */ recoverkills;
	int minplaying;
};

struct koth_player_data
{
	unsigned char crown, hadcrown, deaths, crownkills;
};


local int akey, pkey;

local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)


local Imodman *mm;
local Inet *net;
local Ichat *chat;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Icmdman *cmd;
local Ilogman *lm;
local Imainloop *ml;
local Istats *stats;



/* needs lock */
local void start_koth(Arena *arena)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	struct koth_player_data *pdata;
	struct S2CKoth pkt =
		{ S2C_KOTH, KOTH_ACTION_ADD_CROWN, adata->expiretime, -1 };

	LinkedList set = LL_INITIALIZER;
	Link *link;
	Player *p;

	pd->Lock();
	FOR_EACH_PLAYER_P(p, pdata, pkey)
		if (p->status == S_PLAYING &&
		    p->arena == arena)
		{
			if (p->p_ship != SHIP_SPEC)
			{
				LLAdd(&set, p);
				pdata->crown = 1;
				SET_HAS_CROWN(p);
				pdata->hadcrown = 1;
				pdata->deaths = 0;
				pdata->crownkills = 0;
			}
			else
			{
				pdata->crown = 0;
				UNSET_HAS_CROWN(p);
				pdata->hadcrown = 0;
			}
		}
	pd->Unlock();

	net->SendToSet(&set, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	chat->SendArenaMessage(arena, "King of the Hill game starting");
	lm->LogA(L_DRIVEL, "koth", arena, "game starting");

	DO_CBS(CB_KOTH_START, arena, KothStartFunc, (arena, LLCount(&set)));
	for (link = LLGetHead(&set); link; link = link->next)
		DO_CBS(CB_CROWNCHANGE, arena, CrownChangeFunc,
				(link->data, TRUE, KOTH_CAUSE_GAME_START));

	LLEmpty(&set);
}


/* needs lock */
local void check_koth(Arena *arena)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	struct koth_player_data *pdata;
	Player *p;
	int crowncount = 0, playing = 0, count;
	LinkedList hadset = LL_INITIALIZER;
	Link *link;

	/* first count crowns and previous crowns. also count total playing
	 * players. also keep track of who had a crown. */
	pd->Lock();
	FOR_EACH_PLAYER_P(p, pdata, pkey)
		if (p->status == S_PLAYING &&
		    p->arena == arena &&
		    p->p_ship != SHIP_SPEC &&
		    IS_STANDARD(p))
		{
			playing++;
			if (pdata->crown)
				crowncount++;
			if (pdata->hadcrown)
				LLAdd(&hadset, p);
		}
	pd->Unlock();

	/* figure out if there was a win */
	count = LLCount(&hadset);
	if (crowncount == 0 && count > 0)
	{
		/* a bunch of people expired at once. reward them and then
		 * restart */
		int pts;
		Ipoints_koth *pk = mm->GetInterface(I_POINTS_KOTH, arena);

		if (pk)
			pts = pk->GetPoints(arena, playing, count);
		else
			pts = 1000 / count;
		mm->ReleaseInterface(pk);

		DO_CBS(CB_KOTH_END, arena, KothEndFunc, (arena, playing, count, pts));

		for (link = LLGetHead(&hadset); link; link = link->next)
		{
			p = link->data;
			stats->IncrementStat(p, STAT_FLAG_POINTS, pts);
			stats->IncrementStat(p, STAT_KOTH_GAMES_WON, 1);
			chat->SendArenaMessage(arena, "King of the Hill: %s awarded %d points",
					p->name, pts);
			lm->LogP(L_DRIVEL, "koth", p, "won koth game");
			DO_CBS(CB_KOTH_PLAYER_WIN, arena, KothPlayerWinFunc, (arena, p, pts));
		}
		stats->SendUpdates(NULL);

		DO_CBS(CB_KOTH_PLAYER_WIN_END, arena, KothPlayerWinEndFunc, (arena));

		if (playing >= adata->minplaying)
			start_koth(arena);
	}
	else if (crowncount == 0)
	{
		if (playing >= adata->minplaying)
			start_koth(arena);
	}

	LLEmpty(&hadset);

	/* now mark anyone without a crown as not having had a crown */
	pd->Lock();
	FOR_EACH_PLAYER_P(p, pdata, pkey)
		if (p->status == S_PLAYING &&
		    p->arena == arena &&
		    p->p_ship != SHIP_SPEC)
			pdata->hadcrown = pdata->crown;
	pd->Unlock();
}


local int timer(void *v)
{
	Arena *arena = v;
	LOCK();
	check_koth(arena);
	UNLOCK();
	return TRUE;
}

/* needs lock */
local void set_crown_time(Player *p, int time)
{
	struct koth_player_data *pdata = PPDATA(p, pkey);
	struct S2CKoth pkt =
		{ S2C_KOTH, KOTH_ACTION_ADD_CROWN, time, -1 };
	pdata->crown = 1;
	SET_HAS_CROWN(pid);
	pdata->hadcrown = 1;
	net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
}

/* needs lock */
local void remove_crown(Player *p)
{
	Arena *arena = p->arena;
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	struct koth_player_data *pdata = PPDATA(p, pkey);
	struct S2CKoth pkt =
		{ S2C_KOTH, KOTH_ACTION_REMOVE_CROWN, 0, p->pid };

	if (adata->expiretime == 0)
		return;

	pdata->crown = 0;
	UNSET_HAS_CROWN(pid);
	net->SendToArena(arena, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	lm->LogP(L_DRIVEL, "koth", p, "lost crown");
}


local void load_settings(Arena *arena)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	ConfigHandle ch = arena->cfg;

	LOCK();
	adata->deathcount = cfg->GetInt(ch, "King", "DeathCount", 0);
	adata->expiretime = cfg->GetInt(ch, "King", "ExpireTime", 18000);
	/*
	adata->killadjusttime = cfg->GetInt(ch, "King", "NonCrownAdjustTime", 1500);
	adata->killminbty = cfg->GetInt(ch, "King", "NonCrownMininumBounty", 0);
	*/
	adata->recoverkills = cfg->GetInt(ch, "King", "CrownRecoverKills", 0);
	adata->minplaying = cfg->GetInt(ch, "King", "MinPlaying", 3);
	UNLOCK();
}


local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA || action == PA_LEAVEARENA)
	{
		struct koth_player_data *pdata = PPDATA(p, pkey);
		LOCK();
		pdata->crown = pdata->hadcrown = 0;
		UNSET_HAS_CROWN(pid);

		DO_CBS(CB_CROWNCHANGE, arena, CrownChangeFunc,
				(p, FALSE, KOTH_CAUSE_LEAVE_GAME));
		UNLOCK();
	}
}


local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	if (newship == SHIP_SPEC)
		paction(p, 0, 0);
}


local void mykill(Arena *arena, Player *killer, Player *killed,
		int bounty, int flags, int *pts, int *green)
{
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	struct koth_player_data *erdata = PPDATA(killer, pkey);
	struct koth_player_data *eddata = PPDATA(killed, pkey);

	LOCK();

	if (eddata->crown)
	{
		eddata->deaths++;
		if (eddata->deaths > adata->deathcount)
		{
			remove_crown(killed);
			DO_CBS(CB_CROWNCHANGE, arena, CrownChangeFunc,
					(killed, FALSE, KOTH_CAUSE_TOO_MANY_DEATHS));
		}
	}

	if (erdata->crown)
	{
		/* doesn't work now:
		if (!eddata->crown && bounty >= adata->killminbty)
			add_crown_time(killer, adata->killadjusttime);
			 */
		set_crown_time(killer, adata->expiretime);
		/* this is just extending the expiry time; no change in status,
		 * so no need to call callbacks. */
	}
	else
	{
		/* no crown. if the killed does, count this one */
		if (eddata->crown && adata->recoverkills > 0)
		{
			int left;

			erdata->crownkills++;
			left = adata->recoverkills - erdata->crownkills;

			if (left <= 0)
			{
				erdata->crownkills = 0;
				erdata->deaths = 0;
				set_crown_time(killer, adata->expiretime);
				chat->SendMessage(killer, "You earned back a crown.");
				lm->LogP(L_DRIVEL, "koth", killer, "earned back a crown");
				DO_CBS(CB_CROWNCHANGE, arena, CrownChangeFunc,
						(killer, TRUE, KOTH_CAUSE_RECOVERED));
			}
			else
				chat->SendMessage(killer, "%d kill%s left to earn back a crown.",
						left, left == 1 ? "" : "s");
		}
	}

	UNLOCK();
}


local void p_kothexpired(Player *p, byte *pkt, int len)
{
	if (len != 1 || !p->arena)
	{
		lm->LogP(L_MALICIOUS, "koth", p, "bad KoTH expired packet len=%i", len);
		return;
	}

	LOCK();
	remove_crown(p);
	DO_CBS(CB_CROWNCHANGE, p->arena, CrownChangeFunc,
			(p, FALSE, KOTH_CAUSE_EXPIRED));
	UNLOCK();
}


local void Cresetkoth(const char *cmd, const char *params, Player *p, const Target *t)
{
	Arena *arena = p->arena;
	struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
	/* check to be sure koth is even running in this arena */
	LOCK();
	if (adata->expiretime)
		start_koth(arena);
	UNLOCK();
}

EXPORT const char info_koth[] = CORE_MOD_INFO("koth");

EXPORT int MM_koth(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		net = mm->GetInterface(I_NET, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		if (!net || !chat || !pd || !aman || !cfg || !cmd || !lm || !ml || !stats)
			return MM_FAIL;

		akey = aman->AllocateArenaData(sizeof(struct koth_arena_data));
		pkey = pd->AllocatePlayerData(sizeof(struct koth_player_data));
		if (akey == -1 || pkey == -1) return MM_FAIL;

		cmd->AddCommand("resetkoth", Cresetkoth, ALLARENAS, NULL);

		net->AddPacket(C2S_KOTHEXPIRED, p_kothexpired);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("resetkoth", Cresetkoth, ALLARENAS);
		net->RemovePacket(C2S_KOTHEXPIRED, p_kothexpired);
		ml->ClearTimer(timer, NULL);

		aman->FreeArenaData(akey);
		pd->FreePlayerData(pkey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(stats);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		load_settings(arena);
		mm->RegCallback(CB_PLAYERACTION, paction, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_KILL, mykill, arena);
		ml->SetTimer(timer, 500, 500, (void*)arena, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct koth_arena_data *adata = P_ARENA_DATA(arena, akey);
		adata->expiretime = 0;
		mm->UnregCallback(CB_PLAYERACTION, paction, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_KILL, mykill, arena);
		ml->ClearTimer(timer, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

