
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "asss.h"
#include "persist.h"

/* extra includes */
#include "packets/balls.h"


/* defines */

#define BALL_SEND_PRI NET_PRI_P4

#define LOCK_STATUS(arena) \
	pthread_mutex_lock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))
#define UNLOCK_STATUS(arena) \
	pthread_mutex_unlock((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey))


/* internal structs */
typedef struct BallSpawn
{
	int x, y, r;
} BallSpawn;

typedef struct InternalBallData
{
	/* array of spawn locations */
	BallSpawn *spawns;

	/* when we last send ball packets out to the arena. */
	ticks_t lastSendTime;

	/* number of different spawn locations to use, between 1 and MAXBALLS */
	int spawnCount;

	/* this is the delay between a goal and the ball respawning. */
	int cfg_respawnTimeAfterGoal;

	/* these are in centiseconds. the timer event runs with a resolution
	 * of 25 centiseconds, though, so that's the best resolution you're
	 * going to get. */
	int cfg_sendTime;

	/* this controls whether a death on a goal tile scores or not */
	int cfg_deathScoresGoal;

	/* if setballcount has been used, we don't want to override that if the settings change,
	 * especially since the soccer:ballcount setting might not have been the one that changed. */
	int ballcountOverridden : 1;
} InternalBallData;

/* prototypes */
local void InitBall(Arena *arena, int bid);
local void PhaseBall(Arena *arena, int bid);
local void ChangeBallCount(Arena *arena, int count);
local void AABall(Arena *arena, int action);
local void PABall(Player *p, int action, Arena *arena);
local void ShipFreqChange(Player *, int, int, int, int);
local void BallKill(Arena *, Player *, Player *, int, int, int *, int *);

/* timers */
local int BasicBallTimer(void *);

/* packet funcs */
local void PPickupBall(Player *, byte *, int);
local void PFireBall(Player *, byte *, int);
local void PGoal(Player *, byte *, int);

/* handler functions */
local void HandleGoal(Arena *, Player *, int bid, int goalX, int goalY);

/* interface funcs */
local void SpawnBall(Arena *arena, int bid);
local void SetBallCount(Arena *arena, int ballcount);
local void PlaceBall(Arena *arena, int bid, struct BallData *newpos);
local void EndGame(Arena *arena);
local ArenaBallData * GetBallData(Arena *arena);
local void ReleaseBallData(Arena *arena);


/* local data */
local Imodman *mm;
local Inet *net;
local Iconfig *cfg;
local Ilogman *logm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Imainloop *ml;
local Imapdata *mapdata;
local Iprng *prng;

/* per arena data keys */
local int abdkey, pbdkey, mtxkey;

local Iballs _myint =
{
	INTERFACE_HEAD_INIT(I_BALLS, "ball-core")
	SetBallCount, PlaceBall, EndGame, SpawnBall,
	GetBallData, ReleaseBallData
};

EXPORT const char info_balls[] = CORE_MOD_INFO("balls");

EXPORT int MM_balls(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		logm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		if (!net || !cfg || !logm || !pd || !aman || !ml || !mapdata || !prng)
			return MM_FAIL;

		abdkey = aman->AllocateArenaData(sizeof(ArenaBallData));
		pbdkey = aman->AllocateArenaData(sizeof(InternalBallData));
		mtxkey = aman->AllocateArenaData(sizeof(pthread_mutex_t));
		if (abdkey == -1 || pbdkey == -1 || mtxkey == -1)
			return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, AABall, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, PABall, ALLARENAS);
		mm->RegCallback(CB_SHIPFREQCHANGE, ShipFreqChange, ALLARENAS);
		mm->RegCallback(CB_KILL, BallKill, ALLARENAS);

		net->AddPacket(C2S_PICKUPBALL, PPickupBall);
		net->AddPacket(C2S_SHOOTBALL, PFireBall);
		net->AddPacket(C2S_GOAL, PGoal);

		/* timers */
		ml->SetTimer(BasicBallTimer, 300, 25, NULL, NULL);

		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		ml->ClearTimer(BasicBallTimer, NULL);
		net->RemovePacket(C2S_GOAL, PGoal);
		net->RemovePacket(C2S_SHOOTBALL, PFireBall);
		net->RemovePacket(C2S_PICKUPBALL, PPickupBall);
		mm->UnregCallback(CB_KILL, BallKill, ALLARENAS);
		mm->UnregCallback(CB_SHIPFREQCHANGE, ShipFreqChange, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, PABall, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, AABall, ALLARENAS);
		aman->FreeArenaData(abdkey);
		aman->FreeArenaData(pbdkey);
		aman->FreeArenaData(mtxkey);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(logm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(prng);
		return MM_OK;
	}
	return MM_FAIL;
}



local void send_ball_packet(Arena *arena, int bid)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
	struct BallPacket bp;
	struct BallData *bd = abd->balls + bid;

	LOCK_STATUS(arena);
	bp.type = S2C_BALL;
	bp.ballid = bid;
	bp.x = bd->x;
	bp.y = bd->y;
	bp.xspeed = bd->xspeed;
	bp.yspeed = bd->yspeed;
	bp.player = bd->carrier ? bd->carrier->pid : -1;
	if (bd->state == BALL_CARRIED)
		bp.time = 0;
	else if (bd->state == BALL_ONMAP)
		bp.time = bd->time;
	else
	{
		UNLOCK_STATUS(arena);
		return;
	}
	UNLOCK_STATUS(arena);

	net->SendToArena(arena, NULL, (byte*)&bp, sizeof(bp),
			NET_UNRELIABLE | BALL_SEND_PRI);
}


local void InitBall(Arena *arena, int bid)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
	struct BallData *bd = abd->balls + bid;

	bd->state = BALL_ONMAP;
	bd->x = bd->y = 30000;
	bd->xspeed = bd->yspeed = 0;
	bd->time = (ticks_t)0; /* this is the key for making it phased */
	bd->last_update = current_ticks();
	bd->carrier = NULL;
}

local void PhaseBall(Arena *arena, int bid)
{
	LOCK_STATUS(arena);

	InitBall(arena, bid);
	send_ball_packet(arena, bid);

	UNLOCK_STATUS(arena);
}


void SpawnBall(Arena *arena, int bid)
{
	InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);

	int cx, cy, rad, x, y, idx;
	struct BallData d;

	d.state = BALL_ONMAP;
	d.xspeed = d.yspeed = 0;
	d.carrier = NULL;
	d.freq = -1;
	d.time = current_ticks();

	if (bid < pbd->spawnCount)
	{
		/* we have a defined spawn for this ball */
		idx = bid;
	}
	else
	{
		/* we don't have a specific spawn for this ball, borrow another one */
		idx = bid % pbd->spawnCount;
	}

	cx = pbd->spawns[idx].x;
	cy = pbd->spawns[idx].y;
	rad = pbd->spawns[idx].r;

	/* pick random tile */
	{
		double rndrad, rndang;
		rndrad = prng->Uniform() * (double)rad;
		rndang = prng->Uniform() * M_PI * 2.0;
		x = cx + (int)(rndrad * cos(rndang));
		y = cy + (int)(rndrad * sin(rndang));
		/* wrap around, don't clip, so radii of 2048 from a corner
		 * work properly. */
		while (x < 0) x += 1024;
		while (x > 1023) x -= 1024;
		while (y < 0) y += 1024;
		while (y > 1023) y -= 1024;

		/* ask mapdata to move it to nearest empty tile */
		mapdata->FindEmptyTileNear(arena, &x, &y);
	}

	/* place it randomly within the chosen tile */
	x <<= 4;
	y <<= 4;
	rad = prng->Get32() & 0xff;
	x |= rad / 16;
	y |= rad % 16;

	/* whew, finally place the thing */
	d.x = x; d.y = y;
	PlaceBall(arena, bid, &d);
}

void SetBallCount(Arena *arena, int ballcount)
{
	InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);

	if (ballcount < 0 || ballcount > MAXBALLS)
		return;

	/* an outside module is changing the state */
	pbd->ballcountOverridden = TRUE;

	ChangeBallCount(arena, ballcount);
}

void ChangeBallCount(Arena *arena, int ballcount)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);

	int oldc, i;

	LOCK_STATUS(arena);

	oldc = abd->ballcount;

	DO_CBS(CB_BALLCOUNTCHANGE, arena, BallCountChangeFunc, (arena, ballcount, oldc));

	if (ballcount < oldc)
	{
		/* we have to remove some balls. there is no clean way to do
		 * this (as of now). what we do is "phase" the ball so it can't
		 * be picked up by clients by setting it's last-updated time to
		 * be the highest possible time. then we send it's position to
		 * currently-connected clients. then we forget about the ball
		 * entirely so that updates are never sent again. to new
		 * players, it will look like the ball doesn't exist. */
		/* send it reliably, because clients are never going to see this
		 * ball ever again. */
		for (i = ballcount; i < oldc; i++)
			PhaseBall(arena, i);
	}

	abd->ballcount = ballcount;

	if (ballcount > oldc)
	{
		for (i = oldc; i < ballcount; i++)
			SpawnBall(arena, i);
	}

	UNLOCK_STATUS(arena);
}


void PlaceBall(Arena *arena, int bid, struct BallData *newpos)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);

	if (!newpos) return;

	/* keep information consistant, freq of -1 for unowned balls, player's freq for owned balls */
	if (newpos->carrier)
		newpos->freq = newpos->carrier->p_freq;
	else
		newpos->freq = -1;

	LOCK_STATUS(arena);
	if (bid >= 0 && bid < abd->ballcount)
	{
		memcpy(abd->previous + bid, abd->balls + bid, sizeof(struct BallData));
		memcpy(abd->balls + bid, newpos, sizeof(struct BallData));
		send_ball_packet(arena, bid);
	}
	UNLOCK_STATUS(arena);

	logm->Log(L_DRIVEL, "<balls> {%s} ball %d is at (%d, %d)",
			arena->name, bid, newpos->x, newpos->y);
}


void EndGame(Arena *arena)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);

	int i, newgame;
	ticks_t now = current_ticks();
	ConfigHandle c = arena->cfg;

	LOCK_STATUS(arena);

	for (i = 0; i < abd->ballcount; i++)
	{
		PhaseBall(arena, i);
		abd->balls[i].state = BALL_WAITING;
		abd->balls[i].carrier = NULL;
	}

	/* cfghelp: Soccer:NewGameDelay, arena, int, def: -3000
	 * How long to wait between games. If this is negative, the actual
	 * delay is random, between zero and the absolute value. Units:
	 * ticks. */
	newgame = cfg->GetInt(c, "Soccer", "NewGameDelay", -3000);
	if (newgame < 0)
		newgame = prng->Number(0, -newgame);

	for (i = 0; i < abd->ballcount; i++)
		abd->balls[i].time = TICK_MAKE(now + newgame);

	UNLOCK_STATUS(arena);

	{
		Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (persist)
			persist->EndInterval(NULL, arena, INTERVAL_GAME);
		mm->ReleaseInterface(persist);
	}
}


ArenaBallData * GetBallData(Arena *arena)
{
	LOCK_STATUS(arena);
	return P_ARENA_DATA(arena, abdkey);
}

void ReleaseBallData(Arena *arena)
{
	UNLOCK_STATUS(arena);
}


local int load_ball_settings(Arena *arena)
{
	InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);

	ConfigHandle c = arena->cfg;
	int newBallCount, i;

	BallSpawn spawn[MAXBALLS];
	int newSpawnCount = 1;

	LOCK_STATUS(arena);

	/* cfghelp: Soccer:BallCount, arena, int, def: 0
	 * The number of balls in this arena. */
	newBallCount = cfg->GetInt(c, "Soccer", "BallCount", 0);

	/* cfghelp: Soccer:SpawnX, arena, int, range: 0-1023, def: 512
	 * The X coordinate that the ball spawns at (in tiles). */
	spawn[0].x = cfg->GetInt(c, "Soccer", "SpawnX", 512);
	/* cfghelp: Soccer:SpawnY, arena, int, range: 0-1023, def: 512
	 * The Y coordinate that the ball spawns at (in tiles). */
	spawn[0].y = cfg->GetInt(c, "Soccer", "SpawnY", 512);
	/* cfghelp: Soccer:SpawnRadius, arena, int, def: 20
	 * How far from the spawn center the ball can spawn (in tiles). */
	spawn[0].r = cfg->GetInt(c, "Soccer", "SpawnRadius", 20);

	for (i = 1; i < MAXBALLS; ++i)
	{
		/* cfghelp: Soccer:SpawnX/Y/RadiusN, arena, int, def: -1
		 * The spawn coordinates and radius for balls other than the
		 * first one. N goes from 1 to 7 (0 is take care of by the
		 * settings without a number). If there are more balls than
		 * spawns defined, the latter balls will repeat the first
		 * spawns in order. For example, with 3 spawns, the fourth
		 * ball uses the first spawn, the fifth ball uses the second.
		 * If only part of a spawn is undefined, that part will default to the first spawn's setting. */
		char xname[] = "SpawnX#";
		char yname[] = "SpawnY#";
		char rname[] = "SpawnRadius#";
		xname[6] = yname[6] = rname[11] = '0' + i;

		spawn[i].x = cfg->GetInt(c, "Soccer", xname, -1);
		spawn[i].y = cfg->GetInt(c, "Soccer", yname, -1);
		
		if (spawn[i].x == -1 && spawn[i].y == -1)
		{
			break;
		}
		
		++newSpawnCount;
		if (spawn[i].x == -1)
		{
			spawn[i].x = spawn[0].x;
		}
		else if (spawn[i].y == -1)
		{
			spawn[i].y = spawn[0].y;
		}

		spawn[i].r = cfg->GetInt(c, "Soccer", rname, -1);
		if (spawn[i].r == -1)
		{
			spawn[i].r = spawn[0].r;
		}
	}

	if (pbd->spawns)
	{
		if (pbd->spawnCount != newSpawnCount)
		{
			pbd->spawns = realloc(pbd->spawns, sizeof(BallSpawn) * newSpawnCount);
			if (!pbd->spawns)
			{
				newSpawnCount = 1;
				pbd->spawns = amalloc(sizeof(BallSpawn));
			}
		}
	}
	else
	{
		pbd->spawns = amalloc(sizeof(BallSpawn) * newSpawnCount);
	}
	pbd->spawnCount = newSpawnCount;
	memcpy(pbd->spawns, spawn, sizeof(BallSpawn) * newSpawnCount);

	/* cfghelp: Soccer:SendTime, arena, int, range: 25-500, def: 100
	 * How often the server sends ball positions (in ticks). */
	pbd->cfg_sendTime = cfg->GetInt(c, "Soccer", "SendTime", 100);
	if (pbd->cfg_sendTime < 25)
		pbd->cfg_sendTime = 25;
	else if (pbd->cfg_sendTime > 500)
		pbd->cfg_sendTime = 500;

	/* cfghelp: Soccer:GoalDelay, arena, int, def: 0
	 * How long after a goal before the ball appears (in ticks). */
	pbd->cfg_respawnTimeAfterGoal = cfg->GetInt(c, "Soccer", "GoalDelay", 0);

	/* cfghelp: Soccer:AllowGoalByDeath, arena, bool, def: 0
	 * Whether a goal is scored if a player dies carrying the ball
	 * on a goal tile. */
	pbd->cfg_deathScoresGoal = cfg->GetInt(c, "Soccer", "AllowGoalByDeath", 0);

	UNLOCK_STATUS(arena);

	return newBallCount;
}


void AABall(Arena *arena, int action)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
	InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);

	/* create the mutex if necessary */
	if (action == AA_PRECREATE)
	{
		pthread_mutexattr_t attr;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init((pthread_mutex_t*)P_ARENA_DATA(arena, mtxkey), &attr);
		pthread_mutexattr_destroy(&attr);
	}


	LOCK_STATUS(arena);

	if (action == AA_CREATE)
	{
		int i;

		pbd->spawns = NULL;
		pbd->lastSendTime = current_ticks();
		pbd->spawnCount = 0;
		pbd->ballcountOverridden = FALSE;

		abd->ballcount = load_ball_settings(arena);

		/* allocate array for public ball data */
		abd->balls = amalloc(MAXBALLS * sizeof(struct BallData));
		abd->previous = amalloc(MAXBALLS * sizeof(struct BallData));

		for (i = 0; i < MAXBALLS; ++i)
		{
			//initialize ball data
			if (i < abd->ballcount)
			{
				SpawnBall(arena, i);
			}
			else
			{
				InitBall(arena, i);
			}
			/* initialize the "previous" array with the initial state */
			memcpy(abd->previous + i, abd->balls + i, sizeof(struct BallData));
		}

		if (abd->ballcount > 0)
			logm->LogA(L_DRIVEL, "balls", arena, "%d balls spawned", abd->ballcount);
	}
	else if (action == AA_DESTROY)
	{
		/* clean up ball data */
		afree(abd->balls);
		afree(abd->previous);
		abd->balls = 0;
		abd->ballcount = 0;

		/* clean up internal data */
		afree(pbd->spawns);
		pbd->spawnCount = 0;
		pbd->ballcountOverridden = FALSE;
	}
	else if (action == AA_CONFCHANGED)
	{
		int oldBallCount = abd->ballcount;
		int newBallCount = load_ball_settings(arena);

		/* if the ball count change but it wasn't changed by a module or command,
		 * allow the new setting to change the ball count. */
		if (newBallCount != oldBallCount && !pbd->ballcountOverridden)
		{
			ChangeBallCount(arena, newBallCount);
		}
	}

	UNLOCK_STATUS(arena);
}


local void CleanupAfter(Arena *arena, Player *p, int neut, int leaving)
{
	/* make sure that if someone leaves, his ball drops */
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
	int i;
	struct BallData *b = abd->balls;
	struct BallData *prev = abd->previous;

	LOCK_STATUS(arena);
	for (i = 0; i < abd->ballcount; i++, b++, prev++)
	{
		if (leaving && prev->carrier == p)
		{
			/* prevent stale pointer from appearing in historical data. */
			prev->carrier = NULL;
		}

		if (b->state == BALL_CARRIED && b->carrier == p)
		{
			LinkedList advisers = LL_INITIALIZER;
			Aballs *adviser;
			Link *link;
			int allow = TRUE;
			struct BallData defaultbd;

			defaultbd.state = BALL_ONMAP;
			defaultbd.x = p->position.x;
			defaultbd.y = p->position.y;
			defaultbd.xspeed = defaultbd.yspeed = 0;
			if (neut || leaving)
				defaultbd.carrier = NULL;
			else
				defaultbd.carrier = p;
			defaultbd.time = defaultbd.last_update = current_ticks();

			memcpy(prev, b, sizeof(struct BallData));
			memcpy(b, &defaultbd, sizeof(struct BallData));

			/* run this by advisers to see if they want to make the ball do something weird instead. */
			mm->GetAdviserList(A_BALLS, arena, &advisers);
			FOR_EACH(&advisers, adviser, link)
			{
				if (adviser->AllowBallFire)
				{
					/* the TRUE in the parameter indicates that we are forcing the ball fire because the player left the game */
					allow = adviser->AllowBallFire(arena, p, i, TRUE, b);
					if (!allow)
					{
						/* we won't call any more advisers, but the ball will still be dropped obviously. */
						break;
					}
				}
			}
			mm->ReleaseAdviserList(&advisers);

			if (!allow)
			{
				memcpy(b, &defaultbd, sizeof(struct BallData));
			}

			send_ball_packet(arena, i);

			/* don't forget fire callbacks. even if advisers disallowed it the ball is stil leaving the player like it or not, so we must fire
			 * the callback. */

			DO_CBS(CB_BALLFIRE, arena, BallFireFunc,
					(arena, p, i));
			
			if (!neut && b->carrier != NULL && mapdata->GetTile(arena, b->x/16, b->y/16) == TILE_GOAL)
			{
				/* dropped an unneuted ball on a goal tile. let's not wait for the goal packet, otherwise a pickup packet may intercept it,
				 * which might be okay if it didn't boil down to who has a better connection. */
				logm->LogP(L_DRIVEL, "balls", p, "fired ball on top of goal tile");
				HandleGoal(arena, b->carrier, i, b->x/16, b->y/16);
			}
		}
		else if (neut && b->carrier == p)
		{
			/* if it's on the map, but last touched by the person, reset
			 * it's last touched pid to -1 so that the last touched pid
			 * always refers to a valid player. */
			b->carrier = NULL;
			send_ball_packet(arena, i);
		}
	}
	UNLOCK_STATUS(arena);
}

void PABall(Player *p, int action, Arena *arena)
{
	/* if he's entering arena, the timer event will send him the ball
	 * info. */
	if (action == PA_LEAVEARENA)
		CleanupAfter(arena, p, 1, 1);
}

void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	CleanupAfter(p->arena, p, 1, 0);
}

void BallKill(Arena *arena, Player *killer, Player *killed, int bounty,
		int flags, int *pts, int *green)
{
	InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);
	CleanupAfter(arena, killed, !pbd->cfg_deathScoresGoal, 0);
}


void PPickupBall(Player *p, byte *pkt, int len)
{
	Arena *arena = p->arena;
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);

	int i;
	struct BallData *bd;
	struct C2SPickupBall *bp = (struct C2SPickupBall*)pkt;
	int bid = bp->ballid;

	LinkedList advisers = LL_INITIALIZER;
	Aballs *adviser;
	Link *link;
	int allow = TRUE;
	struct BallData defaultbd;
	
	if (len != sizeof(struct C2SPickupBall))
	{
		logm->Log(L_MALICIOUS, "<balls> [%s] Bad size for ball pickup packet", p->name);
		return;
	}

	if (!arena || p->status != S_PLAYING)
	{
		logm->Log(L_WARN, "<balls> [%s] ball pickup packet from bad arena or status", p->name);
		return;
	}

	if (p->p_ship >= SHIP_SPEC)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: ball pickup packet from spec");
		return;
	}

	/* this player is too lagged to have a ball */
	if (p->flags.no_flags_balls)
	{
		logm->LogP(L_DRIVEL, "balls", p, "too lagged to pick up ball %d", bp->ballid);
		return;
	}

	LOCK_STATUS(arena);

	if (bp->ballid >= abd->ballcount)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: tried to pick up a nonexistent ball");
		UNLOCK_STATUS(arena);
		return;
	}

	bd = abd->balls + bid;

	/* make sure someone else didn't get it first */
	if (bd->state != BALL_ONMAP)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: tried to pick up a carried ball");
		UNLOCK_STATUS(arena);
		return;
	}

	if (bp->time != bd->time)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: tried to pick up a ball from stale coords");
		UNLOCK_STATUS(arena);
		return;
	}

	/* make sure player doesnt carry more than one ball */
	for (i = 0; i < abd->ballcount; i++)
		if (abd->balls[i].carrier == p &&
		    abd->balls[i].state == BALL_CARRIED)
		{
			UNLOCK_STATUS(arena);
			return;
		}

	memcpy(&defaultbd, bd, sizeof(struct BallData));

	bd->state = BALL_CARRIED;
	bd->x = p->position.x;
	bd->y = p->position.y;
	bd->xspeed = 0;
	bd->yspeed = 0;
	bd->carrier = p;
	bd->freq = p->p_freq;
	bd->time = 0;
	bd->last_update = current_ticks();

	mm->GetAdviserList(A_BALLS, arena, &advisers);
	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->AllowBallPickup)
		{
			allow = adviser->AllowBallPickup(arena, p, bid, bd);
			if (!allow)
				break;
		}
	}
	mm->ReleaseAdviserList(&advisers);

	if (!allow)
	{
		memcpy(bd, &defaultbd, sizeof(struct BallData));
		send_ball_packet(arena, bp->ballid);
	}
	else
	{
		memcpy(abd->previous + bid, &defaultbd, sizeof(struct BallData));
		send_ball_packet(arena, bp->ballid);
		
		/* now call callbacks */
		DO_CBS(CB_BALLPICKUP, arena, BallPickupFunc,
				(arena, p, bp->ballid));
				
		logm->Log(L_INFO, "<balls> {%s} [%s] player picked up ball %d",
				arena->name,
				p->name,
				bp->ballid);
	}

	UNLOCK_STATUS(arena);
}


void PFireBall(Player *p, byte *pkt, int len)
{
	Arena *arena = p->arena;
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);

	struct BallData *bd;
	struct BallPacket *fb = (struct BallPacket *)pkt;
	int bid = fb->ballid;

	LinkedList advisers = LL_INITIALIZER;
	Aballs *adviser;
	Link *link;
	int allow = TRUE;
	struct BallData defaultbd;

	if (len != sizeof(struct BallPacket))
	{
		logm->LogP(L_MALICIOUS, "balls", p, "bad size for ball fire packet");
		return;
	}

	if (!arena || p->status != S_PLAYING)
	{
		logm->LogP(L_WARN, "balls", p, "ball fire packet from bad arena or status");
		return;
	}

	if (p->p_ship >= SHIP_SPEC)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: ball fire packet from spec");
		return;
	}

	LOCK_STATUS(arena);

	if (bid < 0 || bid >= abd->ballcount)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: tried to fire up a nonexistent ball");
		UNLOCK_STATUS(arena);
		return;
	}

	bd = abd->balls + bid;

	if (bd->state != BALL_CARRIED || bd->carrier != p)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: player tried to fire ball he wasn't carrying");
		UNLOCK_STATUS(arena);
		return;
	}

	memcpy(&defaultbd, bd, sizeof(struct BallData));

	bd->state = BALL_ONMAP;
	bd->x = fb->x;
	bd->y = fb->y;
	bd->xspeed = fb->xspeed;
	bd->yspeed = fb->yspeed;
	bd->freq = p->p_freq;
	bd->time = fb->time;
	bd->last_update = current_ticks();

	mm->GetAdviserList(A_BALLS, arena, &advisers);
	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->AllowBallFire)
		{
			/* the FALSE in the parameter indicates that this is the client controlling the ball firing */
			allow = adviser->AllowBallFire(arena, p, bid, FALSE, bd);
			if (!allow)
				break;
		}
	}
	mm->ReleaseAdviserList(&advisers);

	if (!allow)
	{
		memcpy(bd, &defaultbd, sizeof(struct BallData));
		send_ball_packet(arena, bid);
	}
	else
	{
		memcpy(abd->previous + bid, &defaultbd, sizeof(struct BallData));
		send_ball_packet(arena, bid);
		
		/* finally call callbacks */
		DO_CBS(CB_BALLFIRE, arena, BallFireFunc, (arena, p, bid));
		logm->LogP(L_INFO, "balls", p, "player fired ball %d", bid);
		
		if (bd->carrier != NULL && mapdata->GetTile(arena, bd->x/16, bd->y/16) == TILE_GOAL)
		{
			/* dropped an unneuted ball on a goal tile. let's not wait for the goal packet, otherwise a pickup packet may intercept it,
			 * which might be okay if it didn't boil down to who has a better connection. */
			logm->LogP(L_DRIVEL, "balls", p, "fired ball on top of goal tile");
			HandleGoal(arena, bd->carrier, bid, bd->x/16, bd->y/16);
		}
	}

	UNLOCK_STATUS(arena);
}

void PGoal(Player *p, byte *pkt, int len)
{
	Arena *arena = p->arena;
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
	int bid;
	struct C2SGoal *g = (struct C2SGoal*)pkt;
	struct BallData *bd;

	if (len != sizeof(struct C2SGoal))
	{
		logm->LogP(L_MALICIOUS, "balls", p, "bad size for goal packet");
		return;
	}

	if (!arena || p->status != S_PLAYING)
	{
		logm->LogP(L_WARN, "balls", p, "goal packet from bad arena or status");
		return;
	}

	LOCK_STATUS(arena);

	bid = g->ballid;

	if (bid < 0 || bid >= abd->ballcount)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: sent a goal for a nonexistent ball");
		UNLOCK_STATUS(arena);
		return;
	}

	bd = abd->balls + bid;

	/* we use this as a flag to check for dupilicated goals */
	if (bd->carrier == NULL)
	{
		UNLOCK_STATUS(arena);
		return;
	}

	if (bd->state != BALL_ONMAP)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: sent goal for carried ball");
		UNLOCK_STATUS(arena);
		return;
	}

	if (p != bd->carrier)
	{
		logm->LogP(L_WARN, "balls", p, "state sync problem: sent goal for ball he didn't fire");
		UNLOCK_STATUS(arena);
		return;
	}

	HandleGoal(arena, p, bid, g->x, g->y);
	
	UNLOCK_STATUS(arena);
}

void HandleGoal(Arena *arena, Player *p, int bid, int goalX, int goalY)
{
	ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
	struct BallData *bd = abd->balls + bid;
	InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);
	
	LinkedList advisers = LL_INITIALIZER;
	Aballs *adviser;
	Link *link;
	int block = FALSE;
	struct BallData newbd;
	
	LOCK_STATUS(arena);
	
	memcpy(&newbd, bd, sizeof(struct BallData));

	mm->GetAdviserList(A_BALLS, arena, &advisers);
	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->BlockBallGoal)
		{
			int result = adviser->BlockBallGoal(arena, p, bid, goalX, goalY, &newbd);
			if (result == TRUE)
			{
				block = TRUE;
				/* at this point, we will allow other modules to redirect the goal's path
				 * but the goal being blocked is final. */
			}
		}
	}
	mm->ReleaseAdviserList(&advisers);

	if (block)
	{
		/* update ball data and transmit it assuming it was changed */
		/* barring extreme circumstances, using this check should not be a problem. */
		if (bd->last_update != newbd.last_update)
		{
			memcpy(abd->previous + bid, bd, sizeof(struct BallData));
			memcpy(bd, &newbd, sizeof(struct BallData));
			send_ball_packet(arena, bid);
		}
	}
	else
	{
		memcpy(abd->previous + bid, bd, sizeof(struct BallData));

		/* send ball update */
		if (bd->state != BALL_ONMAP)
		{
			/* don't respawn ball */
		}
		else if (pbd->cfg_respawnTimeAfterGoal == 0)
		{
			/* we don't want a delay */
			SpawnBall(arena, bid);
		}
		else
		{
			/* phase it, then set it to waiting */
			ticks_t ct = current_ticks();
			PhaseBall(arena, bid);
			bd->state = BALL_WAITING;
			bd->carrier = NULL;
			bd->time = TICK_MAKE(ct + pbd->cfg_respawnTimeAfterGoal);
			bd->last_update = ct;
		}

		/* do callbacks after spawning */
		DO_CBS(CB_GOAL, arena, GoalFunc, (arena, p, bid, goalX, goalY));

		logm->LogP(L_INFO, "balls", p, "goal with ball %d at (%i, %i)", bid, goalX, goalY);
	}

	UNLOCK_STATUS(arena);
}


int BasicBallTimer(void *dummy)
{
	Arena *arena;
	Link *link;

	aman->Lock();
	FOR_EACH_ARENA(arena)
	{
		ArenaBallData *abd = P_ARENA_DATA(arena, abdkey);
		InternalBallData *pbd = P_ARENA_DATA(arena, pbdkey);

		if (arena->status != ARENA_RUNNING)
			continue;

		LOCK_STATUS(arena);
		if (abd->ballcount > 0)
		{
			/* see if we are ready to send packets */
			ticks_t gtc = current_ticks();

			if ( TICK_DIFF(gtc, pbd->lastSendTime) > pbd->cfg_sendTime)
			{
				int bid, bc = abd->ballcount;
				struct BallData *b = abd->balls;

				/* now check the balls up to bc */
				for (bid = 0; bid < bc; bid++, b++)
					if (b->state == BALL_ONMAP)
					{
						/* it's on the map, just send the position
						 * update */
						send_ball_packet(arena, bid);
					}
					else if (b->state == BALL_CARRIED && b->carrier)
					{
						/* it's being carried, update it's x,y coords */
						struct PlayerPosition *pos = &b->carrier->position;
						b->x = pos->x;
						b->y = pos->y;
						send_ball_packet(arena, bid);
					}
					else if (b->state == BALL_WAITING)
					{
						if (TICK_GT(gtc, b->time))
							SpawnBall(arena, bid);
					}
				pbd->lastSendTime = gtc;
			}
		}
		UNLOCK_STATUS(arena);
	}
	aman->Unlock();

	return TRUE;
}


