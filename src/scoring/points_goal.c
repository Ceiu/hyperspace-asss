
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"


/* Soccer modes:
 * ALL - All goals are open for scoring by any freq.
 * LEFTRIGHT - Two goals, scoring is even freqs vs odd freqs.
 * TOPBOTTOM - Same as LEFTRIGHT but goals oriented vertically.
 * CORNERS_3_1 - Each freq (0-3) has one goal to defend, and three to score on.
 * CORNERS_1_3 -  Each freq (0-3) has three goals to defend, and one to score on.
 * SIDES_3_1 - Same as CORNERS_3_1, but using left/right/top/bottom goals.
 * SIDES_1_3 - Same as SIDES_3_1 (goal orientations), except uses mode 4 rules.
 * 1_3 rules:  Birds(0) take pts from Levs(3)
 *             Javs (1) take pts from Spid(2)
 *             Spids(2) take pts from Javs(1)
 *             Levs (3) take pts from Bird(0)
 */

#define GOALMODE_MAP(F) \
	F(GOAL_ALL)  \
	F(GOAL_LEFTRIGHT)  \
	F(GOAL_TOPBOTTOM)  \
	F(GOAL_CORNERS_3_1)  \
	F(GOAL_CORNERS_1_3)  \
	F(GOAL_SIDES_3_1)  \
	F(GOAL_SIDES_1_3)

DEFINE_ENUM(GOALMODE_MAP)
DEFINE_FROM_STRING(goalmode_val, GOALMODE_MAP)

#define MAXFREQ  CFG_SOCCER_MAXFREQ
#define MAXGOALS CFG_SOCCER_MAXGOALS


typedef struct GoalAreas
{
	int upperleft_x;        // coord of first tile in upper-left
	int upperleft_y;        // corner
	int width;              // guess
	int height;             // .. for now just a rectangular approximation =P
	int goalfreq;           // owner freq of goal
} GoalAreas;

struct ArenaScores
{
	int mode;                       // stores type of soccer game, 0-6 by default
	int stealpts;                   // 0 = absolute scoring, else = start value for each team
	int cpts, reward, winby;
	int score[MAXFREQ];             // score each freq has
	GoalAreas goals[MAXGOALS];      // array of goal-defined areas for >2 goal arenas
};


/* prototypes */
local void MyGoal(Arena *, Player *, int, int, int);
local void MyAA(Arena *, int);
#if 0
local int  IdGoalScored(int, int, int);
#endif
local int  RewardPoints(Arena *, int);
local void CheckGameOver(Arena *, int);
local void ScoreMsg(Arena *, Player *);
local void Csetscore(const char *, const char *,Player *, const Target *);
local void Cscore(const char *, const char *, Player *, const Target *);
local void Cresetgame(const char *, const char *, Player *, const Target *);
local helptext_t setscore_help, score_help, resetgame_help;

/* global data */
local int scrkey;

local Imodman *mm;
local Iplayerdata *pd;
local Iballs *balls;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Istats *stats;

EXPORT const char info_points_goal[] = CORE_MOD_INFO("points_goal");

EXPORT int MM_points_goal(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);

		scrkey = aman->AllocateArenaData(sizeof(struct ArenaScores));
		if (scrkey == -1) return MM_FAIL;

		cmd->AddCommand("setscore",Csetscore, ALLARENAS, setscore_help);
		cmd->AddCommand("score",Cscore, ALLARENAS, score_help);
		cmd->AddCommand("resetgame",Cresetgame, ALLARENAS, resetgame_help);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("setscore",Csetscore, ALLARENAS);
		cmd->RemoveCommand("score",Cscore, ALLARENAS);
		cmd->RemoveCommand("resetgame",Cresetgame, ALLARENAS);
		aman->FreeArenaData(scrkey);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(stats);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_ARENAACTION, MyAA, arena);
		mm->RegCallback(CB_GOAL, MyGoal, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_GOAL, MyGoal, arena);
		mm->UnregCallback(CB_ARENAACTION, MyAA, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


void MyAA(Arena *arena, int action)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);

	/* FIXME: add AA_CONFCHANGED support */
	if (action == AA_CREATE)
	{
		int i, cpts;

		/* cfghelp: Soccer:Mode, arena, enum, def: GOAL_ALL
		 * Goal configuration:
		 * GOAL_ALL = any goal,
		 * GOAL_LEFTRIGHT = left-half/right-half,
		 * GOAL_TOPBOTTOM = top-half/bottom-half,
		 * GOAL_CORNERS_3_1 = quadrants-defend-one-goal,
		 * GOAL_CORNERS_1_3 = quadrants-defend-three-goals,
		 * GOAL_SIDES_3_1 = sides-defend-one-goal,
		 * GOAL_SIDES_1_3 = sides-defend-three-goals */
		scores->mode =
			goalmode_val(cfg->GetStr(arena->cfg, "Soccer", "Mode"), GOAL_ALL);
		/* cfghelp: Soccer:CapturePoints, arena, int, def: 1
		 * If positive, these points are distributed to each goal/team.
		 * When you make a goal, the points get transferred to your
		 * goal/team. If one team gets all the points, then they win as
		 * well.  If negative, teams are given 1 point for each goal,
		 * first team to reach -CapturePoints points wins the game. */
		cpts = scores->cpts = cfg->GetInt(arena->cfg, "Soccer", "CapturePoints", 1);
		/* cfghelp: Soccer:Reward, arena, int, def: 0
		 * Negative numbers equal absolute points given,
		 * positive numbers use FlagReward formula. */
		scores->reward = cfg->GetInt(arena->cfg, "Soccer", "Reward", 0);
		/* cfghelp: Soccer:WinBy, arena, int, def: 0
		 * Have to beat other team by this many goals */
		scores->winby  = cfg->GetInt(arena->cfg, "Soccer", "WinBy",0);

		if (cpts < 0)
		{
			scores->stealpts = 0;
			for(i = 0; i < MAXFREQ; i++)
				scores->score[i] = 0;
		}
		else
		{
			scores->stealpts = cpts;
			for(i = 0; i < MAXFREQ; i++)
				scores->score[i] = cpts;
		}

#if 0
		/* setup for custom mode in future */
		{
			int goalc = 0, gf, cx, cy, w, h, gf;
			const char *g;
			char goalstr[8];

			for (i = 0; i < MAXGOALS; i++)
			{
				scores->goals[i].upperleft_x = -1;
				scores->goals[i].upperleft_y = -1;
				scores->goals[i].width = -1;
				scores->goals[i].height = -1;
				scores->goals[i].goalfreq = -1;
			}

			if (scores->mode == 7)
			{
				g = goalstr;
				for(i=0;(i < MAXGOALS) && g;i++) {
					sprintf(goalstr,"Goal%d",goalc);
					g = cfg->GetStr(arena->cfg, "Soccer", goalstr);
					if (g && sscanf(g, "%d,%d,%d,%d,%d", &cx, &cy, &w, &h, &gf) == 5)
					{
						scores->goals[i].upperleft_x = cx;
						scores->goals[i].upperleft_y = cy;
						scores->goals[i].width = w;
						scores->goals[i].height = h;
						scores->goals[i].goalfreq = gf;
					}
					goalc++;
				}
			}
		}
#endif
	}
}


void MyGoal(Arena *arena, Player *p, int bid, int x, int y)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int freq = -1, nullgoal = 0, i;
	Player *j;
	LinkedList teamset = LL_INITIALIZER, nmeset = LL_INITIALIZER;
	Link *link;

	ArenaBallData *abd = balls->GetBallData(arena);

	switch (scores->mode)
	{
		case GOAL_ALL:
			freq = abd->balls[bid].freq;
			break;

		case GOAL_LEFTRIGHT:
			freq = x < 512 ? 1 : 0;
			scores->score[freq]++;
			if (scores->stealpts) scores->score[(~freq)+2]--;
			break;

		case GOAL_TOPBOTTOM:
			freq = y < 512 ? 1 : 0;
			scores->score[freq]++;
			if (scores->stealpts) scores->score[(~freq)+2]--;
			break;

		case GOAL_CORNERS_3_1:
			freq = abd->balls[bid].freq;
			if (x < 512)
				i = y < 512 ? 0 : 2;
			else
				i = y < 512 ? 1 : 3;

			if (!scores->stealpts) scores->score[freq]++;
			else if (scores->score[i])
			{
				scores->score[freq]++;
				scores->score[i]--;
			}
			else nullgoal = 1;
			break;

		case GOAL_CORNERS_1_3: /* only use absolute scoring, as stealpts game is pointless */
			freq = abd->balls[bid].freq;
			scores->score[freq]++;
			break;

		case GOAL_SIDES_3_1:
			freq = abd->balls[bid].freq;
			if (x < y)
				i = x < (1024-y) ? 0 : 1;
			else
				i = x < (1024-y) ? 2 : 3;

			if (!scores->stealpts) scores->score[freq]++;
			else if (scores->score[i])
			{
				scores->score[freq]++;
				scores->score[i]--;
			}
			else nullgoal = 1;
			break;

		case GOAL_SIDES_1_3:
			freq = abd->balls[bid].freq;
			scores->score[freq]++;
			break;
	}

	pd->Lock();
	FOR_EACH_PLAYER(j)
		if (j->status == S_PLAYING &&
		    j->arena == arena)
		{
			if (j->p_freq == freq)
				LLAdd(&teamset, j);
			else
				LLAdd(&nmeset, j);
		}
	pd->Unlock();

	if (scores->reward)
	{
		int points = RewardPoints(arena, freq);

		chat->SendSetSoundMessage(&teamset, SOUND_GOAL,
			"Team Goal! by %s  Reward:%d", p->name, points);
		chat->SendSetSoundMessage(&nmeset, SOUND_GOAL,
			"Enemy Goal! by %s  Reward:%d", p->name, points);

		stats->SendUpdates(NULL);
	}
	else
	{
		chat->SendSetSoundMessage(&teamset, SOUND_GOAL, "Team Goal! by %s", p->name);
		chat->SendSetSoundMessage(&nmeset, SOUND_GOAL, "Enemy Goal! by %s", p->name);
		if (nullgoal) chat->SendArenaMessage(arena,"Enemy goal had no points to give.");
	}
	LLEmpty(&teamset); LLEmpty(&nmeset);

	if (scores->mode)
	{
		ScoreMsg(arena, NULL);
		CheckGameOver(arena, bid);
	}

	abd->balls[bid].freq = -1;

	balls->ReleaseBallData(arena);
}


#if 0
int IdGoalScored (Arena *arena, int x, int y)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i = 0, xmin, xmax, ymin, ymax;

	for(i=0; (i < MAXGOALS) && (scores->goals[i].upperleft_x != -1); i++)
	{
		chat->SendArenaMessage(arena,"goal %d: %d,%d,%d,%d,%d",i,
			scores->goals[i].upperleft_x,
			scores->goals[i].upperleft_y,
			scores->goals[i].width,
			scores->goals[i].height,
			scores->goals[i].goalfreq);


		xmin = scores->goals[i].upperleft_x;
		xmax = xmin + scores->goals[i].width;
		ymin = scores->goals[i].upperleft_y;
		ymax = ymin + scores->goals[i].height;

		if ((x >= xmin) && (x <= xmax) && (y >= ymin) && (y <= ymax))
		{
			return scores->goals[i].goalfreq;
		}
	}

	return -1;
}
#endif


int RewardPoints(Arena *arena, int winfreq)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	LinkedList set = LL_INITIALIZER;
	int players = 0, points;
	int reward = scores->reward;
	int j, freqcount = 0, freq[4] = { 0, 0, 0, 0 };
	Link *link;
	Player *i;

	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->status == S_PLAYING &&
		    i->arena == arena &&
		    i->p_ship != SHIP_SPEC)
		{
			players++;
			freq[i->p_freq % 4]++;
			if (i->p_freq == winfreq)
			{
				stats->IncrementStat(i, STAT_BALL_GAMES_WON, 1);
				/* only do reward points if not in safe zone */
				if (!(i->position.status & STATUS_SAFEZONE))
					LLAdd(&set, i);
			}
			else
				stats->IncrementStat(i, STAT_BALL_GAMES_LOST, 1);
		}
	pd->Unlock();

	if (reward < 1)
		points = reward * -1;
	else
		points = players * players * reward / 1000;

	for (j = 0; j < 4; j++)
		if (freq[j] > 0)
			freqcount++;

	/* cfghelp: Soccer:MinPlayers, arena, int, def: 0
	 * The minimum number of players who must be playing for soccer
	 * points to be awarded. */
	/* cfghelp: Soccer:MinTeams, arena, int, def: 0
	 * The minimum number of teams that must exist for soccer points to
	 * be awarded. */
	if (players < cfg->GetInt(arena->cfg, "Soccer", "MinPlayers", 0) ||
	    freqcount < cfg->GetInt(arena->cfg, "Soccer", "MinTeams", 0))
	{
		points = 0;
	}
	else
	{
		for (link = LLGetHead(&set); link; link = link->next)
			stats->IncrementStat(link->data, STAT_FLAG_POINTS, points);
	}
	LLEmpty(&set);

	/* don't SendUpdates here, wait until after the balls->EndGame */

	return points;
}

void CheckGameOver(Arena *arena, int bid)
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i, j = 0, freq = 0;

	for(i = 0; i < MAXFREQ; i++)
		if (scores->score[i] > scores->score[freq]) freq = i;

	// check if game is over
	if (scores->mode <= 2 && scores->stealpts)
	{
		if (!scores->score[(~freq)+2]) // check opposite freq (either 0 or 1)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			RewardPoints(arena, freq);
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores->score[i] = scores->stealpts;
		}
	}
	else if (scores->mode > 2 && scores->stealpts)
	{
		for (i = 0, j = 0; i < 4; i++) // check that other 3 freqs have no points
			if (!scores->score[i]) j++;

		if (j == 3)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			RewardPoints(arena, freq);
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores->score[i] = scores->stealpts;
		}
	}
	else // is mode 1-6 with absolute scoring
	{
		int win = scores->cpts;
		int by  = scores->winby;

		if (scores->score[freq] >= win*-1)
			for(i = 0; i < MAXFREQ; i++)
				if ((scores->score[i]+by) <= scores->score[freq]) j++;

		if (j == MAXFREQ-1)
		{
			chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
			RewardPoints(arena, freq);
			balls->EndGame(arena);
			for(i=0;i < MAXFREQ;i++)
				scores->score[i] = 0;

		}
	}

}

void ScoreMsg(Arena *arena, Player *p)  // p = NULL means arena-wide, otherwise private
{
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int cfg_freqtypes = cfg->GetInt(arena->cfg, "Misc", "FrequencyShipTypes", 0);
	char msg[256];

	/* no score message shown when mode = 0 */
	if (scores->mode == 0)
		return;

	if (scores->mode > 2)
	{
		/* modes 3, 4, 5 and 6 show 4 teams */
		const char *fmt = cfg_freqtypes ?
			"SCORE: Warbirds:%d Javelins:%d Spiders:%d Leviathans:%d" :
			"SCORE: Team0:%d Team1:%d Team2:%d Team3:%d";
		snprintf(msg, sizeof(msg), fmt,
				scores->score[0], scores->score[1],
				scores->score[2], scores->score[3]);
	}
	else
	{
		/* modes 1 and 2 only have 2 teams */
		const char *fmt = cfg_freqtypes ?
			"SCORE: Warbirds:%d Javelins:%d" :
			"SCORE: Evens:%d Odds:%d";
		snprintf(msg, sizeof(msg), fmt,
				scores->score[0], scores->score[1]);
	}

	if (p)
		chat->SendMessage(p, "%s", msg);
	else
		chat->SendArenaMessage(arena, "%s", msg);
}


local helptext_t setscore_help =
"Targets: none\n"
"Args: <freq 0 score> [<freq 1 score> [... [<freq 7 score>]]]\n"
"Changes score of current soccer game, based on arguments. Only supports\n"
"first eight freqs, and arena must be in absolute scoring mode \n"
"(Soccer:CapturePoints < 0).\n";

void Csetscore(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i, newscores[MAXFREQ];

	/* crude for now */
	for(i = 0; i < MAXFREQ; i++)
		newscores[i] = -1;

	if (sscanf(params,"%d %d %d %d %d %d %d %d", &newscores[0], &newscores[1], &newscores[2],
		&newscores[3], &newscores[4], &newscores[5],&newscores[6], &newscores[7]) > 0)
	{
		// only allowed to setscore in modes 1-6 and if game is absolute scoring
		if (!scores->mode) return;
		if (scores->stealpts) return;

		for(i = 0; i < MAXFREQ && newscores[i] != -1; i ++)
			scores->score[i] = newscores[i];

		if (scores->mode) {
			ScoreMsg(arena, NULL);
			CheckGameOver(arena, -1);
		}
	}
	else
		chat->SendMessage(p,"setscore format: *setscore x y z .... where x = freq 0, y = 1,etc");
}


local helptext_t score_help =
"Targets: none\n"
"Args: none\n"
"Returns current score of the soccer game in progress.\n";

void Cscore(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);

	if (scores->mode) ScoreMsg(arena, p);
}


local helptext_t resetgame_help =
"Targets: none\n"
"Args: none\n"
"Resets soccer game scores and balls.\n";

void Cresetgame(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	struct ArenaScores *scores = P_ARENA_DATA(arena, scrkey);
	int i, j = 0;

	if (scores->mode)
	{
		chat->SendArenaMessage(arena, "Resetting game. -%s", p->name);
		chat->SendArenaSoundMessage(arena, SOUND_DING, "Soccer game over.");
		balls->EndGame(arena);
		if (scores->stealpts) j = scores->stealpts;
		for(i = 0; i < MAXFREQ; i++)
			scores->score[i] = j;
	}
}

