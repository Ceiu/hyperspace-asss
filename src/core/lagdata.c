
/* dist: public */

#include <string.h>

#include "asss.h"

#define MAX_PING 10000
#define PLOSS_MIN_PACKETS 200

#define USE_BUCKETS

#ifdef CFG_PEDANTIC_LOCKING
#define PEDANTIC_LOCK() LOCK()
#define PEDANTIC_UNLOCK() UNLOCK()
#else
#define PEDANTIC_LOCK()
#define PEDANTIC_UNLOCK()
#endif

local pthread_mutex_t mtx;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

local Iplayerdata *pd;


/* we're going to keep track of ppk pings and rel delay pings
 * separately, because in some sense they're different protocols.
 *
 * pings will go in buckets so we can make histograms later. the buckets
 * will be 20 ms wide, and go from 0 up to 500 ms, so there will be 25
 * buckets.
 */

#ifdef USE_BUCKETS

#ifdef CFG_LAG_BUCKETS
#define MAX_BUCKET CFG_LAG_BUCKETS
#else
#define MAX_BUCKET 25
#endif

#ifdef CFG_LAG_BUCKET_WIDTH
#define BUCKET_WIDTH CFG_LAG_BUCKET_WIDTH
#else
#define BUCKET_WIDTH 20
#endif

#define MS_TO_BUCKET(ms) \
( ((ms) < 0) ? 0 : ( ((ms) < (MAX_BUCKET * BUCKET_WIDTH)) ? ((ms) / BUCKET_WIDTH) : MAX_BUCKET ) )

#endif

struct PingData
{
#ifdef USE_BUCKETS
	int buckets[MAX_BUCKET];
#endif
	/* these are all in milliseconds */
	int current, avg, max, min;
};

typedef struct
{
	struct PingData pping; /* position packet ping */
	struct PingData rping; /* reliable ping */
	struct ClientLatencyData cping; /* client-reported ping */
	struct TimeSyncData ploss; /* basic ploss info */
	struct TimeSyncHistory timesync; /* time sync data */
	struct ReliableLagData reldata; /* reliable layer data */
	unsigned int wpnsent, wpnrcvd, lastwpnsent;
} LagData;


/* an array of pointers to lagdata */
local int lagkey;



local void add_ping(struct PingData *pd, int ping)
{
	/* prevent horribly incorrect pings from messing up stats */
	if (ping > MAX_PING)
		ping = MAX_PING;
	if (ping < 0)
		ping = 0;

	pd->current = ping;
#ifdef USE_BUCKETS
	pd->buckets[MS_TO_BUCKET(ping)]++;
#endif
	pd->avg = (pd->avg * 7 + ping) / 8;
	if (ping < pd->min)
		pd->min = ping;
	if (ping > pd->max)
		pd->max = ping;
}


local void Position(Player *p, int ping, int cliping, unsigned int wpnsent)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	/* convert one-way to round-trip */
	add_ping(&ld->pping, ping * 2);
	ld->lastwpnsent = wpnsent;
	PEDANTIC_UNLOCK();
}


local void RelDelay(Player *p, int ping)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	add_ping(&ld->rping, ping);
	PEDANTIC_UNLOCK();
}


local void ClientLatency(Player *p, struct ClientLatencyData *d)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	ld->cping = *d;
	ld->wpnrcvd = d->weaponcount;
	ld->wpnsent = ld->lastwpnsent;
	PEDANTIC_UNLOCK();
}


local void TimeSync(Player *p, struct TimeSyncData *d)
{
	int this;
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	ld->ploss = *d;
	this = ld->timesync.next;
	ld->timesync.servertime[this] = d->s_time;
	ld->timesync.clienttime[this] = d->c_time;
	ld->timesync.next = (this + 1) % TIME_SYNC_SAMPLES;
	/* TODO: calculate drift here */
	PEDANTIC_UNLOCK();
}


local void RelStats(Player *p, struct ReliableLagData *d)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	ld->reldata = *d;
	PEDANTIC_UNLOCK();
}



local void QueryPPing(Player *p, struct PingSummary *ping)
{
	LagData *ld = PPDATA(p, lagkey);
	struct PingData *pd = &ld->pping;

	PEDANTIC_LOCK();
	ping->cur = pd->current;
	ping->avg = pd->avg;
	ping->min = pd->min;
	ping->max = pd->max;
	PEDANTIC_UNLOCK();
}


local void QueryCPing(Player *p, struct PingSummary *ping)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	/* convert units: values in cping are in ticks.
	 * we want milliseconds. */
	ping->cur = ld->cping.lastping * 10;
	ping->avg = ld->cping.averageping * 10;
	ping->min = ld->cping.lowestping * 10;
	ping->max = ld->cping.highestping * 10;
	/* special stuff for client ping: */
	ping->s2cslowtotal = ld->cping.s2cslowtotal;
	ping->s2cfasttotal = ld->cping.s2cfasttotal;
	ping->s2cslowcurrent = ld->cping.s2cslowcurrent;
	ping->s2cfastcurrent = ld->cping.s2cfastcurrent;
	PEDANTIC_UNLOCK();
}


local void QueryRPing(Player *p, struct PingSummary *ping)
{
	LagData *ld = PPDATA(p, lagkey);
	struct PingData *pd = &ld->rping;
	PEDANTIC_LOCK();
	ping->cur = pd->current;
	ping->avg = pd->avg;
	ping->min = pd->min;
	ping->max = pd->max;
	PEDANTIC_UNLOCK();
}



local void QueryPLoss(Player *p, struct PLossSummary *d)
{
	LagData *ld = PPDATA(p, lagkey);
	int s, r;
	PEDANTIC_LOCK();
	s = ld->ploss.s_pktsent;
	r = ld->ploss.c_pktrcvd;
	d->s2c = s > PLOSS_MIN_PACKETS ? (double)(s - r) / (double)s : 0.0;

	s = ld->ploss.c_pktsent;
	r = ld->ploss.s_pktrcvd;
	d->c2s = s > PLOSS_MIN_PACKETS ? (double)(s - r) / (double)s : 0.0;

	s = ld->wpnsent;
	r = ld->wpnrcvd;
	d->s2cwpn = s > PLOSS_MIN_PACKETS ? (double)(s - r) / (double)s : 0.0;
	PEDANTIC_UNLOCK();
}


local void QueryRelLag(Player *p, struct ReliableLagData *d)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	*d = ld->reldata;
	PEDANTIC_UNLOCK();
}


local void QueryTimeSyncHistory(Player *p, struct TimeSyncHistory *d)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	*d = ld->timesync;
	PEDANTIC_UNLOCK();
}


local void do_hist(
		Player *p,
		struct PingData *pd,
		void (*callback)(Player *p, int bucket, int count, int maxcount, void *clos),
		void *clos)
{
#ifdef USE_BUCKETS
	int i, max = 0;

	for (i = 0; i < MAX_BUCKET; i++)
		if (pd->buckets[i] > max)
			max = pd->buckets[i];

	for (i = 0; i < MAX_BUCKET; i++)
		callback(p, i, pd->buckets[i], max, clos);
#endif
}

local void DoPHistogram(Player *p,
		void (*callback)(Player *p, int bucket, int count, int maxcount, void *clos),
		void *clos)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	do_hist(p, &ld->pping, callback, clos);
	PEDANTIC_UNLOCK();
}

local void DoRHistogram(Player *p,
		void (*callback)(Player *p, int bucket, int count, int maxcount, void *clos),
		void *clos)
{
	LagData *ld = PPDATA(p, lagkey);
	PEDANTIC_LOCK();
	do_hist(p, &ld->rping, callback, clos);
	PEDANTIC_UNLOCK();
}


local void paction(Player *p, int action, Arena *arena)
{
	LagData *ld = PPDATA(p, lagkey);
	if (action == PA_CONNECT)
		memset(ld, 0, sizeof(*ld));
}



local Ilagcollect lcint =
{
	INTERFACE_HEAD_INIT(I_LAGCOLLECT, "lagdata")
	Position, RelDelay, ClientLatency,
	TimeSync, RelStats
};


local Ilagquery lqint =
{
	INTERFACE_HEAD_INIT(I_LAGQUERY, "lagdata")
	QueryPPing, QueryCPing, QueryRPing,
	QueryPLoss, QueryRelLag,
	QueryTimeSyncHistory,
	DoPHistogram, DoRHistogram
};

EXPORT const char info_lagdata[] = CORE_MOD_INFO("lagdata");

EXPORT int MM_lagdata(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!pd) return MM_FAIL;
		lagkey = pd->AllocatePlayerData(sizeof(LagData));
		if (lagkey == -1) return MM_FAIL;

		pthread_mutex_init(&mtx, NULL);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		mm->RegInterface(&lcint, ALLARENAS);
		mm->RegInterface(&lqint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&lcint, ALLARENAS))
			return MM_FAIL;
		if (mm->UnregInterface(&lqint, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		pthread_mutex_destroy(&mtx);
		pd->FreePlayerData(lagkey);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}

