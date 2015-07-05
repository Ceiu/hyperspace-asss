/*
 * Hyperspace Elimination
 * 07.06.05 D1st0rt
 */

#define MODULENAME hs_elim
#define SZMODULENAME "hs_elim"
#define INTERFACENAME Ihs_elim

#include "math.h"
#include "akd_asss.h"
#include "hscore.h"
#include "hscore_money.h"
#include "hscore_database.h"
#include "hscore_buysell_points.h"
#include "kill.h"
#include "hscore_spawner.h"
#include "gamestats.h"

// Defines
#define ACTIVE 2
#define STARTING 1
#define PLAYING 1
#define IDLE 0

#define KILLERNAME "<heavenly judgment>"

// Non-Standard Interfaces
local Istats *stats;
local Ihscoredatabase *hsdb;
local Ihscoremoney *money;
local Ihscorebuysellpoints *store;
local Ikill *kill;
// Globals
local pthread_mutex_t globalmutex;

DEF_PARENA_TYPE
	int status;
	int deaths;
	int participants;
	int minplayers;
	int specfreq;

	int cfg_timeAwayLimit;
	int cfg_distanceAwayLimit;

	Igamestats *gs;
	gamestat_type *stat_W;
	gamestat_type *stat_L;
	gamestat_type *stat_T;
	gamestat_type *stat_EL;

	Ihscorespawner *spawn;
	
	Killer *killer;
ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
	int status;
	int warned;
	int timeAway;
ENDDEF_PPLAYER_TYPE;

// Prototypes
local void announceElim(Arena *);
local void startElim(Arena *);
local void elimPlayer(Player *);
local void checkStatus(Arena *);
local void endElim(Arena *);

local void endelim_statsenum(Arena *a, const char *name, LinkedList *statlist, int *param);

// Callbacks
local void shipfreqchange(Player *, int newship, int oldship, int newfreq, int oldfreq);
local void playeraction(Player *, int action, Arena *);
local void playerkill(Arena *, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);

// Timers
local int startDelay(void *a);
local int checkHiders(void *_a);

EXPORT const char info_hs_elim[] = "v1.0 by D1st0rt <d1st0rter@gmail.com>";
EXPORT int MM_hs_elim(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		mm = mm_;

		GET_USUAL_INTERFACES();
		GETINT(stats, I_STATS);
		GETINT(hsdb, I_HSCORE_DATABASE);
		GETINT(money, I_HSCORE_MONEY);
		GETINT(kill, I_KILL);
		GETINT(store, I_HSCORE_BUYSELL_POINTS);

		REG_PARENA_DATA();
		BREG_PPLAYER_DATA();

		INIT_MUTEX(globalmutex);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		DESTROY_MUTEX(globalmutex);

		UNREG_PARENA_DATA();
		UNREG_PPLAYER_DATA();

		ml->ClearTimer(checkHiders, 0);
		ml->ClearTimer(startDelay, 0);

Lfailload:
		RELEASEINT(store);
		RELEASEINT(kill);
		RELEASEINT(money);
		RELEASEINT(hsdb);
		RELEASEINT(stats);
		RELEASE_USUAL_INTERFACES();

		DO_RETURN();
	}

	else if (action == MM_ATTACH)
	{
		ALLOC_ARENA_DATA(ad);

		GETARENAINT(ad->gs, I_GAMESTATS);
		GETARENAINT(ad->spawn, I_HSCORE_SPAWNER);

		ad->killer = kill->LoadKiller(KILLERNAME, arena, 0, 9999);
		ad->stat_W = ad->gs->getStatType(arena, "W");
		ad->stat_L = ad->gs->getStatType(arena, "L");
		ad->stat_T = ad->gs->getStatType(arena, "T");
		ad->stat_EL = ad->gs->getStatType(arena, "EL");

		/* cfghelp: Elim:Deaths, arena, int, def: 5, mod: hs_elim
		 The number of deaths before a player is eliminated*/
		ad->deaths = cfg->GetInt(arena->cfg, "elim", "deaths", 5);
		/* cfghelp: Elim:MinPlayers, arena, int, def: 5, mod: hs_elim
		 The minimum number of players required to start a game*/
		ad->minplayers = cfg->GetInt(arena->cfg, "elim", "minplayers", 5);
		/* cfghelp: Elim:TimeAwayLimit, arena, int, def: 10, mod: hs_elim
		 How long a player can be away from other players*/
		ad->cfg_timeAwayLimit = cfg->GetInt(arena->cfg, "elim", "timeawaylimit", 10);
		/* cfghelp: Elim:DistanceAwayLimit, arena, int, def: 1500, mod: hs_elim
		 How far a player can be away from other players*/
		ad->cfg_distanceAwayLimit = cfg->GetInt(arena->cfg, "elim", "distanceawaylimit", 1500);
		ad->specfreq = cfg->GetInt(arena->cfg, "Team", "SpectatorFrequency", 8025);

		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_PLAYERACTION, playeraction, arena);
		mm->RegCallback(CB_KILL, playerkill, arena);

		ml->SetTimer(checkHiders, 100, 100, arena, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		ml->ClearTimer(checkHiders, arena);
		ml->ClearTimer(startDelay, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_PLAYERACTION, playeraction, arena);
		mm->UnregCallback(CB_KILL, playerkill, arena);

		kill->UnloadKiller(ad->killer);
Lfailattach:
		RELEASEINT(ad->spawn);
		RELEASEINT(ad->gs);
		FREE_ARENA_DATA(ad);
		DO_RETURN();
	}

	return MM_FAIL;
}

local void announceElim(Arena *a)
{
	DEF_AD(a);
	int total;
	int playing;

	if(!ad->status)
	{
		aman->GetPopulationSummary(&total, &playing);
		if(a->playing >= ad->minplayers)
		{
			chat->SendArenaSoundMessage(a, SOUND_BEEP2, "Elim is starting in 90 seconds!");
			ad->status = STARTING;
			ml->SetTimer(startDelay, 9000, 0, a, a);
		}
		else
		{
			if(ad->minplayers - a->playing == 1)
			{
				chat->SendArenaMessage(a, "Elim will start when one more player enters.");
			}
			else
			{
				chat->SendArenaMessage(a, "Elim will start when %d more players enter.", (ad->minplayers - a->playing));
			}
		}
	}
}

local void startElim(Arena *a)
{
	DEF_AD(a);
	Link *link;
	Player *p;
	Target t;
	int freq;
	int total;
	int playing;

	if(ad->status == STARTING)
	{
		aman->GetPopulationSummary(&total, &playing);
		if(a->playing >= ad->minplayers)
		{
			ticks_t ct = current_ticks();
			freq = 0;
			game->LockArena(a, 0, 0, 0, 0);
			ad->gs->ClearGame(a, 0);
			PDLOCK;
			FOR_EACH_PLAYER(p)
			{
				BDEF_PD(p);
				if(IS_PLAYING(p, a) && IS_STANDARD(p))
				{
					pdat->status = ACTIVE;
					t.type = T_PLAYER;
					t.u.p = p;
					stats->ScoreReset(p, INTERVAL_RESET);
					game->SetFreq(p, freq);
					game->ShipReset(&t);
					ad->spawn->respawn(p);
					freq++;
					ad->gs->StartStatTicker(a, p, ad->stat_T, 0, 0, 0, ct);

					pdat->warned = 0;
					pdat->timeAway = 0;
				}
				else
				{
					pdat->status = IDLE;
				}
			}
			PDUNLOCK;

			stats->SendUpdates(NULL);
			ad->status = ACTIVE;
			ad->participants = 0;
			store->setSellingAllowed(a, FALSE);
			chat->SendArenaSoundMessage(a, SOUND_GOAL, "ELIM HAS STARTED!");
		}
		else
		{
			ad->status = IDLE;
			announceElim(a);
		}
	}
}

local void elimPlayer(Player *p)
{
	BDEF_PD(p);
	DEF_AD(p->arena);
	ticks_t ct = current_ticks();

	ad->gs->StopStatTicker(p->arena, p, ad->stat_T, ct);
	pdat->status = IDLE;
	game->SetShip(p, SHIP_SPEC);
	game->SetFreq(p, ad->specfreq);
	checkStatus(p->arena);
}

local void checkStatus(Arena *a)
{
	Link *link;
	Player *p;
	Player *winner;
	int wins;
	int losses;
	int playing;
	int totaltime;

	DEF_AD(a);
	if(ad->status == ACTIVE)
	{
		playing = 0;
		PDLOCK;
		FOR_EACH_PLAYER(p)
		{
			BDEF_PD(p);

			if (p->arena != a)
				continue;

			if(pdat->status)
			{
				playing++;
				winner = p;
			}
		}
		PDUNLOCK;

		if(!winner || playing < 1)
		{
			chat->SendArenaSoundMessage(a, SOUND_BEEP1, "No players, elimination aborted");
			endElim(a);
		}
		else if(playing == 1)
		{
			BDEF_PD(winner);
			wins = stats->GetStat(winner, STAT_KILLS, INTERVAL_RESET);
			losses = stats->GetStat(winner, STAT_DEATHS, INTERVAL_RESET);

			chat->SendArenaSoundMessage(a, SOUND_DING, "%s [%d-%d] has won Elim!", winner->name, wins, losses);
			ad->participants++;

			//stop all tickers in the arena
			ad->gs->StopStatTicker(a, 0, 0, current_ticks());

			totaltime = ad->gs->getStat(a, winner->name, ad->stat_T, 0, 0, 0);

			pdat->status = IDLE;

			/*if(ad->participants >= ad->minplayers)
			{
				chat->SendArenaMessage(a, "Rewards");
			}*/

			//ad->gs->StatsEnum(a, 0, 0, endelim_statsenum, &totaltime);
			endElim(a);
		}

	}
}

local void endelim_statsenum(Arena *a, const char *name, LinkedList *statlist, int *param)
{
	//DEF_AD(a);
	//int seconds = ad->gs->rawGet(a, statlist, ad->stat_T, 0);
}

local void endElim(Arena *a)
{
	Target t;
	DEF_AD(a);
	ad->status = IDLE;
	//game->LockArena(a, 0, 0, 0, 1);
	t.type = T_ARENA;
	t.u.arena = a;
	ad->gs->SpamStatsTable(a, 0, 0, &t);
	game->UnlockArena(a, 1, 0);
	ad->gs->ClearGame(a, 0);
	store->setSellingAllowed(a, TRUE);
}

local int startDelay(void *param)
{
	Arena *a = (Arena *)param;
	startElim(a);
	return FALSE;
}

local int checkHiders(void *_a)
{
	Arena *a = (Arena *)_a;
	DEF_AD(a);
	Player *p, *p2;
	Link *link, *link2;
	int x1, x2, y1, y2, d1, d2;
	int okay = 0;
	int playing = 0;
	int donecounting = 0;
	if (!ad)
		return 1;

	if (ad->status != ACTIVE)
		return 1;

	PDLOCK;

	//check each player against each other player ( O(N^2) worst case.. oh well)
	FOR_EACH_PLAYER(p)
	{
		BDEF_PD(p);

		if (!IS_PLAYING(p, a))
			continue;
		okay = 0;

		for (link2 = LLGetHead(&pd->playerlist); link2 && ((p2 = link2->data, link2 = link2->next) || 1); )
		{
			if (!IS_PLAYING(p2, a))
				continue;

			if (donecounting && okay)	//this player is okay, and we don't need to count up how many are in the arena anymore.
				break;

			++playing; //count up players in this loop.

			if (okay)
				continue;

			x1 = p->position.x;
			x2 = p2->position.x;
			y1 = p->position.y;
			y2 = p2->position.y;

			d1 = (x1 - x2) * (x1 - x2);
			d2 = (y1 - y2) * (y1 - y2);

			if ((d1 + d2) < 0) //this shouldn't happen
				continue;

			//calculate distance
			if (sqrt(d1 + d2) < ad->cfg_distanceAwayLimit)
			{
				//they're close? refund them.
				--pdat->timeAway;
				if (pdat->timeAway < 0)
					pdat->timeAway = 0;

				okay = 1;
				continue;
			}
		}

		donecounting = 1; //don't waste more time in loops.

		//if there are only 2 players, you can't hide.
		if (playing <= 2)
			break;

		if (okay)
			continue;

		++pdat->timeAway;

		//they've been away too long..time to take action
		if (pdat->timeAway >= ad->cfg_timeAwayLimit)
		{
			//not warned yet.. warn them
			if (!pdat->warned)
			{
				Target t;
				chat->SendMessage(p, "Don't hide!");
				t.type = T_PLAYER;
				t.u.p = p;
				game->GivePrize(&t, PRIZE_WARP, 1);
				pdat->warned = 1;
			}
			//okay, get mean now.
			else
			{
				chat->SendMessage(p, "Don't hide!");
				kill->Kill(p, ad->killer, 0, 0);
				pdat->warned = 0;
				pdat->timeAway = 0;
			}
		}
	}

	PDUNLOCK;
	return okay;
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	DEF_AD(p->arena);
	BDEF_PD(p);
	int wins;
	int losses;

	if(ad->status == ACTIVE && pdat->status)
	{
		if(newship == SHIP_SPEC || newfreq == ad->specfreq)
		{
			losses = stats->GetStat(p, STAT_DEATHS, INTERVAL_RESET);
			wins = stats->GetStat(p, STAT_KILLS, INTERVAL_RESET);

			chat->SendArenaMessage(p->arena, "%s [%d-%d] is out (specced)", p->name, wins, losses);
			elimPlayer(p);
		}
	}
	else if(ad->status == ACTIVE && !pdat->status)
	{
		game->SetShip(p, SHIP_SPEC);
		game->SetFreq(p, ad->specfreq);
	}
	else if(!ad->status && !pdat->status)
	{
		//if(p->pkt.ship == SHIP_SPEC || p->pkt.freq == ad->specfreq)
		//{
			if(newship != SHIP_SPEC)
			{
				pdat->status = PLAYING;
				announceElim(p->arena);
			}
		//}
	}
}

local void playeraction(Player *p, int action, Arena *a)
{
	DEF_AD(a);
	BDEF_PD(p);
	int wins;
	int losses;

	if(ad->status == ACTIVE && pdat->status)
	{

		if(action == PA_LEAVEARENA)
		{
			losses = stats->GetStat(p, STAT_DEATHS, INTERVAL_RESET);
			wins = stats->GetStat(p, STAT_KILLS, INTERVAL_RESET);

			chat->SendArenaMessage(a, "%s [%d-%d] is out (left arena)", p->name, wins, losses);
			elimPlayer(p);
		}
	}
	else if(!ad->status)
	{
		if(action == PA_ENTERGAME && !pdat->status)
		{
			announceElim(a);
		}
		else if(action == PA_ENTERARENA)
		{
			chat->SendMessage(p, "Elim has not yet started, enter to join.");
		}
	}
}

local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	DEF_AD(a);
	BDEF_PD(killer);
	BDEF_PD_ALT(killed, pdat2);

	int losses;
	int wins;

	if(ad->status == ACTIVE)
	{
		losses = stats->GetStat(killed, STAT_DEATHS, INTERVAL_RESET);
		wins = stats->GetStat(killed, STAT_KILLS, INTERVAL_RESET);

		ad->gs->AddStat(a, killer, ad->stat_W, 0, 0, 0, 1);
		ad->gs->AddStat(a, killed, ad->stat_L, 0, 0, 0, 1);

		//reset hide counts
		pdat->warned = pdat2->warned = 0;
		pdat->timeAway = pdat2->timeAway = 0;

		if(losses >= ad->deaths)
		{
			chat->SendArenaMessage(a, "%s [%d-%d] is out!", killed->name, wins, losses);
			ad->participants++;
			ad->gs->AddStat(a, killer, ad->stat_EL, 0, 0, 0, 1);
			elimPlayer(killed);
		}
		else
		{
			if(ad->deaths - losses == 1)
			{
				chat->SendMessage(killed, "You are on your final life.");
			}
			else
			{
				chat->SendMessage(killed, "You have %d lives remaining", (ad->deaths - losses));
			}
		}
	}
}
