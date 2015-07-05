#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "asss.h"
#include "formula.h"
#include "hscore.h"
#include "jackpot.h"

//Structs
typedef struct ArenaData
{
	Region *noPowerballRegion;

    struct
    {
        Formula *goalRewardFormula;
        Formula *goalJackpotFormula;
    } formulas;

    struct
    {
        int resetDelay;
    } config;

    int resetDelay; //Starts at config.resetDelay, reduced by 1 every second.
    int active;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

//Module interface
local Imodman *mm;

//Other interfaces
local Iarenaman *aman;
local Iballs *balls;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Iformula *formula;
local Ijackpot *jackpot;
local Ilogman *lm;
local Imapdata *map;
local Imainloop *ml;
local Ihscoredatabase *database;
local Iplayerdata *pd;
local Iprng *prng;

//Prototypes
local void Cpowerballreset(const char *cmd, const char *params, Player *p, const Target *target);

local void ballPickupCB(Arena *arena, Player *p, int bid);
local void ballFireCB(Arena *arena, Player *p, int bid);
local void goalCB(Arena *arena, Player *p, int bid, int x, int y);

local void spawnBall(Arena *arena, int bid);
local void resetBalls(Arena *arena);
local bool readConfig(Arena *arena);
local bool getFormulas(Arena *arena);
local void freeFormulas(Arena *arena);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Commands
local helptext_t powerballreset_help =
"Targets: arena\n"
"Syntax: ?powerballreset\n"
"Respawns each powerball.\n";
local void Cpowerballreset(const char *cmd, const char *params, Player *p, const Target *target)
{
	chat->SendArenaMessage(p->arena, "Powerball game reset.");
	resetBalls(p->arena);
}

local void ballPickupCB(Arena *arena, Player *p, int bid)
{
    ArenaData *adata = getArenaData(arena);

    adata->active = 1;
}

//Callbacks
local void ballFireCB(Arena *arena, Player *p, int bid)
{
	ArenaData *adata = getArenaData(arena);

	struct ArenaBallData *abd;
	struct BallData bd;
	int x, y;

	abd = balls->GetBallData(arena);
	bd = abd->balls[bid];
	balls->ReleaseBallData(arena);

	x = bd.x / 16;
	y = bd.y / 16;

	if (!adata->noPowerballRegion) adata->noPowerballRegion = map->FindRegionByName(arena, "nopowerball");

	if (adata->noPowerballRegion && map->Contains(adata->noPowerballRegion, x, y))
	{
		chat->SendMessage(p, "The powerball disappears into the void...");
		spawnBall(arena, bid);
	}
}

local void goalCB(Arena *arena, Player *p, int bid, int x, int y)
{
    ArenaData *adata = getArenaData(arena);

    //Get our formulas ready for use
    FormulaVariable arenaVariable, freqVariable;
    arenaVariable.name = NULL;
    arenaVariable.type = VAR_TYPE_ARENA;
    arenaVariable.arena = arena;

    freqVariable.name = NULL;
    freqVariable.type = VAR_TYPE_FREQ;
    freqVariable.freq.arena = arena;
    freqVariable.freq.freq = p->p_freq;

    HashTable *variables = HashAlloc();
    HashAdd(variables, "arena", &arenaVariable);
    HashAdd(variables, "freq", &freqVariable);

    char errorBuffer[200];
    errorBuffer[0] = '\0';

    //Evaluate formulas, if any of them 'error', we're not doing anything this goal.

    int reward = formula->EvaluateFormulaInt(adata->formulas.goalRewardFormula,
        variables, NULL, errorBuffer, sizeof(errorBuffer), 0);

    if (errorBuffer[0] != '\0')
    {
        lm->LogA(L_WARN, "hs_spi_powerball", arena, "Error with GoalRewardFormula: %s", errorBuffer);

        HashFree(variables);
        return;
    }

    int jackpotIncrease = formula->EvaluateFormulaInt(adata->formulas.goalJackpotFormula,
        variables, NULL, errorBuffer, sizeof(errorBuffer), 0);

    if (errorBuffer[0] != '\0')
    {
        lm->LogA(L_WARN, "hs_spi_powerball", arena, "Error with GoalJackpotFormula: %s", errorBuffer);

        HashFree(variables);
        return;
    }

    HashFree(variables);

    //Alright, we're set to pay out the money.
    Player *i;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
        if (!IS_STANDARD(i))
           continue;
		if (i->arena != p->arena)
		   continue;

		if (i->p_freq == p->p_freq)
        {
            chat->SendMessage(i, "Team goal by %s!  Reward: $%i  Jackpot +$%i", p->name, reward, jackpotIncrease);

            if ((i->position.status & STATUS_SAFEZONE) || i->p_ship == SHIP_SPEC)
			{
				continue;
			}

			database->addMoney(i, MONEY_TYPE_BALL, reward);
        }
        else
        {
            chat->SendMessage(i, "Enemy goal by %s (worth $%i)!  Jackpot +$%i", p->name, reward, jackpotIncrease);
        }
	}
	pd->Unlock();

    //Increase jackpot
    jackpot->AddJP(arena, jackpotIncrease);

    adata->resetDelay = adata->config.resetDelay;
}

//Timers
local int secondTimer(void *param)
{
	Arena *arena = (Arena *)param;
	ArenaData *adata = getArenaData(arena);

    if (!adata->active)
    {
        // Ball is not yet active. We don't need to reset it yet.
        return 1;
    }

	adata->resetDelay--;
	int tenth = adata->config.resetDelay / 10;

	if (adata->resetDelay == tenth)
	{
		chat->SendArenaMessage(arena, "NOTICE: Powerball game will be automatically reset if no goals are scored in the next %d seconds.", tenth);
	}
	else if (adata->resetDelay <= 0)
	{
		chat->SendArenaMessage(arena, "Powerball game reset.");
		resetBalls(arena);
	}

	return 1;
}

//Misc/utils
local void spawnBall(Arena *arena, int bid) // function written by Arnk Dylie for hs_powerball
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
		map->FindEmptyTileNear(arena, &x, &y);
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

local void resetBalls(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    adata->resetDelay = adata->config.resetDelay;
    adata->active = 0;

    int ballcount = (balls->GetBallData(arena))->ballcount;
	balls->ReleaseBallData(arena);

    balls->SetBallCount(arena, 0);
	balls->SetBallCount(arena, ballcount);
}

local bool readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    //Formulas
    if (!getFormulas(arena))
       return false;

    //Misc configs
    adata->config.resetDelay = cfg->GetInt(ch, "HS_Spi_Powerball", "ResetDelay", 600);
    adata->resetDelay = adata->config.resetDelay;

    return true;
}

local bool getFormulas(Arena *arena)
{
    ConfigHandle ch = arena->cfg;
    const char *goalReward = cfg->GetStr(ch, "HS_Spi_Powerball", "GoalRewardFormula");
    const char *goalJackpot = cfg->GetStr(ch, "HS_Spi_Powerball", "GoalJackpotFormula");

    if (!goalReward || !*goalReward || !goalJackpot || !*goalJackpot)
    {
        lm->LogA(L_ERROR, "hs_spi_powerball", arena, "GoalRewardFormula and/or GoalJackpotFormula configs not found.");
        return false;
    }

    ArenaData *adata = getArenaData(arena);
    char errorBuffer[200];
	errorBuffer[0] = '\0';

    adata->formulas.goalRewardFormula = formula->ParseFormula(goalReward, errorBuffer, sizeof(errorBuffer));
    if (!adata->formulas.goalRewardFormula)
    {
        lm->LogA(L_ERROR, "hs_spi_powerball", arena, "Error parsing GoalRewardFormula: %s", errorBuffer);
        return false;
    }

    adata->formulas.goalJackpotFormula = formula->ParseFormula(goalJackpot, errorBuffer, sizeof(errorBuffer));
    if (!adata->formulas.goalJackpotFormula)
    {
        lm->LogA(L_ERROR, "hs_spi_powerball", arena, "Error parsing GoalJackpotFormula: %s", errorBuffer);

        //Free the formula that DID "work"
        formula->FreeFormula(adata->formulas.goalRewardFormula);
		adata->formulas.goalRewardFormula = NULL;

        return false;
    }

    return true;
}

local void freeFormulas(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);

    if (adata->formulas.goalRewardFormula)
    {
        formula->FreeFormula(adata->formulas.goalRewardFormula);
		adata->formulas.goalRewardFormula = NULL;
    }
    if (adata->formulas.goalJackpotFormula)
    {
        formula->FreeFormula(adata->formulas.goalJackpotFormula);
		adata->formulas.goalJackpotFormula = NULL;
    }
}

//Interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    balls = mm->GetInterface(I_BALLS, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    formula = mm->GetInterface(I_FORMULA, ALLARENAS);
    jackpot = mm->GetInterface(I_JACKPOT, ALLARENAS);
    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
    map = mm->GetInterface(I_MAPDATA, ALLARENAS);
    ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    prng = mm->GetInterface(I_PRNG, ALLARENAS);
}
local bool checkInterfaces()
{
    if (aman && balls && cfg && chat && cmd && formula && jackpot && lm && map
       && ml && database && pd && prng)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(balls);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(chat);
    mm->ReleaseInterface(cmd);
    mm->ReleaseInterface(formula);
    mm->ReleaseInterface(jackpot);
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(map);
    mm->ReleaseInterface(ml);
    mm->ReleaseInterface(database);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(prng);
}

EXPORT const char info_hs_spi_powerball[] = "hs_spi_powerball 1.0 by Spidernl\n";

EXPORT int MM_hs_spi_powerball(int action, Imodman *mm_, Arena *arena)
{
    if (action == MM_LOAD)
	{
		mm = mm_;

		getInterfaces();
		if (!checkInterfaces())
		{
            releaseInterfaces();
            return MM_FAIL;
        }

        adkey = aman->AllocateArenaData(sizeof(struct ArenaData));
        if (adkey == -1) //Out of memory
        {
            releaseInterfaces();
            return MM_FAIL;
        }

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        releaseInterfaces();
        aman->FreeArenaData(adkey);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        if (!readConfig(arena)) //Error during config reading/loading
        {
            return MM_FAIL;
        }

        mm->RegCallback(CB_GOAL, goalCB, arena);
		mm->RegCallback(CB_BALLFIRE, ballFireCB, arena);
        mm->RegCallback(CB_BALLPICKUP, ballPickupCB, arena);

		cmd->AddCommand("powerballreset", Cpowerballreset, arena, powerballreset_help);

		ml->SetTimer(secondTimer, 100, 100, arena, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ml->ClearTimer(secondTimer, arena);

        cmd->RemoveCommand("powerballreset", Cpowerballreset, arena);

        mm->UnregCallback(CB_BALLPICKUP, ballPickupCB, arena);
		mm->UnregCallback(CB_BALLFIRE, ballFireCB, arena);
        mm->UnregCallback(CB_GOAL, goalCB, arena);

		freeFormulas(arena);

        return MM_OK;
    }

	return MM_FAIL;
}
