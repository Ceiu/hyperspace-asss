/*
	HS_POWERBALL
	15 Jan 2008
	Author:
	- Justin "Arnk Kilo Dylie" Schwartz
	Contributors:

*/
/*
	Independent module presently authorized by the author for use in Hyperspace.
*/

#include "asss.h"
#include "jackpot.h"
#include "hscore.h"
#include <math.h>
#include <string.h>

#define MODULENAME hs_powerball
#define SZMODULENAME "hs_powerball"
#define INTERFACENAME Ihs_powerball

#define NOT_USING_SHIP_NAMES 1
#include "akd_asss.h"

//other interfaces we want to use besides the usual
local Imapdata *mapdata;
local Ihscoredatabase *database;
local Iballs *balls;
local Ijackpot *jackpot;

//config values

//other globals
local pthread_mutex_t globalmutex;

//prototype all functions we will be using in the interface here. then define the interface, then prototype that other stuff.
local void SpawnBall(Arena *arena, int bid);

//MYINTERFACE =
//{
//	INTERFACE_HEAD_INIT(I_HS_POWERBALL, "hs-powerball")
//	0,
//	//functions in order here
//};

//DEF_GLOBALINTERFACE;

DEF_PARENA_TYPE
	int cfg_minimumRewardFactor;
	int cfg_maximumRewardFactor;
	int cfg_minimumRewardOffset;
	int cfg_maximumRewardOffset;
	int cfg_resetDelay;
	int cfg_rewardShiftDelay;
	int cfg_maximumReward;
	int cfg_jackpotDividerMin;
	int cfg_jackpotDividerMax;
	int cfg_offsetUpDifficulty;
	int ma_goalCount;
	Region **rgn_goals;
	Region *rgn_nopowerball;
	int *goal_rewardFactors;
	int *goal_rewardOffsets;
	//INTERFACENAME arenainterface;
	int resetDelay;
	int rewardShiftDelay;
	int active;
ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
	int i;

ENDDEF_PPLAYER_TYPE;

//prototype internal functions here.
local int lookupGoalNumber(Arena *, int x, int y);
local void rewardShift(Arena *);
local void reset(Arena *);
local const char *lookupGoalName(Arena *, int gn);
local int calculateReward(Arena *, int gn, int freq);

local helptext_t goals_help =
"Targets: arena\n"
"Syntax:\n"
"  ?goals\n"
"Displays the current rewards of each goal.\n";
local void Cgoals(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t powerballreset_help =
"Targets: arena\n"
"Syntax:\n"
"  ?powerballreset\n"
"Respawns each powerball and initiates a reward shift.\n";
local void Cpowerballreset(const char *cmd, const char *params, Player *p, const Target *target);

//callback
local void arenaaction(Arena *a, int action);
local void ballgoal(Arena *a, Player *p, int bid, int x, int y);
local void ballfire(Arena *a, Player *p, int bid);
local void ballpickup(Arena *arena, Player *p, int bid);


//timer
local int perSecond(void *a);

EXPORT const char info_hs_powerball[] = "v0.0 by Author <contact@email.bleh>";
EXPORT int MM_hs_powerball(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		//store the provided Imodman interface.
		mm = mm_;

		//get all interfaces first. if a required interface is not available, jump to Lfailload and release the interfaces we did get, and return failure.
		GET_USUAL_INTERFACES();	//several interfaces used in many modules, there's no real harm in getting them even if they're not used

		GETINT(mapdata, I_MAPDATA);
		GETINT(database, I_HSCORE_DATABASE);
		GETINT(balls, I_BALLS);
		GETINT(jackpot, I_JACKPOT);

		//register per-arena and per-player data.
		REG_PARENA_DATA();
		//REG_PPLAYER_DATA();

		//malloc and init anything else.

		//init a global mutex if you need one. you only need one if you have a global linkedlist, hashtable, or something cool like that.
		INIT_MUTEX(globalmutex);

		//register the interface if exposing one.
		//INIT_GLOBALINTERFACE();
		//zero out any functions that would be used on the arena level only for the global interface

		//finally, return success.
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		//first try to unregister the interface if exposing one.
		//UNREG_GLOBALINTERFACE();

		//unregister all timers anyway because we're cool.
		ml->ClearTimer(perSecond, 0);

		//clear the mutex if we were using it
		DESTROY_MUTEX(globalmutex);

		//free any other malloced data

		//unregister per-arena and per-player data
		UNREG_PARENA_DATA();
		UNREG_PPLAYER_DATA();

		//release interfaces last.
		//this is where GETINT jumps to if it fails.
Lfailload:
		RELEASE_USUAL_INTERFACES();
		RELEASEINT(mapdata);
		RELEASEINT(database);
		RELEASEINT(balls);
		RELEASEINT(jackpot);

		//returns MM_FAIL if we jumped from a failed GETINT or other MM_LOAD action, returns MM_OK if not.
		DO_RETURN();
	}


	else if (action == MM_ATTACH)
	{
		//allocate this arena's per-arena data.
		ALLOC_ARENA_DATA(ad);
		ALLOC_ARENA_PLAYER_DATA(arena);

		//register per-league-player data if we're using it.
		//REG_PLEAGUEP_DATA(); (we're not)

		//malloc other things in arena data.

		//register global commands, timers, and callbacks.

		ad->cfg_minimumRewardFactor = cfg->GetInt(arena->cfg, "hs_powerball", "minimumrewardfactor", 25);
		ad->cfg_maximumRewardFactor = cfg->GetInt(arena->cfg, "hs_powerball", "maximumrewardfactor", 40);
		ad->cfg_minimumRewardOffset = cfg->GetInt(arena->cfg, "hs_powerball", "minimumrewardoffset", -35);
		ad->cfg_maximumRewardOffset = cfg->GetInt(arena->cfg, "hs_powerball", "maximumrewardoffset", 20);
		ad->cfg_resetDelay = cfg->GetInt(arena->cfg, "hs_powerball", "resetdelay", 700);
		ad->cfg_rewardShiftDelay = cfg->GetInt(arena->cfg, "hs_powerball", "rewardshiftdelay", 4);
		ad->cfg_maximumReward = cfg->GetInt(arena->cfg, "hs_powerball", "maximumreward", 400);
		ad->cfg_jackpotDividerMin = cfg->GetInt(arena->cfg, "hs_powerball", "jackpotdividermin", 1);
		ad->cfg_jackpotDividerMax = cfg->GetInt(arena->cfg, "hs_powerball", "jackpotdividermax", 4);
		ad->cfg_offsetUpDifficulty = cfg->GetInt(arena->cfg, "hs_powerball", "offsetupdifficulty", 1);
		{
			const char *tmp;

			ad->resetDelay = ad->cfg_resetDelay;
			ad->rewardShiftDelay = prng->Number(ad->cfg_rewardShiftDelay - 1, ad->cfg_rewardShiftDelay + 1);

			tmp = mapdata->GetAttr(arena, "goalcount");
			if (tmp)
				ad->ma_goalCount = atoi(tmp);
			else
				ad->ma_goalCount = 0;

			if (ad->ma_goalCount)
			{
				lm->LogA(L_INFO, "hs_powerball", arena, "%i goals", ad->ma_goalCount);
				ad->rgn_goals = amalloc(sizeof(Region *) * ad->ma_goalCount);
				ad->goal_rewardFactors = amalloc(sizeof(int) * ad->ma_goalCount);
				ad->goal_rewardOffsets = amalloc(sizeof(int) * ad->ma_goalCount);
			}
			else
			{
				lm->LogA(L_WARN, "hs_powerball", arena, "0 goals");
			}

			rewardShift(arena);
		}

		mm->RegCallback(CB_GOAL, ballgoal, arena);
		mm->RegCallback(CB_BALLFIRE, ballfire, arena);
		mm->RegCallback(CB_BALLPICKUP, ballpickup, arena);
		mm->RegCallback(CB_ARENAACTION, arenaaction, arena);


		ml->SetTimer(perSecond, 100, 100, arena, arena);

		cmd->AddCommand("goals", Cgoals, arena, goals_help);
		cmd->AddCommand("powerballreset", Cpowerballreset, arena, powerballreset_help);

		//finally, return success.

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		//first try to unregister the interface if exposing one.
		//UNREG_ARENAINTERFACE();

		//unregister global commands, timers, and callbacks.
		//remember to clear ALL timers this arena was using even if they were not set in the MM_ATTACH phase..
		mm->UnregCallback(CB_GOAL, ballgoal, arena);
		mm->UnregCallback(CB_BALLFIRE, ballfire, arena);
		mm->UnregCallback(CB_BALLPICKUP, ballpickup, arena);
		mm->UnregCallback(CB_ARENAACTION, arenaaction, arena);

		ml->ClearTimer(perSecond, arena);

		cmd->RemoveCommand("goals", Cgoals, arena);
		cmd->RemoveCommand("powerballreset", Cpowerballreset, arena);

		//free other things in arena data.
		if (ad->ma_goalCount)
		{
			afree(ad->rgn_goals);
			afree(ad->goal_rewardFactors);
			afree(ad->goal_rewardOffsets);
		}

		//unreg per-league-player data if we're using it.
		//UNREG_PLEAGUEP_DATA();

//Lfailattach:

		//release this arena's per-arena data
		//including all player's per-player data this module would use
		FREE_ARENA_PLAYER_DATA(arena);
		FREE_ARENA_DATA(ad);

		//returns MM_FAIL if we jumped from a failed GETARENAINT or other MM_ATTACH action, returns MM_OK if not.
		DO_RETURN();
	}


	return MM_FAIL;
}

//body:perSecond
local int perSecond(void *param)
{
	Arena *a = (Arena *)param;
	DEF_AD(a);

	if (!ad->ma_goalCount || !ad->active)
		return 1;

	--ad->resetDelay;
	if (ad->resetDelay == 60)
	{
		chat->SendArenaMessage(a, "NOTICE: Powerball game will be automatically reset if no goals are scored in the next 60 seconds.");
	}
	else if (ad->resetDelay <= 0)
	{
		chat->SendArenaMessage(a, "Powerball game reset.");
		reset(a);
	}

	return 1;
}

//body: playeraction
local void arenaaction(Arena *a, int action)
{
	DEF_AD(a);
	if (!ad)
		return;

	if (action == AA_CONFCHANGED)
	{
		ad->cfg_minimumRewardFactor = cfg->GetInt(a->cfg, "hs_powerball", "minimumrewardfactor", 25);
		ad->cfg_maximumRewardFactor = cfg->GetInt(a->cfg, "hs_powerball", "maximumrewardfactor", 40);
		ad->cfg_minimumRewardOffset = cfg->GetInt(a->cfg, "hs_powerball", "minimumrewardoffset", -35);
		ad->cfg_maximumRewardOffset = cfg->GetInt(a->cfg, "hs_powerball", "maximumrewardoffset", 20);
		ad->cfg_resetDelay = cfg->GetInt(a->cfg, "hs_powerball", "resetdelay", 700);
		ad->cfg_rewardShiftDelay = cfg->GetInt(a->cfg, "hs_powerball", "rewardshiftdelay", 4);
		ad->cfg_maximumReward = cfg->GetInt(a->cfg, "hs_powerball", "maximumreward", 500);
		ad->cfg_jackpotDividerMin = cfg->GetInt(a->cfg, "hs_powerball", "jackpotdividermin", 1);
		ad->cfg_jackpotDividerMax = cfg->GetInt(a->cfg, "hs_powerball", "jackpotdividermax", 5);
		ad->cfg_offsetUpDifficulty = cfg->GetInt(a->cfg, "hs_powerball", "offsetupdifficulty", 1);
	}
}

local void SpawnBall(Arena *arena, int bid)
{
	int cx, cy, rad, x, y;
	struct BallData d;

	d.state = BALL_ONMAP;
	d.xspeed = d.yspeed = 0;
	d.carrier = NULL;
	d.time = current_ticks();

	cx = cfg->GetInt(arena->cfg, "Soccer", "SpawnX", 512);
	cy = cfg->GetInt(arena->cfg, "Soccer", "SpawnY", 512);
	rad = cfg->GetInt(arena->cfg, "Soccer", "SpawnRadius", 1);

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
	balls->PlaceBall(arena, bid, &d);
}


local void ballpickup(Arena *arena, Player *p, int bid)
{
	DEF_AD(arena);
	ad->active = 1;
}


local void ballfire(Arena *a, Player *p, int bid)
{
	DEF_AD(a);
	struct ArenaBallData *abd;
	struct BallData bd;
	int x, y;
	if (!ad)
		return;

	abd = balls->GetBallData(a);
	bd = abd->balls[bid];
	balls->ReleaseBallData(a);
	x = bd.x / 16;
	y = bd.y / 16;

	if (!ad->rgn_nopowerball)
		ad->rgn_nopowerball = mapdata->FindRegionByName(a, "nopowerball");

	if (ad->rgn_nopowerball && mapdata->Contains(ad->rgn_nopowerball, x, y))
	{
		chat->SendMessage(p, "The powerball disappears into the void...");
		SpawnBall(a, bid);
	}
}

local void ballgoal(Arena *a, Player *p, int bid, int x, int y)
{
	int n;
	Player *i;
	Link *link;
	int reward, jpReward;
	int gn;
	const char *goalName;
	DEF_AD(a);
	if (!ad)
		return;
	if (!ad->ma_goalCount)
		return;

	gn = lookupGoalNumber(a, x, y);
	goalName = lookupGoalName(a, gn);
	reward = calculateReward(a, gn, p->p_freq);
	jpReward = prng->Number(reward / ad->cfg_jackpotDividerMax, reward / ad->cfg_jackpotDividerMin);

	lm->LogA(L_INFO, "hs_powerball", a, "goal scored on gn %i by freq %i giving $%i, jackpot+$%i", gn, p->p_freq, reward, jpReward);

	PDLOCK;
	FOR_EACH_PLAYER(i)
	{
		if (p->type == T_FAKE)
			continue;
		if (!IS_IN(i, a))
			continue;

		if (IS_ON_FREQ(i, a, p->p_freq))
		{
			chat->SendMessage(i, "Team goal in %s by %s!  Reward: $%i  Jackpot +$%i", goalName, p->name, reward, jpReward);
			if ((i->position.status & STATUS_SAFEZONE) || IS_SPEC(i))
			{
				continue;
			}
			else
			{
				database->addMoney(i, MONEY_TYPE_BALL, reward);
			}
		}
		else
		{
			chat->SendMessage(i, "Enemy goal in %s by %s (worth $%i)!  Jackpot +$%i", goalName, p->name, reward, jpReward);
		}
	}
	PDUNLOCK;
	jackpot->AddJP(a, jpReward);

	//this goal was scored on, so lean it towards being less valuable later.

	if (ad->ma_goalCount > 1)
	{
		ad->goal_rewardOffsets[gn] -= 1;
		if (ad->goal_rewardOffsets[gn] < ad->cfg_minimumRewardOffset)
		{
			ad->goal_rewardOffsets[gn] = ad->cfg_minimumRewardOffset;
		}
		else
		{
			int odds = ad->cfg_offsetUpDifficulty + ad->ma_goalCount;
			if (odds < 1)
				odds = 1;

			for (n = 0; n < ad->ma_goalCount; ++n)
			{
				if (n == gn)
					continue;
				if ((prng->Rand() % odds) == 0)
				{
					//this goal is randomly chosen to be increased in value because it was not scored on this time.
					ad->goal_rewardOffsets[n] += 1;

					if (ad->goal_rewardOffsets[n] > ad->cfg_maximumRewardOffset)
					{
						ad->goal_rewardOffsets[n] = ad->cfg_maximumRewardOffset;
						break;
					}
				}
			}
		}
	}

	--ad->rewardShiftDelay;
	if (ad->rewardShiftDelay <= 0)
	{
		rewardShift(a);
		chat->SendArenaMessage(a, "Goal rewards have shifted.");
	}


	ad->resetDelay += ad->cfg_resetDelay / 2;
	if (ad->resetDelay > ad->cfg_resetDelay)
		ad->resetDelay = ad->cfg_resetDelay;
}

local void reset(Arena *a)
{
	DEF_AD(a);
	ArenaBallData *abd;
	int ballcount;

	if (!ad)
		return;
	if (!ad->ma_goalCount)
		return;

	rewardShift(a);
	ad->resetDelay = ad->cfg_resetDelay;
	ad->active = 0;

	abd = balls->GetBallData(a);
	balls->ReleaseBallData(a);
	ballcount = abd->ballcount;

	balls->SetBallCount(a, 0);
	balls->SetBallCount(a, ballcount);
}

local int lookupGoalNumber(Arena *a, int x, int y)
{
	char buffer[16];
	int i;
	int result = 0;
	DEF_AD(a);
	if (!ad)
		return 0;
	if (!ad->ma_goalCount)
		return 0;

	for (i = 0; i < ad->ma_goalCount; ++i)
	{
		if (!ad->rgn_goals[i])	//cache the Region * on the first time getting this far.
		{
			sprintf(buffer, "goal%i", i);
			ad->rgn_goals[i] = mapdata->FindRegionByName(a, buffer);
		}

		if (ad->rgn_goals[i])	//note that if the region was not found, it will continue trying to cache a Region *.
		{
			if (mapdata->Contains(ad->rgn_goals[i], x, y))
			{
				result = i;
				break;
			}
		}
	}

	return result;
}

local void rewardShift(Arena *a)
{
	DEF_AD(a);
	int i;
	if (!ad)
		return;

	ad->rewardShiftDelay = prng->Number(ad->cfg_rewardShiftDelay - 1, ad->cfg_rewardShiftDelay + 1);

	if (!ad->ma_goalCount)
		return;

	for (i = 0; i < ad->ma_goalCount; ++i)
	{
		ad->goal_rewardFactors[i] = prng->Number(ad->cfg_minimumRewardFactor + ad->goal_rewardOffsets[i], ad->cfg_maximumRewardFactor + ad->goal_rewardOffsets[i]);
	}
}

local int calculateReward(Arena *a, int gn, int freq)
{
	DEF_AD(a);
	int allies = 0;
	int enemies = 0;
	int N;
	int reward;
	Player *p;
	Link *link;

	if (gn < 0)
		gn = 0;
	else if (gn >= ad->ma_goalCount)
		gn = 0;

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		if (p->type == T_FAKE)
			continue;
		if (!IS_IN(p, a))
			continue;
		if (IS_SPEC(p))
			continue;
		if (p->p_freq == freq)
		{
			++allies;
		}
		else
		{
			++enemies;
		}
	}
	PDUNLOCK;

	N = (int)((float)enemies * 2.f) - (int)((float)allies * 1.2f);

	if (N <= 0)
	{
		reward = 0;
	}
	else
	{
		reward = (int) ( ((double)ad->goal_rewardFactors[gn]) * log((double)(N*N)) );
	}

	if (reward > ad->cfg_maximumReward)
		reward = ad->cfg_maximumReward;
	else if (reward < 0)
		reward = 0;

	return reward;
}

local const char *lookupGoalName(Arena *a, int gn)
{
	const char *result;
	char buffer[16];
	sprintf(buffer, "goal%i", gn);

	result = mapdata->GetAttr(a, buffer);
	if (!result)
		result = "???";

	return result;
}

//?goals
//body:Cgoals
local void Cgoals(const char *cmd, const char *params, Player *p, const Target *target)
{
	int switchF = strstr(params, "-f") != NULL;
	int i;
	DEF_AD(p->arena);
	if (!ad)
		return;
	if (!ad->ma_goalCount)
		return;

	if (p->p_freq != p->arena->specfreq)
	{
		chat->SendMessage(p, " %-15.15s  %-6.6s  Reward for Freq %i", "Goal", "Factor", p->p_freq);


		for (i = 0; i < ad->ma_goalCount; ++i)
		{
			if (!switchF)
				chat->SendMessage(p, "%-16.16s  %6i  $%i", lookupGoalName(p->arena, i), ad->goal_rewardFactors[i], calculateReward(p->arena, i, p->p_freq));
			else
				chat->SendMessage(p, "%-16.16s  %6i  %3i  $%i", lookupGoalName(p->arena, i), ad->goal_rewardFactors[i], ad->goal_rewardOffsets[i], calculateReward(p->arena, i, p->p_freq));
		}
	}
	else
	{
		chat->SendMessage(p, " %-15.15s  %-6.6s", "Goal", "Factor");

		for (i = 0; i < ad->ma_goalCount; ++i)
		{
			if (!switchF)
				chat->SendMessage(p, "%-16.16s  %6i", lookupGoalName(p->arena, i), ad->goal_rewardFactors[i]);
			else
				chat->SendMessage(p, "%-16.16s  %6i  %3i", lookupGoalName(p->arena, i), ad->goal_rewardFactors[i], ad->goal_rewardOffsets[i]);
		}
	}

	if (strstr(params, "-cfg"))
	{
		chat->SendMessage(p, "minrf:%i maxrf:%i minro:%i maxro:%i rd:%i rsd:%i max:%i jdmin:%i jdmax:%i",
		ad->cfg_minimumRewardFactor, ad->cfg_maximumRewardFactor, ad->cfg_minimumRewardOffset, ad->cfg_maximumRewardOffset, ad->cfg_resetDelay,
		ad->cfg_rewardShiftDelay, ad->cfg_maximumReward, ad->cfg_jackpotDividerMin, ad->cfg_jackpotDividerMax);
	}
}

//?powerballreset
//body:Cpowerballreset
local void Cpowerballreset(const char *cmd, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);
	if (!ad)
		return;
	if (!ad->ma_goalCount)
		return;

	chat->SendArenaMessage(p->arena, " ** %s reset the powerball game.", p->name);
	reset(p->arena);
}

