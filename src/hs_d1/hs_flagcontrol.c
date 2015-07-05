/*
 * Flag Control
 * 08.02.10 D1st0rt
 */

#define MODULENAME hs_flagcontrol
#define SZMODULENAME "hs_flagcontrol"
#define INTERFACENAME Ihs_flagcontrol

#include "akd_asss.h"
#include "gamestats.h"
#include "fg_wz.h"
#include "hs_flagtime.h"
#include "hscore_teamnames.h"
#include "hscore_spawner.h"
#include "flagcore.h"

///////////////////////////////////////////////////////////////////////////////
// Defines
///////////////////////////////////////////////////////////////////////////////

#define MAXFLAGS 255
#define TIMEBUFFERSIZE 12

#define IDLE 0
#define PREGAME 1
#define ACTIVE 2
#define PAUSED 3
#define POSTGAME 4

#define LOSS 0
#define WIN_WARZONE 1
#define WIN_FLAGTIME 2

//game->Lock*
#define SILENT 0
#define NOTIFY 1
#define EVERYONE 0
#define ENTERSPEC 0
#define SPECCURRENT 1
#define TOSPEC 1
#define FOREVER 0

typedef struct FCTeam
{
	Arena *Arena;
	int Freq;
	const char *Name;
	FlagTeam *Team;
} FCTeam;

DEF_PARENA_TYPE

	Ihs_flagtime *ft;
	Iteamnames *tn;
	Igamestats *gs;
	Ihscorespawner *sp;

	int Status;
	int Elapsed;
	LinkedList *FCTeams;
	gamestat_type *stat_T;		//play time
	gamestat_type *stat_W;		//kills
	gamestat_type *stat_L;		//deaths
	gamestat_type *stat_FT;		//flag time
	gamestat_type *stat_FG;		//flag grabs
	gamestat_type *stat_FD;		//flag drops
	gamestat_type *stat_FN;		//flag neuts
	gamestat_type *stat_FS;		//flag steals
	gamestat_type *stat_A;		//attaches
	gamestat_type *stat_AT;		//attached to
	int cfg_SpecFreq;
	int cfg_FlagCount;
	int cfg_FlagX[MAXFLAGS];
	int cfg_FlagY[MAXFLAGS];
	int cfg_TimeLimit;

ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE

	int ExistingFT;
	int Status;

ENDDEF_PPLAYER_TYPE;

///////////////////////////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////////////////////////

local pthread_mutex_t globalmutex;

// Non-Standard Interfaces
local Istats *stats;
local Iflagcore *flagcore;

///////////////////////////////////////////////////////////////////////////////
// Prototypes
///////////////////////////////////////////////////////////////////////////////
local char *TimeString(int time, char *buffer);
local void PrepareGame(Arena *a);
local void StartGame(Arena *a);
local void EndGame(Arena *a);
local void EndGameStatsEnum(Arena *a, const char *name, LinkedList *statlist, int *param);
local void CleanupGame(Arena *a);
local FCTeam *GetTeam(Arena *a, int freq);
local void AddTeam(Arena *a, int freq);
local void RemoveTeam(Arena *a, int freq);
local void DeclareVictor(Arena *a, int freq, int how);
local FCTeam *GetFlagTimeLeadingTeam(Arena *a);
local Player *GetFlagTimeLeadingPlayer(Arena *a, int freq);
local int BreakdownEnum(const char *key, void *val, void *clos);

// Callbacks
local void FlagGain(Arena *a, Player *p, int fid, int how);
local void FlagLost(Arena *a, Player *p, int fid, int how);
local void WarzoneWin(Arena *a, int freq, int *points);
local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void PlayerAction(Player *p, int action, Arena *a);
local void PlayerKill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void PlayerAttach(Player *p, byte *pkt2, int len);

// Timers
local int Tick(void *a);

// Commands
local helptext_t showteams_help =
"Targets: none\n"
"Args: none\n"
"Shows teams participating in flag control game.";
local void C_showteams(const char *command, const char *params, Player *p, const Target *target);

local helptext_t addteam_help =
"Targets: freq\n"
"Args: none\n"
"Adds a team to the flag control game.";
local void C_addteam(const char *command, const char *params, Player *p, const Target *target);

local helptext_t removeteam_help =
"Targets: freq\n"
"Args: none\n"
"Removes a team from the flag control game.";
local void C_removeteam(const char *command, const char *params, Player *p, const Target *target);

local helptext_t newgame_help =
"Targets: none\n"
"Args: none\n"
"Sets up a new flag control game. Use ?addteam to add teams (naturally).";
local void C_newgame(const char *command, const char *params, Player *p, const Target *target);

local helptext_t startgame_help =
"Targets: none\n"
"Args: none\n"
"Once a flag control game has been created and teams have been added, begins play.";
local void C_startgame(const char *command, const char *params, Player *p, const Target *target);

local helptext_t gameover_help =
"Targets: none\n"
"Args: none\n"
"Ends a flag control game if there is one in progress, and displays post-game statistics.";
local void C_gameover(const char *command, const char *params, Player *p, const Target *target);

///////////////////////////////////////////////////////////////////////////////
// Entry Point
///////////////////////////////////////////////////////////////////////////////

EXPORT const char info_hs_flagcontrol[] = "v1.0 by D1st0rt <d1st0rter@gmail.com>";
EXPORT int MM_hs_flagcontrol(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		mm = mm_;

		GET_USUAL_INTERFACES();
		GETINT(stats, I_STATS);
		GETINT(flagcore, I_FLAGCORE);

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

Lfailload:
		RELEASEINT(stats);
		RELEASEINT(flagcore);
		RELEASE_USUAL_INTERFACES();

		DO_RETURN();
	}

	else if (action == MM_ATTACH)
	{
		ALLOC_ARENA_DATA(ad);
		MYGLOCK;
		ad->FCTeams = LLAlloc();
		MYGUNLOCK;

		//Attached interfaces
		GETARENAINT(ad->gs, I_GAMESTATS);
		GETARENAINT(ad->ft, I_HS_FLAGTIME);
		GETARENAINT(ad->tn, I_TEAMNAMES);
		GETARENAINT(ad->sp, I_HSCORE_SPAWNER);

		/* cfghelp: FlagControl:TimeLimit, arena, int, def: 30, mod: hs_flagcontrol
		The number of minutes a flag control game can last*/
		ad->cfg_TimeLimit = cfg->GetInt(arena->cfg, "FlagControl", "TimeLimit", 30);

		ad->cfg_FlagCount = cfg->GetInt(arena->cfg, "Flag", "FlagCount", 6);

		char buf[8];
		for(int i = 0; i < ad->cfg_FlagCount; i++)
		{
			sprintf(buf, "FlagX%d", i);
			ad->cfg_FlagX[i] = cfg->GetInt(arena->cfg, "FlagControl", buf, i);
			sprintf(buf, "FlagY%d", i);
			ad->cfg_FlagY[i] = cfg->GetInt(arena->cfg, "FlagControl", buf, i);
		}

		ad->cfg_SpecFreq = cfg->GetInt(arena->cfg, "Team", "SpectatorFrequency", 8025);

		//Gamestats
		ad->stat_T = ad->gs->getStatType(arena, "T");
		ad->stat_W = ad->gs->getStatType(arena, "W");
		ad->stat_L = ad->gs->getStatType(arena, "L");
		ad->stat_FT = ad->gs->getStatType(arena, "FT");
		ad->stat_FG = ad->gs->getStatType(arena, "FG");
		ad->stat_FD = ad->gs->getStatType(arena, "FD");
		ad->stat_FN = ad->gs->getStatType(arena, "FN");
		ad->stat_FS = ad->gs->getStatType(arena, "FS");
		ad->stat_A = ad->gs->getStatType(arena, "A");
		ad->stat_AT = ad->gs->getStatType(arena, "AT");

		//Callbacks
		mm->RegCallback(CB_FLAGGAIN, FlagGain, arena);
		mm->RegCallback(CB_FLAGLOST, FlagLost, arena);
		mm->RegCallback(CB_WARZONEWIN, WarzoneWin, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
		mm->RegCallback(CB_PLAYERACTION, PlayerAction, arena);
		mm->RegCallback(CB_KILL, PlayerKill, arena);
		net->AddPacket(C2S_ATTACHTO, PlayerAttach);

		//Timers

		//Commands
		cmd->AddCommand("showteams", C_showteams, arena, showteams_help);
		cmd->AddCommand("addteam", C_addteam, arena, addteam_help);
		cmd->AddCommand("removeteam", C_removeteam, arena, removeteam_help);
		cmd->AddCommand("newgame", C_newgame, arena, newgame_help);
		cmd->AddCommand("startgame", C_startgame, arena, startgame_help);
		cmd->AddCommand("gameover", C_gameover, arena, gameover_help);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		//Timers

		//Callbacks
		mm->UnregCallback(CB_FLAGGAIN, FlagGain, arena);
		mm->UnregCallback(CB_FLAGLOST, FlagLost, arena);
		mm->UnregCallback(CB_WARZONEWIN, WarzoneWin, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, arena);
		mm->UnregCallback(CB_KILL, PlayerKill, arena);
		net->RemovePacket(C2S_ATTACHTO, PlayerAttach);

		//Commands
		cmd->RemoveCommand("showteams", C_showteams, arena);
		cmd->RemoveCommand("addteam", C_addteam, arena);
		cmd->RemoveCommand("removeteam", C_removeteam, arena);
		cmd->RemoveCommand("newgame", C_newgame, arena);
		cmd->RemoveCommand("startgame", C_startgame, arena);
		cmd->RemoveCommand("gameover", C_gameover, arena);

Lfailattach:
		//Attached interfaces
		RELEASEINT(ad->gs);
		RELEASEINT(ad->ft);
		RELEASEINT(ad->tn);
		RELEASEINT(ad->sp);

		MYGLOCK;
		LLFree(ad->FCTeams);
		MYGUNLOCK;
		FREE_ARENA_DATA(ad);
		DO_RETURN();
	}

	return MM_FAIL;
}

///////////////////////////////////////////////////////////////////////////////
// Functions
///////////////////////////////////////////////////////////////////////////////

local char *TimeString(int seconds, char *buffer)
{
	int minutes = seconds / 60;
	int hours = minutes / 60;
	minutes = minutes % 60;
	seconds = seconds % 60;

	snprintf(buffer, TIMEBUFFERSIZE-1, "%02d:%02d:%02d", hours, minutes, seconds);
	buffer[TIMEBUFFERSIZE-1] = 0;

	return buffer;
}

local void PrepareGame(Arena *a)
{
	DEF_AD(a);

	Player *p;
	Link *link;

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		if(IS_IN(p,a))
		{
			BDEF_PD(p);
			pdat->Status = IDLE;
		}
	}
	PDUNLOCK;

	ad->Elapsed = 0;
	ad->Status = PREGAME;
	ad->ft->SetStatus(a, IDLE);
	ad->ft->Reset(a);
}

local void StartGame(Arena *a)
{
	DEF_AD(a);

	FlagInfo flags[ad->cfg_FlagCount];

	int n = flagcore->GetFlags(a, 0, flags, ad->cfg_FlagCount);
	for(int i = 0; i < n; i++)
	{
		flags[i].state = FI_ONMAP;
		flags[i].carrier = NULL;
		flags[i].freq = -1;
		flags[i].x = ad->cfg_FlagX[i];
		flags[i].y = ad->cfg_FlagY[i];
	}

	flagcore->SetFlags(a, 0, flags, n);

	DEF_T_A(a);
	game->GivePrize(&t, PRIZE_WARP, 1);
	game->ShipReset(&t);

	Player *p;
	Link *link;
	ticks_t ct = current_ticks();

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		if(IS_IN(p,a))
		{
			BDEF_PD(p);
			pdat->ExistingFT = 0;
			if(IS_PLAYING(p,a))
			{
				pdat->Status = ACTIVE;
				ad->gs->StartStatTicker(a, p, ad->stat_T, 0, 0, p->pkt.freq, ct);
				ad->sp->respawn(p);
			}
		}
	}
	PDUNLOCK;

	chat->SendArenaSoundMessage(a, SOUND_GOAL, "The game has started!");
	ad->Status = ACTIVE;
	ad->ft->SetStatus(a, 1);
	ml->SetTimer(Tick, 100, 100, a, a);
}

local void EndGame(Arena *a)
{
	DEF_T_A(a);
	DEF_AD(a);

	Link *link;
	Player *p;

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		if(IS_IN(p,a))
		{
			int seconds = ad->ft->GetFlagSeconds(p);
			if(seconds)
			{
				ad->gs->AddStat(a, p, ad->stat_FT, 0, 0, p->pkt.freq, seconds);
			}
		}
	}
	PDUNLOCK;

	ad->gs->StatsEnum(a, 0, 0, (statsenumfunc)EndGameStatsEnum, NULL);
	//game->LockArena(a, SILENT, EVERYONE, ENTERSPEC, CURRENTSPEC);
	ad->gs->SpamStatsTable(a, 0, 0, &t);
	//game->UnlockArena(a, NOTIFY, EVERYONE);
}

local void EndGameStatsEnum(Arena *a, const char *name, LinkedList *statlist, int *param)
{

}

local void CleanupGame(Arena *a)
{
	Link *link;
	FCTeam *team;
	Player *p;
	DEF_AD(a);

	MYGLOCK;
	FOR_EACH(ad->FCTeams, team, link)
	{
		LLRemove(ad->FCTeams, team);
		afree(team);
	}

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		if(IS_IN(p,a))
		{
			BDEF_PD(p);
			pdat->ExistingFT = 0;
		}
	}
	PDUNLOCK;

	MYGUNLOCK;

	ad->ft->Reset(a);
	ad->gs->ClearGame(a, 0);
	ad->Status = IDLE;
}

local FCTeam *GetTeam(Arena *a, int freq)
{
	FCTeam *team;
	FCTeam *ret = NULL;
	Link *link;
	DEF_AD(a);

	MYGLOCK;
	FOR_EACH(ad->FCTeams, team, link)
	{
		if(team->Freq == freq)
		{
			ret = team;
			break;
		}
	}
	MYGUNLOCK;

	return ret;
}

local void AddTeam(Arena *a, int freq)
{
	DEF_AD(a);

	FCTeam *team = amalloc(sizeof(FCTeam));
	team->Arena = a;
	team->Freq = freq;
	team->Name = ad->tn->getFreqTeamName(freq, a);
	team->Team = ad->ft->GetFlagTeam(a, freq);

	MYGLOCK;
	LLAdd(ad->FCTeams, team);
	MYGUNLOCK;
}

local void RemoveTeam(Arena *a, int freq)
{
	FCTeam *team = GetTeam(a, freq);
	DEF_AD(a);

	MYGLOCK;
	if(team)
	{
		LLRemove(ad->FCTeams, team);
	}
	MYGUNLOCK;

	afree(team);
}

local void DeclareVictor(Arena *a, int freq, int how)
{
	DEF_AD(a);
	if(ad->Status == ACTIVE)
	{
		FCTeam *team = GetTeam(a, freq);
		if(how == WIN_WARZONE)
		{
			chat->SendArenaSoundMessage(a, SOUND_HALLELLULA, "Team %s wins by warzone victory!", team->Name);
		}
		else if(how == WIN_FLAGTIME)
		{
			chat->SendArenaSoundMessage(a, SOUND_HALLELLULA, "Team %s wins by flag time!", team->Name);
		}
		else
		{
			chat->SendArenaSoundMessage(a, SOUND_INCONCEIVABLE, "Team %s wins by some unknown victory condition!", team->Name);
		}
		ad->Status = POSTGAME;
		ad->ft->SetStatus(a, IDLE);

		Link *link;
		Player *p;
		ticks_t ct = current_ticks();
		PDLOCK;
		FOR_EACH_PLAYER(p)
		{
			BDEF_PD(p);
			pdat->Status = IDLE;
			ad->gs->StopStatTicker(a, p, ad->stat_T, ct);
		}
		PDUNLOCK;
	}
}

local FCTeam *GetFlagTimeLeadingTeam(Arena *a)
{
	FCTeam *ret = NULL;
	DEF_AD(a);
	if(ad->Status == ACTIVE)
	{
		Link *link;
		FCTeam *team;
		MYGLOCK;
		FOR_EACH(ad->FCTeams, team, link)
		{
			if(ret == NULL || team->Team->FlagSeconds > ret->Team->FlagSeconds)
			{
				ret = team;
			}
		}
		MYGUNLOCK;
	}

	return ret;
}

local Player *GetFlagTimeLeadingPlayer(Arena *a, int freq)
{
	Player *ret = NULL;
	int time = 0;
	DEF_AD(a);
	if(ad->Status == ACTIVE)
	{
		if(freq == -1)
		{
			Link *link;
			Player *p;
			PDLOCK;
			FOR_EACH_PLAYER(p)
			{
				if(ad->ft->GetFlagSeconds(p) > time)
				{
					ret = p;
					time = ad->ft->GetFlagSeconds(p);
				}
			}
			PDUNLOCK;
		}
		else
		{
			FCTeam *team = GetTeam(a, freq);
			if(team)
			{
				Player *leader;
				MYGLOCK;
				HashEnum(team->Team->Breakdown, BreakdownEnum, leader);
				MYGUNLOCK;
				ret = leader;
			}
		}
	}

	return ret;
}

local int BreakdownEnum(const char *key, void *val, void *clos)
{
	int time = *(int *)val;

	DEF_AD(((Player*)clos)->arena);
	Player *p = pd->FindPlayer(key);

	if(time > ad->ft->GetFlagSeconds((Player*)clos))
	{
		clos = p;
	}
	return FALSE;
}

///////////////////////////////////////////////////////////////////////////////
// Callbacks
///////////////////////////////////////////////////////////////////////////////

local void FlagGain(Arena *a, Player *p, int fid, int how)
{
	DEF_AD(a);

	if(ad->Status == ACTIVE && how == FLAGGAIN_PICKUP)
	{
		ad->gs->AddStat(a, p, ad->stat_FG, 0, 0, p->pkt.freq, 1);
	}
}

local void FlagLost(Arena *a, Player *p, int fid, int how)
{
	DEF_AD(a);

	if(ad->Status == ACTIVE)
	{
		if(how == CLEANUP_DROPPED)
		{
			ad->gs->AddStat(a, p, ad->stat_FD, 0, 0, p->pkt.freq, 1);
		}
		else if(how == CLEANUP_SHIPCHANGE || how == CLEANUP_FREQCHANGE)
		{
			ad->gs->AddStat(a, p, ad->stat_FN, 0, 0, p->pkt.freq, 1);
		}
	}
}

local void WarzoneWin(Arena *a, int freq, int *points)
{
	DEF_AD(a);
	if(ad->Status == ACTIVE && *points > 0)
	{
		DeclareVictor(a, freq, WIN_WARZONE);
	}
}

//update ShipChange and FreqChange to account for ExistingFT

local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	DEF_AD(p->arena);
	BDEF_PD(p);

	if(ad->Status == ACTIVE)
	{
		if(!GetTeam(p->arena, newfreq) && newfreq != ad->cfg_SpecFreq)
		{
			chat->SendMessage(p, "You cannot join that team while a game is in progress.");
			DEF_T_P(p);
			game->Lock(&t, SILENT, TOSPEC, FOREVER);
			game->Unlock(&t, SILENT);
		}
		else if(!pdat->Status && newship != SHIP_SPEC)
		{
			ticks_t ct = current_ticks();
			ad->gs->StartStatTicker(p->arena, p, ad->stat_T, 0, 0, p->pkt.freq, ct);
			pdat->Status = ACTIVE;
		}
		else if(newship == SHIP_SPEC)
		{
			ticks_t ct = current_ticks();
			ad->gs->StopStatTicker(p->arena, p, ad->stat_T, ct);
			pdat->Status = IDLE;
		}
	}
}

local void PlayerAction(Player *p, int action, Arena *a)
{
	//set stats upon leave
	DEF_AD(p->arena);

	if(ad->Status == ACTIVE)
	{
		BDEF_PD(p);
		if(action == PA_ENTERARENA)
		{
			pdat->ExistingFT = ad->gs->getStat(a, p->name, ad->stat_FT, 0, 0, p->pkt.freq);
			if(IS_PLAYING(p,a))
			{
				ticks_t ct = current_ticks();
				pdat->Status = ACTIVE;
				ad->gs->StartStatTicker(a, p, ad->stat_T, 0, 0, p->pkt.freq, ct);
			}
		}
		else if(action == PA_LEAVEARENA)
		{
			int newFT = ad->ft->GetFlagSeconds(p) - pdat->ExistingFT;
			if(newFT > 0)
			{
				ticks_t ct = current_ticks();
				pdat->Status = IDLE;
				ad->gs->AddStat(a, p, ad->stat_FT, 0, 0, p->pkt.freq, newFT);
				ad->gs->StopStatTicker(a, p, ad->stat_T, ct);
			}
		}
	}
}

local void PlayerKill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	DEF_AD(a);

	if(ad->Status == ACTIVE)
	{
		ad->gs->AddStat(a, killer, ad->stat_W, 0, 0, killer->pkt.freq, 1);
		ad->gs->AddStat(a, killed, ad->stat_L, 0, 0, killed->pkt.freq, 1);

		if(flags > 0)
		{
			ad->gs->AddStat(a, killer, ad->stat_FS, 0, 0, killer->pkt.freq, flags);
		}
	}
}

local void PlayerAttach(Player *p, byte *pkt2, int len)
{
	DEF_AD(p->arena);

	if(ad && ad->Status == ACTIVE)
	{
		int pid2 = ((struct SimplePacket*)pkt2)->d1;
		Arena *arena = p->arena;
		Player *to = NULL;

		if (len == 3 &&
			p->status == S_PLAYING &&
			arena &&
			pid2 != -1)
		{
			to = pd->PidToPlayer(pid2);
			if (to &&
				to->status == S_PLAYING &&
				to != p &&
				p->arena == to->arena &&
				p->p_freq == to->p_freq)
			{
				ad->gs->AddStat(arena, p, ad->stat_A, 0, 0, p->pkt.freq, 1);
				ad->gs->AddStat(arena, to, ad->stat_AT, 0, 0, p->pkt.freq, 1);
			}
		}
		//The problem with this is being able to track summoner as well.
		//It would appear that net doesn't allow addpacketing on s2c packets
		//and the c2s handler is not a valid option. I will have to investigate
		//possibilities as to how this can be implemented.
	}
}

///////////////////////////////////////////////////////////////////////////////
// Timers
///////////////////////////////////////////////////////////////////////////////

local int Tick(void *data)
{
	Arena *a = (Arena *)data;
	DEF_AD(a);
	int repeat = TRUE;

	if(!ad)
	{
		repeat = FALSE;
	}
	else if(ad->Status == ACTIVE)
	{
		ad->Elapsed++;

		FCTeam *team = GetFlagTimeLeadingTeam(a);
		if(team)
		{
			int minutes = team->Team->FlagSeconds / 60;
			if(minutes >= ad->cfg_TimeLimit)
			{
				DeclareVictor(team->Arena, team->Freq, WIN_FLAGTIME);
			}
		}
	}
	else if(ad->Status == IDLE)
	{
		repeat = FALSE;
	}

	return repeat;
}

///////////////////////////////////////////////////////////////////////////////
// Commands
///////////////////////////////////////////////////////////////////////////////

local void C_showteams(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	if(ad->Status == IDLE)
	{
		chat->SendMessage(p, "No Flag Control game in progress.");
	}
	else
	{
		MYGLOCK;
		char buf[TIMEBUFFERSIZE];
		Link *link;
		FCTeam *team;
		FOR_EACH(ad->FCTeams, team, link)
		{
			TimeString(team->Team->FlagSeconds, buf);
			chat->SendMessage(p, "%-20s [%s] (%d flags dropped)", team->Name, buf, team->Team->DroppedFlags);
		}
		MYGUNLOCK;
	}
}

local void C_addteam(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	if(target->type == T_FREQ)
	{
		if(ad->Status == PREGAME)
		{
			if(GetTeam(target->u.freq.arena, target->u.freq.freq))
			{
				chat->SendMessage(p, "That team is already in the game.");
			}
			else
			{
				AddTeam(target->u.freq.arena, target->u.freq.freq);
				chat->SendMessage(p, "Team added to the game.");
				lm->LogA(L_DRIVEL, "flagcontrol", p->arena, "Freq %d added.", target->u.freq.freq);
			}
		}
		else
		{
			chat->SendMessage(p, "Can only add teams before a new game.");
		}
	}
}

local void C_removeteam(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	if(target->type == T_FREQ)
	{
		if(ad->Status == PREGAME)
		{
			if(GetTeam(target->u.freq.arena, target->u.freq.freq))
			{
				RemoveTeam(target->u.freq.arena, target->u.freq.freq);
				chat->SendMessage(p, "Team removed from the game.");
				lm->LogA(L_DRIVEL, "flagcontrol", p->arena, "Freq %d removed.", target->u.freq.freq);
			}
			else
			{

				chat->SendMessage(p, "That team is not currently in the game.");
			}
		}
		else
		{
			chat->SendMessage(p, "Can only remove teams before a game starts.");
		}
	}
}

local void C_newgame(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	if(ad->Status == IDLE)
	{
		PrepareGame(p->arena);
		chat->SendArenaSoundMessage(p->arena, SOUND_BEEP2, "A new Flag Control game is starting!");
	}
	else
	{
		chat->SendMessage(p, "Flag Control game already in progress.");
	}
}

local void C_startgame(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	if(ad->Status == PREGAME)
	{
		MYGLOCK;
		int teams = LLCount(ad->FCTeams);
		MYGUNLOCK;

		if(teams < 2)
		{
			chat->SendMessage(p, "There must be at least 2 teams to start a game.");
		}
		else
		{
			StartGame(p->arena);
		}
	}
	else if(ad->Status == ACTIVE || ad->Status == POSTGAME)
	{
		chat->SendMessage(p, "Flag Control game already in progress.");
	}
	else
	{
		chat->SendMessage(p, "There is no game to start.");
	}

}

local void C_gameover(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	if(ad->Status == PREGAME)
	{
		chat->SendMessage(p, "Flag Control game cancelled.");
		CleanupGame(p->arena);
	}
	else if(ad->Status == ACTIVE || ad->Status == POSTGAME)
	{
		chat->SendArenaMessage(p->arena, "Flag Control game ended by %s.", p->name);
		EndGame(p->arena);
		CleanupGame(p->arena);
	}
	else
	{
		chat->SendMessage(p, "There is no game in progress to stop");
	}
}
