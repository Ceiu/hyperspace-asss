
/* dist: public */

#include <time.h>
#include <string.h>

#include "asss.h"
#include "persist.h"


#define KEY_STATS 1
#define KEY_ENDING_TIME 2


/* structs */

/* the treap key is the statid */
typedef struct stat_info
{
	TreapHead head;
	int value;
	time_t started; /* for timers only */
	byte dirty;
} stat_info;


/* global data */

local Imodman *mm;
local Inet *net;
local Iplayerdata *pd;
local Icmdman *cmd;
local Ipersist *persist;
local Ichat *chat;

/* this mutex protects these treaps */
#define LOCK_PLAYER(pd) pthread_mutex_lock(&pd->mtx)
#define UNLOCK_PLAYER(pd) pthread_mutex_unlock(&pd->mtx)
typedef struct
{
	pthread_mutex_t mtx;
	stat_info *forever;
	stat_info *reset;
	stat_info *game;
} pdata;

local int pdkey;


/* functions */

local void newplayer(Player *p, int new)
{
	pdata *stats = PPDATA(p, pdkey);
	if (new)
	{
		pthread_mutex_init(&stats->mtx, NULL);
		stats->forever = NULL;
		stats->reset = NULL;
		stats->game = NULL;
	}
	else
	{
		pthread_mutex_destroy(&stats->mtx);
		TrEnum((TreapHead*)stats->forever, tr_enum_afree, NULL);
		TrEnum((TreapHead*)stats->reset, tr_enum_afree, NULL);
		TrEnum((TreapHead*)stats->game, tr_enum_afree, NULL);
	}
}


local stat_info **get_array(pdata *stats, int interval)
{
	switch (interval)
	{
		case INTERVAL_FOREVER: return &stats->forever;
		case INTERVAL_RESET: return &stats->reset;
		case INTERVAL_GAME: return &stats->game;
		default: return NULL;
	}
}

local stat_info *new_stat(int stat)
{
	stat_info *si = amalloc(sizeof(*si));
	si->head.key = stat;
	return si;
}


local void IncrementStat(Player *p, int stat, int amount)
{
	pdata *stats = PPDATA(p, pdkey);
	stat_info *si;

	LOCK_PLAYER(stats);

#define INC(iv) \
	if ((si = (stat_info*)TrGet((TreapHead*)stats->iv, stat)) == NULL) \
	{ \
		si = new_stat(stat); \
		TrPut((TreapHead**)(&stats->iv), (TreapHead*)si); \
	} \
	si->value += amount; \
	si->dirty = 1;

	INC(forever)
	INC(reset)
	INC(game)
#undef INC

	UNLOCK_PLAYER(stats);
}


local inline void update_timer(stat_info *si, time_t tm)
{
	if (si->started)
	{
		si->value += (tm - si->started);
		si->started = tm;
		si->dirty = 1;
	}
}

local inline void start_timer(stat_info *si, time_t tm)
{
	if (si->started)
		update_timer(si, tm);
	else
		si->started = tm;
}

local inline void stop_timer(stat_info *si, time_t tm)
{
	update_timer(si, tm);
	si->started = 0;
}


local void StartTimer(Player *p, int stat)
{
	pdata *stats = PPDATA(p, pdkey);
	stat_info *si;
	time_t tm = time(NULL);

	LOCK_PLAYER(stats);

#define INC(iv) \
	if ((si = (stat_info*)TrGet((TreapHead*)stats->iv, stat)) == NULL) \
	{ \
		si = new_stat(stat); \
		TrPut((TreapHead**)(&stats->iv), (TreapHead*)si); \
	} \
	start_timer(si, tm);

	INC(forever)
	INC(reset)
	INC(game)
#undef INC

	UNLOCK_PLAYER(stats);
}


local void StopTimer(Player *p, int stat)
{
	pdata *stats = PPDATA(p, pdkey);
	stat_info *si;
	time_t tm = time(NULL);

	LOCK_PLAYER(stats);

#define INC(iv) \
	if ((si = (stat_info*)TrGet((TreapHead*)stats->iv, stat)) == NULL) \
	{ \
		si = new_stat(stat); \
		TrPut((TreapHead**)(&stats->iv), (TreapHead*)si); \
	} \
	stop_timer(si, tm);

	INC(forever)
	INC(reset)
	INC(game)
#undef INC

	UNLOCK_PLAYER(stats);
}


/* call with player locked */
local inline void set_stat(int stat, stat_info **arr, int val)
{
	stat_info *si = (stat_info*)TrGet((TreapHead*)(*arr), stat);
	if (!si)
	{
		si = new_stat(stat);
		TrPut((TreapHead**)(arr), (TreapHead*)si);
	}
	si->value = val;
	si->started = 0; /* setting a stat stops any timers that were running */
	si->dirty = 1;
}

local void SetStat(Player *p, int stat, int interval, int amount)
{
	pdata *stats = PPDATA(p, pdkey);
	stat_info **arr = get_array(stats, interval);
	if (arr)
	{
		LOCK_PLAYER(stats);
		set_stat(stat, arr, amount);
		UNLOCK_PLAYER(stats);
	}
}


/* call with player locked */
local inline int get_stat(int stat, stat_info **arr)
{
	stat_info *si = (stat_info*)TrGet((TreapHead*)(*arr), stat);
	return si ? si->value : 0;
}

local int GetStat(Player *p, int stat, int iv)
{
	pdata *stats = PPDATA(p, pdkey);
	int val;
	stat_info **arr = get_array(stats, iv);
	if (!arr)
		return 0;
	LOCK_PLAYER(stats);
	val = get_stat(stat, arr);
	UNLOCK_PLAYER(stats);
	return val;
}


local void scorereset_enum(TreapHead *node, void *clos)
{
	stat_info *si = (stat_info*)node;
	/* keep timers running. if the timer was running while this happens,
	 * only the time from this point will be counted. the time from the
	 * timer start up to this point will be discarded. */
	update_timer(si, *(time_t*)clos);
	si->value = 0;
	si->dirty = 1;
}

local void ScoreReset(Player *p, int iv)
{
	pdata *stats = PPDATA(p, pdkey);
	stat_info **arr = get_array(stats, iv);
	time_t tm = time(NULL);
	if (!arr) return;
	LOCK_PLAYER(stats);
	TrEnum((TreapHead*)(*arr), scorereset_enum, (void*)&tm);
	UNLOCK_PLAYER(stats);
}


/* utility functions for doing stuff to stat treaps */

#ifdef this_wont_be_necessary_until_new_protocol
local void update_timers_work(TreapHead *node, void *clos)
{
	update_timer((stat_info*)node, *(time_t*)clos);
}

local void update_timers(stat_info *si, time_t now)
{
	TrEnum((TreapHead*)si, update_timers_work, (void*)&now);
}
#endif


/* stuff dealing with stat protocol */

local void dirty_count_work(TreapHead *node, void *clos)
{
	stat_info *si = (stat_info*)node;
	if (si->dirty)
	{
		(*(int*)clos)++;
		si->dirty = 0;
	}
}

/* note that this clears all the dirty bits! */
local int dirty_count(stat_info *si)
{
	int c = 0;
	TrEnum((TreapHead*)si, dirty_count_work, &c);
	return c;
}

#include "packets/scoreupd.h"

local void SendUpdates(Player *exclude)
{
	pdata *stats;
	struct ScorePacket sp = { S2C_SCOREUPDATE };
	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER_P(p, stats, pdkey)
		if (p->status == S_PLAYING && p != exclude)
		{
			LOCK_PLAYER(stats);
			if (dirty_count(stats->reset))
			{
				sp.pid = p->pid;
				sp.killpoints = p->pkt.killpoints = get_stat(STAT_KILL_POINTS, &stats->reset);
				sp.flagpoints = p->pkt.flagpoints = get_stat(STAT_FLAG_POINTS, &stats->reset);
				sp.kills = p->pkt.wins = get_stat(STAT_KILLS, &stats->reset);
				sp.deaths = p->pkt.losses = get_stat(STAT_DEATHS, &stats->reset);

				UNLOCK_PLAYER(stats);

				net->SendToArena(
						p->arena,
						exclude,
						(unsigned char*)&sp,
						sizeof(sp),
						NET_UNRELIABLE | NET_PRI_N1);
			}
			else
				UNLOCK_PLAYER(stats);
		}
	pd->Unlock();
}


/* stuff dealing with persistant storage */

struct stored_stat
{
	unsigned short stat;
	int value;
};

struct get_stats_clos
{
	struct stored_stat *ss;
	int left;
	time_t tm;
};

local void get_stats_enum(TreapHead *node, void *clos_)
{
	struct get_stats_clos *clos = (struct get_stats_clos*)clos_;
	struct stat_info *si = (stat_info*)node;
	if (si->value != 0 && clos->left > 0)
	{
		update_timer(si, clos->tm);
		clos->ss->stat = node->key;
		clos->ss->value = si->value;
		clos->ss++;
		clos->left--;
	}
}

local void clear_stats_enum(TreapHead *node, void *clos)
{
	stat_info *si = (stat_info*)node;
	if (si->value)
		si->dirty = 1;
	si->started = 0;
	si->value = 0;
}

#define DO_PERSISTENT_DATA(ival, code)                                         \
                                                                               \
local int get_##ival##_data(Player *p, void *data, int len, void *v)           \
{                                                                              \
    pdata *stats = PPDATA(p, pdkey);                                           \
    struct get_stats_clos clos = { data, len / sizeof(struct stored_stat),     \
        time(NULL) };                                                          \
    LOCK_PLAYER(stats);                                                        \
    TrEnum((TreapHead*)stats->ival, get_stats_enum, &clos);                    \
    UNLOCK_PLAYER(stats);                                                      \
    return (byte*)clos.ss - (byte*)data;                                       \
}                                                                              \
                                                                               \
local void set_##ival##_data(Player *p, void *data, int len, void *v)          \
{                                                                              \
    pdata *stats = PPDATA(p, pdkey);                                           \
    struct stored_stat *ss = (struct stored_stat*)data;                        \
    LOCK_PLAYER(stats);                                                        \
    for ( ; len >= sizeof(struct stored_stat);                                 \
            ss++, len -= sizeof(struct stored_stat))                           \
        set_stat(ss->stat, &stats->ival, ss->value);                           \
    UNLOCK_PLAYER(stats);                                                      \
}                                                                              \
                                                                               \
local void clear_##ival##_data(Player *p, void *v)                             \
{                                                                              \
    pdata *stats = PPDATA(p, pdkey);                                           \
    LOCK_PLAYER(stats);                                                        \
    TrEnum((TreapHead*)stats->ival, clear_stats_enum, NULL);                   \
    UNLOCK_PLAYER(stats);                                                      \
}                                                                              \
                                                                               \
local PlayerPersistentData my_##ival##_data =                                  \
{                                                                              \
    KEY_STATS, code, PERSIST_ALLARENAS,                                        \
    get_##ival##_data, set_##ival##_data, clear_##ival##_data                  \
};

DO_PERSISTENT_DATA(forever, INTERVAL_FOREVER)
DO_PERSISTENT_DATA(reset, INTERVAL_RESET)
DO_PERSISTENT_DATA(game, INTERVAL_GAME)

#undef DO_PERSISTENT_DATA


/* interval ending time */

local int get_ending_time(Arena *arena, void *data, int len, void *v)
{
	time(data);
	return sizeof(time_t);
}

local void set_ending_time(Arena *arena, void *data, int len, void *v) { /* noop */ }

local void clear_ending_time(Arena *arena, void *v) { /* noop */ }

local ArenaPersistentData my_reset_end_time_data =
{
	KEY_ENDING_TIME, INTERVAL_RESET, PERSIST_ALLARENAS,
	get_ending_time, set_ending_time, clear_ending_time
};

local ArenaPersistentData my_game_end_time_data =
{
	KEY_ENDING_TIME, INTERVAL_GAME, PERSIST_ALLARENAS,
	get_ending_time, set_ending_time, clear_ending_time
};




local helptext_t stats_help =
"Targets: player or none\n"
"Args: [{forever}|{game}|{reset}]\n"
"Prints out some basic statistics about the target player, or if no\n"
"target, yourself. An interval name can be specified as an argument.\n"
"By default, the per-reset interval is used.\n";

local void enum_send_msg(TreapHead *node, void *clos)
{
	struct stat_info *si = (struct stat_info*)node;
	chat->SendMessage((Player*)clos, "  %s: %d", get_stat_name(si->head.key), si->value);
}

local void Cstats(const char *tc, const char *params, Player *p, const Target *target)
{
    stat_info *arr;
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	pdata *stats = PPDATA(t, pdkey);

	if (!strcasecmp(params, "forever"))
		arr = stats->forever;
	else if (!strcasecmp(params, "game"))
		arr = stats->game;
	else
		arr = stats->reset;

	if (chat)
	{
		chat->SendMessage(p,
				"The server is keeping track of the following stats about %s:",
				target->type == T_PLAYER ? t->name : "you");
		LOCK_PLAYER(stats);
		TrEnum((TreapHead*)arr, enum_send_msg, p);
		UNLOCK_PLAYER(stats);
	}
}


local Istats _myint =
{
	INTERFACE_HEAD_INIT(I_STATS, "stats")
	IncrementStat, StartTimer, StopTimer,
	SetStat, GetStat, SendUpdates,
	ScoreReset
};

EXPORT const char info_stats[] = CORE_MOD_INFO("stats");

EXPORT int MM_stats(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		if (!pd || !net || !cmd || !persist) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		cmd->AddCommand("stats", Cstats, ALLARENAS, stats_help);

		persist->RegPlayerPD(&my_forever_data);
		persist->RegPlayerPD(&my_reset_data);
		persist->RegPlayerPD(&my_game_data);
		persist->RegArenaPD(&my_reset_end_time_data);
		persist->RegArenaPD(&my_game_end_time_data);

		mm->RegCallback(CB_INTERVAL_ENDED, SendUpdates, ALLARENAS);
		mm->RegCallback(CB_NEWPLAYER, newplayer, ALLARENAS);

		mm->RegInterface(&_myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_INTERVAL_ENDED, SendUpdates, ALLARENAS);
		mm->UnregCallback(CB_NEWPLAYER, newplayer, ALLARENAS);
		persist->UnregPlayerPD(&my_forever_data);
		persist->UnregPlayerPD(&my_reset_data);
		persist->UnregPlayerPD(&my_game_data);
		persist->UnregArenaPD(&my_reset_end_time_data);
		persist->UnregArenaPD(&my_game_end_time_data);

		cmd->RemoveCommand("stats", Cstats, ALLARENAS);

		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}

