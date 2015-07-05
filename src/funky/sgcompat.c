
/* dist: public */

#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "idle.h"

local Icmdman *cmd;
local Iplayerdata *pd;
local Ilogman *lm;
local Ichat *chat;
local Ilagquery *lagq;
local Inet *net;
local Igroupman *groupman;
local Iidle *idle;

local HashTable *aliases;


local void rewritecommand(int initial, char *buf, int len)
{
	char key[64], *k, *b;
	const char *new;

	if (initial != '?' && initial != '*')
		return;

	key[0] = initial;
	k = key + 1;
	b = buf;
	while (*b && *b != ' ' && *b != '=')
	{
		*k++ = *b++;
		if ((k-key) >= sizeof(key))
			return;
	}
	*k = '\0';

	new = HashGetOne(aliases, key);
	if (new && (strlen(new) + strlen(b) + 1) < len)
	{
		memmove(buf + strlen(new), b, strlen(b) + 1);
		memcpy(buf, new, strlen(new));
		lm->Log(L_DRIVEL, "<sgcompat> command rewritten from '%s' to '%s'",
				key, new);
	}
}


local void setup_aliases(void)
{
	aliases = HashAlloc();
#define ALIAS(x, y) HashAdd(aliases, x, y)
	ALIAS("?recycle",     "recyclearena");
	ALIAS("?get",         "geta");
	ALIAS("?set",         "seta");
	ALIAS("?setlevel",    "putmap");
	ALIAS("*listban",     "listmidbans");
	ALIAS("*removeban",   "delmidban");
	ALIAS("*kill",        "kick -m");
	ALIAS("*log",         "lastlog");
	ALIAS("*zone",        "az");
	ALIAS("*flags",       "flaginfo");
	ALIAS("*locate",      "find");
	ALIAS("*recycle",     "shutdown -r");
	ALIAS("*sysop",       "setgroup sysop");
	ALIAS("*smoderator",  "setgroup smod");
	ALIAS("*moderator",   "setgroup mod");
	ALIAS("*arena",       "aa");
	ALIAS("*einfo",       "sg_einfo");
	ALIAS("*tinfo",       "sg_tinfo");
	ALIAS("*listmod",     "sg_listmod");
	ALIAS("*where",       "sg_where");
	ALIAS("*info",        "sg_info");
	ALIAS("*lag",         "sg_lag");
#undef ALIAS
}

local void cleanup_aliases(void)
{
	HashFree(aliases);
	aliases = NULL;
}


local void Csg_einfo(const char *tc, const char *params, Player *p, const Target *target)
{
	int i, drift = 0, count = 0;
	struct TimeSyncHistory history;
	Player *t = target->type == T_PLAYER ? target->u.p : p;

	if (IS_STANDARD(t))
	{
		lagq->QueryTimeSyncHistory(t, &history);
		for (i = 1; i < TIME_SYNC_SAMPLES; i++)
		{
			int j = (i + history.next) % TIME_SYNC_SAMPLES;
			int k = (i + history.next - 1) % TIME_SYNC_SAMPLES;
			int delta = (history.servertime[j] - history.clienttime[j]) -
			            (history.servertime[k] - history.clienttime[k]);
			if (delta >= -10000 && delta <= 10000)
			{
				drift += delta;
				count++;
			}
		}
	}

	chat->SendMessage(p,
			"%s: UserId: %d  Res: %dx%d  Client: %s  Proxy: %s  Idle: %d s  Timer drift: %d",
			t->name,
			/* FIXME: get userid */
			-1,
			t->xres, t->yres,
			t->clientname,
			/* FIXME: get proxy */
			"(unknown)",
			idle ? idle->GetIdle(t) : -1,
			count ? drift/count : 0);
}


local void Csg_tinfo(const char *tc, const char *params, Player *p, const Target *target)
{
	int i;
	struct TimeSyncHistory history;
	Player *t = target->type == T_PLAYER ? target->u.p : p;

	if (!IS_STANDARD(t)) return;

	lagq->QueryTimeSyncHistory(t, &history);

	chat->SendMessage(p, "%11s %11s %11s",
			"ServerTime", "UserTime", "Diff");
	for (i = 0; i < TIME_SYNC_SAMPLES; i++)
	{
		int j = (i + history.next) % TIME_SYNC_SAMPLES;
		chat->SendMessage(p, "%11d %11d %11d",
				history.servertime[j], history.clienttime[j],
				history.servertime[j] - history.clienttime[j]);
	}
}


local void Csg_listmod(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *i;
	Link *link;
	const char *grp;

	if (!groupman) return;

	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING &&
		    strcmp((grp = groupman->GetGroup(i)), "default") != 0)
			chat->SendMessage(p, "%s - %s - %s", i->name, grp, i->arena->name);
	pd->Unlock();
}


local void Csg_where(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	int x = t->position.x >> 4;
	int y = t->position.y >> 4;
	chat->SendMessage(p, "%s: %c%d", t->name, 'A' + (x * 20 / 1024), (y * 20 / 1024) + 1);
}


/* weight reliable ping twice the s2c and c2s */
#define AVG_PING(field) (pping.field + cping.field + 2*rping.field) / 4

local void Csg_info(const char *tc, const char *params, Player *p, const Target *target)
{
	struct PingSummary pping, cping, rping;
	struct PLossSummary ploss;
	struct net_client_stats stats;
	ticks_t tm;
	Player *t = target->type == T_PLAYER ? target->u.p : p;

	tm = TICK_DIFF(current_ticks(), t->connecttime);
	if (IS_STANDARD(t))
		net->GetClientStats(t, &stats);
	else
		memset(&stats, 0, sizeof(stats));
	lagq->QueryPPing(t, &pping);
	lagq->QueryCPing(t, &cping);
	lagq->QueryRPing(t, &rping);
	lagq->QueryPLoss(t, &ploss);

	chat->SendMessage(p,
			"IP:%s  TimeZoneBias:%d  Freq:%d  TypedName:%s  Demo:0  MachineId:%d",
			t->ipaddr,
			/* FIXME: get tz */
			0,
			t->p_freq,
			/* FIXME: make this _typed_ name */
			t->name,
			t->macid);
	chat->SendMessage(p,
			"Ping:%dms  LowPing:%dms  HighPing:%dms  AvePing:%dms",
			AVG_PING(cur), AVG_PING(min), AVG_PING(max), AVG_PING(avg));
	chat->SendMessage(p,
			"LOSS: S2C:%.1f%%  C2S:%.1f%%  S2CWeapons:%.1f%%  S2C_RelOut:%d(%d)",
			100.0*ploss.s2c, 100.0*ploss.c2s, 100.0*ploss.s2cwpn,
			/* FIXME: get this data: unacked rels, s2c seqnum */
			0, 0);
	chat->SendMessage(p,
			"S2C:%d-->%d  C2S:%d-->%d",
			0, 0, 0, 0);
	chat->SendMessage(p,
			"C2S CURRENT: Slow:%d Fast:%d %.1f%%   TOTAL: Slow:%d Fast:%d %.1f%%",
			0, 0, 0.0, 0, 0, 0.0);
	chat->SendMessage(p,
			"S2C CURRENT: Slow:%d Fast:%d %.1f%%   TOTAL: Slow:%d Fast:%d %.1f%%",
			cping.s2cslowcurrent, cping.s2cfastcurrent, 0.0,
			cping.s2cslowtotal, cping.s2cfasttotal, 0.0);
	chat->SendMessage(p,
			"TIME: Session:%5d:%02d:%02d  Total:%5d:%02d:%02d  Created: %d-%d-%d %02d:%02d:%02d",
			tm / 3600, (tm / 60) % 60, tm % 60,
			/* FIXME: get this data */
			0, 0, 0,
			0, 0, 0, 0, 0, 0);
	chat->SendMessage(p,
			"Bytes/Sec:%u  LowBandwidth:%d  MessageLogging:%d  ConnectType:%s",
			tm ? stats.bytesent / tm : 0, 0, 0, "Unknown");
}


local void Csg_lag(const char *tc, const char *params, Player *p, const Target *target)
{
	struct PingSummary pping, cping, rping;
	struct PLossSummary ploss;
	Player *t = target->type == T_PLAYER ? target->u.p : p;

	lagq->QueryPPing(t, &pping);
	lagq->QueryCPing(t, &cping);
	lagq->QueryRPing(t, &rping);
	lagq->QueryPLoss(t, &ploss);

	chat->SendMessage(p,
			"PING Current:%d ms  Average:%d ms  Low:%d ms  High:%d ms  "
			"S2C: %.1f%%  C2S: %.1f%%  S2CWeapons: %.1f%%",
			AVG_PING(cur), AVG_PING(avg), AVG_PING(min), AVG_PING(max),
			100.0*ploss.s2c, 100.0*ploss.c2s, 100.0*ploss.s2cwpn);
}

#undef AVG_PING

EXPORT const char info_sgcompat[] = CORE_MOD_INFO("sgcompat");

EXPORT int MM_sgcompat(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lagq = mm->GetInterface(I_LAGQUERY, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		groupman = mm->GetInterface(I_GROUPMAN, ALLARENAS);
		idle = mm->GetInterface(I_IDLE, ALLARENAS);

		cmd->AddCommand("sg_einfo", Csg_einfo, ALLARENAS, NULL);
		cmd->AddCommand("sg_tinfo", Csg_tinfo, ALLARENAS, NULL);
		cmd->AddCommand("sg_listmod", Csg_listmod, ALLARENAS, NULL);
		cmd->AddCommand("sg_where", Csg_where, ALLARENAS, NULL);
		cmd->AddCommand("sg_info", Csg_info, ALLARENAS, NULL);
		cmd->AddCommand("sg_lag", Csg_lag, ALLARENAS, NULL);

		setup_aliases();
		mm->RegCallback(CB_REWRITECOMMAND, rewritecommand, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("sg_einfo", Csg_einfo, ALLARENAS);
		cmd->RemoveCommand("sg_tinfo", Csg_tinfo, ALLARENAS);
		cmd->RemoveCommand("sg_listmod", Csg_listmod, ALLARENAS);
		cmd->RemoveCommand("sg_where", Csg_where, ALLARENAS);
		cmd->RemoveCommand("sg_info", Csg_info, ALLARENAS);
		cmd->RemoveCommand("sg_lag", Csg_lag, ALLARENAS);
		mm->UnregCallback(CB_REWRITECOMMAND, rewritecommand, ALLARENAS);
		cleanup_aliases();
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lagq);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(groupman);
		mm->ReleaseInterface(idle);
		return MM_OK;
	}
	else
		return MM_FAIL;
}
