//Hyperspace Race Module by D1st0rt

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "asss.h"

#define IDLE -1
#define READY 0
#define PRE_CHECKPOINT 1
#define POST_CHECKPOINT 2
#define PARTIAL_FINISH 3
#define POST_FINISH 4

#define OPEN 247
#define CLOSED 255

#define CHECKPOINT 0
#define FINISH 1

local Ichat *chat;
local Imodman *mm;
local Icmdman *cmd;
local Inet *net;
local Iplayerdata *pd;
local Igame *game;
local Iconfig *cfg;
local Imainloop *ml;
local Iarenaman *aman;

//Variables
local int pdkey;
local int adkey;

typedef struct race_pdata{
	short status;
	long checkPointTime;
	long finishTime;
	char *cptime;
	char *time;
	short place;
	char *best;
}race_pdata;

typedef struct race_adata{
	short status;
	short ticks;
	short curPlace;
	long startTime;
	LinkedList racers;
	LinkedList finished;
	ConfigHandle race_times;
}race_adata;

//Callbacks
local void pAction(Player *p, int action, Arena *arena);
local void posUpdate(Player *p, byte *data, int len);
local void settingsChanged(Arena *a, int action);
local int rawTimerStart(void *param);
local int tick(void *param);

//Functions
#define GetTime(p, n) cfg->GetInt(ad->race_times, p->name, n, -1)
#define SetTime(p, n, t) cfg->SetInt(ad->race_times, p->name, n, t,"",1)
#define SetDoors(n) cfg->SetInt(p->arena->cfg,"Door","DoorMode",n,"",1)

local void setup(Arena *a);
local float distance(int loc, int x, int y);
local int checkPos(int loc, int x, int y);
local int inside(int x, int y, int px, int py, int xtol, int ytol);
local char* timeString(long time);
local void checkPoint(Player *p, long time);
local void addFinish(Player *p, long time);
local void didNotFinish(Player *p);
local void endRace(Arena *a);
local void processFinish(Player *p);
local void cleanup(Arena *a);
local Player *getLeadPlayer(Arena *a);

//Commands
local void C_enter(const char *command, const char *params, Player *p, const Target *target);
local void C_start(const char *command, const char *params, Player *p, const Target *target);
local void C_close(const char *command, const char *params, Player *p, const Target *target);
local void C_new(const char *command, const char *params, Player *p, const Target *target);

local void pAction(Player *p, int action, Arena *arena)
{
	race_adata *ad = P_ARENA_DATA(arena, adkey);
	if(action == PA_LEAVEARENA || action == PA_DISCONNECT)
	{
		LLRemove(&ad->racers, p);
		LLRemove(&ad->finished, p);

		if(LLCount(&ad->racers) < 1)
			endRace(p->arena);
	}
}

local void setup(Arena *a)
{
	Player *p;
	Link *link;
	race_pdata *rpd;
	race_adata *ad = P_ARENA_DATA(a, adkey);

	LLInit(&ad->racers);
	LLInit(&ad->finished);
	ad->status = READY;
	ad->ticks = 0;
	ad->curPlace = 1;

	pd->Lock();
	FOR_EACH_PLAYER_P(p, rpd, pdkey)
	{
		rpd->status = IDLE;
	}
	pd->Unlock();
}

local void checkPoint(Player *p, long time)
{
	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	if(rpd->status == PRE_CHECKPOINT)
	{
		rpd->checkPointTime = time;
		rpd->cptime = timeString(time);
		chat->SendMessage(p, "Checkpoint! (%s)", rpd->cptime);

		if(ad->status == PRE_CHECKPOINT)
		{
			chat->SendArenaSoundMessage(p->arena, 2, "%s reaches the checkpoint first with a time of %s!", p->name, rpd->cptime);
			ad->status = POST_CHECKPOINT;
		}
		rpd->status = POST_CHECKPOINT;
	}
}

local void addFinish(Player *p, long time)
{
	long timetotal;
	long timebest;
	int timesraced;

	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	if(rpd->status == POST_CHECKPOINT)
	{
		LLRemove(&ad->racers, p);
		LLAdd(&ad->finished, p);

		rpd->finishTime = time;
		rpd->place = ad->curPlace;
		rpd->time = timeString(time);

		ad->curPlace++;

		timetotal = GetTime(p, "total");
		timesraced = GetTime(p, "raced");
		timebest = GetTime(p, "best");

		timetotal += time;
		timesraced ++;
		if(timebest < 0 || timebest > time)
			timebest = time;

		SetTime(p, "total", timetotal);
		SetTime(p, "raced", timesraced);
		SetTime(p, "best", timebest);

		rpd->best = timeString(timebest);
		rpd->status = POST_FINISH;
		ad->status = PARTIAL_FINISH;
	}
	else if(rpd->status == PRE_CHECKPOINT)
		LLRemove(&ad->racers, p);

	if(LLCount(&ad->racers) < 1)
		endRace(p->arena);
}

local void didNotFinish(Player *p)
{
	addFinish(p, 120000);
}

local void endRace(Arena *a)
{
	race_adata *ad = P_ARENA_DATA(a, adkey);

	if(LLCount(&ad->finished) < 1)
	{
		chat->SendArenaMessage(a, "There are no remaining racers. Race Terminated.");
		cleanup(a);
	}


	chat->SendArenaMessage(a, "Race Complete! Final Results:");
	chat->SendArenaMessage(a, "+-----+--------------------+--------------------+---------+---------+---------+");
	chat->SendArenaMessage(a, "|Place|Name                |Squad               |CheckP Tm|Finish Tm|Best Time|");
	chat->SendArenaMessage(a, "+-----+--------------------+--------------------+---------+---------+---------+");
	LLEnum(&ad->finished, (void *)processFinish);
	chat->SendArenaMessage(a, "+-----+--------------------+--------------------+---------+---------+---------+");
	cleanup(a);
}

local void processFinish(Player *p)
{
	char str[255];
	race_pdata *rpd = PPDATA(p, pdkey);

	sprintf(str, "|%2d.  |%-20s|%-20s|%8s |%8s |%8s |", rpd->place, p->name, p->squad, rpd->cptime, rpd->time, rpd->best);
	chat->SendArenaMessage(p->arena, str);
}

local void cleanup(Arena *a)
{
	race_adata *ad = P_ARENA_DATA(a, adkey);
	ad->status = IDLE;
	LLFree(&ad->racers);
	LLFree(&ad->finished);
}

local int tick(void *param)
{
	Player *p;
	Arena *a = (Arena *)param;
	race_adata *ad = P_ARENA_DATA(a, adkey);
	if(ad->status > READY)
	{
		ad->ticks++;
		p = getLeadPlayer(a);

		if(ad->status <= POST_CHECKPOINT)
			chat->SendArenaSoundMessage(a, 105, "%d Seconds elapsed. %s in the lead.", ad->ticks*15, p->name);

		if(ad->ticks == 8)
		{
			chat->SendArenaSoundMessage(a, 1, "Two minutes exceeded. All other racers cut off.");
			LLEnum(&ad->racers, (void *)didNotFinish);
			return FALSE;
		}
		return TRUE;
	}
	return FALSE;
}

local void posUpdate(Player *p, byte *data, int len)
{
	race_pdata *rpd;
	struct C2SPosition *pos = (struct C2SPosition *)data;
	rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	if(ad->status > READY && rpd->status == PRE_CHECKPOINT)
	{
		if(checkPos(CHECKPOINT, p->position.x >> 4, p->position.y >> 4))
		{
			long dT = current_millis() - ad->startTime;
			checkPoint(p, dT);
		}
	}
	else if(ad->status > READY && rpd->status == POST_CHECKPOINT)
	{
		if(checkPos(FINISH, p->position.x >> 4, p->position.y >> 4))
		{
			long dT = current_millis() - ad->startTime;
			addFinish(p, dT);
		}
	}
}

local void settingsChanged(Arena *a, int action)
{
	if(action == AA_CONFCHANGED)
		if(cfg->GetInt(a->cfg,"Door","DoorMode",255) == 247)
		{
			race_adata *ad = P_ARENA_DATA(a, adkey);
			ad->status = PRE_CHECKPOINT;
			ad->startTime = current_millis();
			chat->SendArenaSoundMessage(a,104,"GOGOGOGOGOOOOO!");
			ml->SetTimer(tick, 1500, 1500, (void *)a, NULL);
		}
}

local int rawTimerStart(void *param)
{
	Player *p;
	Link *link;
	Target t;
	race_pdata *rpd;
	race_adata *ad = P_ARENA_DATA((Arena *)param, adkey);

	t.type = T_PLAYER;
	ad->status = PRE_CHECKPOINT;
	ad->startTime = current_millis();
	chat->SendArenaSoundMessage((Arena *)param,104,"GOGOGOGOGOOOOO!");

	pd->Lock();
	FOR_EACH_PLAYER_P(p, rpd, pdkey)
	{
		if(rpd->status == READY)
		{
			rpd->status = PRE_CHECKPOINT;
			t.u.p = p;
			game->WarpTo(&t, 509, p->position.y >> 4);
		}
	}
	pd->Unlock();

	SetDoors(OPEN);
	ml->SetTimer(tick, 1500, 1500, param, NULL);
	return FALSE;
}

local Player *getLeadPlayer(Arena *a)
{
	float cdist, dist;

	Player *p, *c;
	Link *link;
	race_adata *ad = P_ARENA_DATA(a, adkey);
	int point = (ad->status == PRE_CHECKPOINT? CHECKPOINT : FINISH);
	cdist = -1.0;

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		dist = distance(point, p->position.x >> 4, p->position.y >> 4);
		if(cdist < 0 || dist < cdist)
		{
			cdist = dist;
			c = p;
		}
	}
	pd->Unlock();

	return c;
}

local float distance(int loc, int x, int y)
{
	switch(loc)
	{
		case CHECKPOINT:
			return sqrt(pow(506 - x, 2) + pow(674 - y, 2));
		break;
		case FINISH:
			return sqrt(pow(505 - x, 2) + pow(343 - y, 2));
		break;
	}
	return 0;
}

local int checkPos(int loc,int x, int y)
{
	switch(loc)
	{
		case CHECKPOINT:
			return (abs(x - 506) < 20) && y > 655;
		break;
		case FINISH:
			return (abs(x - 506) < 20) && y < 361;
		break;
	}
	return 0;
}

local int inside(int x, int y, int px, int py, int xtol, int ytol)
{
	int dx = abs(x-px);
	int dy = abs(y-py);
	return (dx <= xtol && dy <= ytol);
}

local char *timeString(long time)
{
	int minutes;
	int seconds;
	int millis;
	char *str;

	minutes = (time - (time % 60000)) / 60000;
	seconds = ((time - (minutes * 60000)) - ((time - (minutes * 60000)) % 1000)) / 1000;
	millis = (time - (minutes * 60000) - (seconds * 1000));

	str = malloc(sizeof(char [8]));
	sprintf(str, "%01d:%02d.%03d", minutes, seconds, millis);
	return str;
}


local helptext_t enter_help =
"Targets: none\n"
"Args: none\n"
"Puts you into the race.";
local void C_enter(const char *command, const char *params, Player *p, const Target *target)
{
	Target *t;
	race_pdata *rpd = PPDATA(p, pdkey);
	t->type = T_PLAYER;
	t->u.p = p;

	if(rpd->status == IDLE)
	{
		game->SetShipAndFreq(p, 0, 0);
		game->Lock(t,0,0,0);
		game->WarpTo(t,504,342);
        game->ShipReset(t);
		rpd->status = READY;
		chat->SendArenaMessage(p->arena, "%s has entered the race.", p->name);
	}
}

local helptext_t start_help =
"Targets: none\n"
"Args: none\n"
"Starts the race.";
local void C_start(const char *command, const char *params, Player *p, const Target *target)
{
	Link *link;
	race_pdata *rpd;
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	if(ad->status == READY)
	{
		chat->SendArenaMessage(p->arena, "On your marks, get set...");

		pd->Lock();
		FOR_EACH_PLAYER_P(p, rpd, pdkey)
		{
			if(rpd->status == READY)
				rpd->status = PRE_CHECKPOINT;
		}
		pd->Unlock();

		SetDoors(OPEN);
	}
}

local helptext_t close_help =
"Targets: none\n"
"Args: none\n"
"Closes the race doors.";
local void C_close(const char *command, const char *params, Player *p, const Target *target)
{
	SetDoors(CLOSED);
}

local helptext_t new_help =
"Targets: none\n"
"Args: none\n"
"Creates a new race.";
local void C_new(const char *command, const char *params, Player *p, const Target *target)
{
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	if(ad->status == IDLE)
	{
		setup(p->arena);
		chat->SendArenaMessage(p->arena, "A new race is beginning! Type ?race to join.");
		SetDoors(CLOSED);
	}
}

local void C_rstat(const char *command, const char *params, Player *p, const Target *target)
{
	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	chat->SendMessage(p, "Game state: %d", ad->status);
	chat->SendMessage(p, "Your state: %d", rpd->status);
}

EXPORT const char info_hs_race[] = "v1.0 D1st0rt";

EXPORT int MM_hs_race(int action, Imodman *mm_, Arena *arena)
{
	if(action == MM_LOAD)
	{
		mm = mm_;
		chat    = mm->GetInterface(I_CHAT,		 ALLARENAS);
		cmd	    = mm->GetInterface(I_CMDMAN,	 ALLARENAS);
		net     = mm->GetInterface(I_NET,		 ALLARENAS);
		pd      = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg     = mm->GetInterface(I_CONFIG,	 ALLARENAS);
		game    = mm->GetInterface(I_GAME,		 ALLARENAS);
		ml		= mm->GetInterface(I_MAINLOOP, 	 ALLARENAS);
		aman    = mm->GetInterface(I_ARENAMAN,   ALLARENAS);

		//Data
		pdkey = pd->AllocatePlayerData( sizeof(race_pdata) );
		if (pdkey == -1) return MM_FAIL;
		adkey = aman->AllocateArenaData(sizeof(struct race_adata));
		if (adkey == -1) return MM_FAIL;

		if (!chat || !cmd || !net || !pd || !ml || !cfg || !game || !aman)
			return MM_FAIL;

		return MM_OK;
	}
	else if(action == MM_ATTACH)
	{
		race_adata *ad = P_ARENA_DATA(arena, adkey);
		ad->race_times = cfg->OpenConfigFile("events", "race.conf", NULL, NULL);

		//Callbacks
		net->AddPacket(C2S_POSITION, posUpdate);
		mm->RegCallback(CB_ARENAACTION, settingsChanged, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, pAction, ALLARENAS);

		//Commands
		cmd->AddCommand("race", C_enter, arena, enter_help);
		cmd->AddCommand("startrace", C_start, arena,  start_help);
		cmd->AddCommand("closedoors", C_close, arena,  close_help);
		cmd->AddCommand("newrace", C_new, arena,  new_help);
		cmd->AddCommand("rstat", C_rstat, arena,  NULL);

		ad->status = IDLE;
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		race_adata *ad = P_ARENA_DATA(arena, adkey);
		cfg->CloseConfigFile(ad->race_times);

		//Callbacks
		net->RemovePacket(C2S_POSITION, posUpdate);
		mm->UnregCallback(CB_ARENAACTION, settingsChanged, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, pAction, ALLARENAS);

		//Commands
		cmd->RemoveCommand("race", C_enter, arena);
		cmd->RemoveCommand("startrace", C_start, arena);
		cmd->RemoveCommand("closedoors", C_close, arena);
		cmd->RemoveCommand("newrace", C_new, arena);
		cmd->RemoveCommand("rstat", C_rstat, arena);

		return MM_OK;
	}
	else if(action == MM_UNLOAD)
	{
		pd->FreePlayerData(pdkey);
		aman->FreeArenaData(adkey);

		//Interfaces
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);

		return MM_OK;
	}
	return MM_FAIL;
}
