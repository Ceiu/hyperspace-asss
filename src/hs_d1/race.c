//Hyperspace Race Module by D1st0rt

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "asss.h"
#include "clientset.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_mysql.h"

#define IDLE 0
#define READY 1
#define PRE_CHECKPOINT 2
#define POST_CHECKPOINT 3
#define PARTIAL_FINISH 4
#define POST_FINISH 5

#define OPEN 247
#define CLOSED 255

#define CHECKPOINT 0
#define FINISH 1

#define TIMEBUFFERSIZE 12

local Ichat *chat;
local Ilogman *lm;
local Imodman *mm;
local Icmdman *cmd;
local Inet *net;
local Iplayerdata *pd;
local Igame *game;
local Iconfig *cfg;
local Imainloop *ml;
local Iarenaman *aman;
local Iclientset *cs;
local Ihscoremysql *db;
local Ihscoredatabase *hsdb;

#define CREATE_RACE_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_races` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `hostId` int(10) unsigned NOT NULL default '0'," \
"  `date` datetime NOT NULL," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_RESULT_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_race_results` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `raceId` int(10) unsigned NOT NULL default '0'," \
"  `playerId` int(10) unsigned NOT NULL default '0'," \
"  `msCheckpoint` int(10) unsigned NOT NULL default '0'," \
"  `msFinish` int(10) unsigned NOT NULL default '0'," \
"  `place` smallint(4) unsigned NOT NULL default '0'," \
"  `dnc` tinyint(1) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

//Variables
local int pdkey;
local int adkey;
local override_key_t ok_DoorMode;

local pthread_mutex_t mymutex;

#define MYLOCK pthread_mutex_lock(&mymutex)
#define MYUNLOCK pthread_mutex_unlock(&mymutex)

typedef struct race_pdata
{
	short status;
	long checkPointTime;
	long finishTime;
	//char *cptime;
	//char *time;
	short place;
	//char *best;
	int bestTime;
	char cptime[TIMEBUFFERSIZE];
	char time[TIMEBUFFERSIZE];
	char best[TIMEBUFFERSIZE];
}race_pdata;

typedef struct race_adata
{
	int giveRewards;
	int saveResults;
	short status;
	short ticks;
	short curPlace;
	long startTime;
	int bestTime;
	char best[TIMEBUFFERSIZE];
	char recordHolder[20];
	LinkedList racers;
	LinkedList finished;
	int raceId;
	int basereward;
} race_adata;

//Callbacks
local void pAction(Player *p, int action, Arena *arena);
local void posUpdate(Player *p, byte *data, int len);
local void shipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local int tick(void *param);
local void dbcb_newrace(int status, db_res *res, void *clos);
local void dbcb_loadpdata(int status, db_res *res, void *clos);
local void dbcb_loadadata(int status, db_res *res, void *clos);

//Functions
//#define GetTime(p, n) cfg->GetInt(ad->race_times, p->name, n, -1)
//#define SetTime(p, n, t) cfg->SetInt(ad->race_times, p->name, n, t,"",0)
//#define SetDoors(n) cfg->SetInt(p->arena->cfg,"Door","DoorMode",n,"",0)
#define getHSid(p) hsdb->getPlayerWalletId(p)

local void init_db();
local void setup(Arena *a);
local int checkPos(int loc, int x, int y);
local char* timeString(long time, char *buffer);
local void checkPoint(Player *p, long time);
local void addFinish(Player *p, long time);
local void endRace(Arena *a);
local void processFinish(Player *p);
local void cleanup(Arena *a);
local void closeDoors(Arena *a);
local int raceBegin(void *param);
local void openDoors(Arena *a);
local void checkArenaRecord(Arena *arena, race_adata *ad);

//Commands
local void C_race(const char *command, const char *params, Player *p, const Target *target);
local void C_startrace(const char *command, const char *params, Player *p, const Target *target);
local void C_closedoors(const char *command, const char *params, Player *p, const Target *target);
local void C_newrace(const char *command, const char *params, Player *p, const Target *target);
local void C_racestats(const char *command, const char *params, Player *p, const Target *target);

local void init_db()
{
	db->Query(NULL, NULL, 0, CREATE_RACE_TABLE);
	db->Query(NULL, NULL, 0, CREATE_RESULT_TABLE);
}

local void pAction(Player *p, int action, Arena *arena)
{
	if(arena)
	{
		race_adata *ad = P_ARENA_DATA(arena, adkey);
		race_pdata *rpd = PPDATA(p, pdkey);

		if(action == PA_ENTERARENA)
		{
			rpd->status = IDLE;
			db->Query(dbcb_loadpdata, p, 0, "select msFinish, dnc from hs_race_results where playerId = # and dnc = 0 order by msFinish asc limit 1",
			getHSid(p));
		}

		if(ad->status > READY)
		{
			if(action == PA_LEAVEARENA)
			{
				MYLOCK;
				LLRemove(&ad->racers, p);
				LLRemove(&ad->finished, p);

				if(LLCount(&ad->racers) < 1)
					endRace(p->arena);
				MYUNLOCK;
			}
		}
	}
}

local void dbcb_loadadata(int status, db_res *res, void *clos)
{
	Arena *a = (Arena *)clos;
	race_adata *rad = P_ARENA_DATA(a, adkey);
	db_row *row;
	int results;

	if (status != 0 || res == NULL)
	{
		lm->LogA(L_ERROR, "race", a, "Unexpected database error during course best time load.");
		return;
	}

	results = db->GetRowCount(res);

	if (results > 1)
	{
		lm->LogA(L_ERROR, "race", a, "Multiple rows returned from MySQL: using first.");
	}

	if(results != 0)
	{
		row = db->GetRow(res);
		rad->bestTime = atoi(db->GetField(row, 0));
		timeString(rad->bestTime, rad->best);
		astrncpy(rad->recordHolder, db->GetField(row, 1), 20);
	}
}

local void dbcb_loadpdata(int status, db_res *res, void *clos)
{
	Player *p = (Player *)clos;
	race_pdata *rpd = PPDATA(p, pdkey);
	db_row *row;
	int results;

	if (status != 0 || res == NULL)
	{
		lm->LogP(L_ERROR, "race", p, "Unexpected database error during player best time load.");
		return;
	}

	results = db->GetRowCount(res);

	if (results > 1)
	{
		lm->LogP(L_ERROR, "race", p, "Multiple rows returned from MySQL: using first.");
	}

	if(results != 0)
	{
		row = db->GetRow(res);
		rpd->bestTime = atoi(db->GetField(row, 0));
		timeString(rpd->bestTime, rpd->best);
	}
}

local void setup(Arena *a)
{
	Player *p;
	Link *link;
	race_pdata *rpd;
	race_adata *ad = P_ARENA_DATA(a, adkey);

	MYLOCK;
	LLInit(&ad->racers);
	LLInit(&ad->finished);
	MYUNLOCK;

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
		//rpd->cptime = timeString(time);
		timeString(time, rpd->cptime);
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
	//long timetotal;
	//long timebest;
	//int timesraced;

	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	MYLOCK;
	if(rpd->status == POST_CHECKPOINT)
	{
		LLRemove(&ad->racers, p);
		LLAdd(&ad->finished, p);

		rpd->finishTime = time;
		rpd->place = ad->curPlace;
		//rpd->time = timeString(time);
		timeString(time, rpd->time);

		ad->curPlace++;

		chat->SendArenaMessage(p->arena, "%s finishes in %s.", p->name, rpd->time);

		/*timetotal = GetTime(p, "total");
		timesraced = GetTime(p, "raced");
		timebest = GetTime(p, "best");

		timetotal += time;
		timesraced ++;
		if(timebest < 0 || timebest > time)
			timebest = time;

		SetTime(p, "total", timetotal);
		SetTime(p, "raced", timesraced);
		SetTime(p, "best", timebest);

		rpd->best = timeString(timebest);*/
		rpd->status = POST_FINISH;
		ad->status = PARTIAL_FINISH;
	}
	else
	{
		LLRemove(&ad->racers, p);
	}

	if(LLCount(&ad->racers) < 1)
		endRace(p->arena);
	MYUNLOCK;
}

local void endRace(Arena *a)
{
	race_adata *ad = P_ARENA_DATA(a, adkey);
	Target t;
	t.type = T_ARENA;
	t.u.arena = a;
	ad->basereward = 0;

	MYLOCK;
	if(LLCount(&ad->finished) < 1)
	{
		chat->SendArenaMessage(a, "There are no remaining racers. Race Terminated.");
		cleanup(a);
		MYUNLOCK;
		return;
	}

	if(ad->giveRewards)
	{
		ad->basereward = 500 * LLCount(&ad->finished);
	}

	chat->SendArenaMessage(a, "Race Complete! Final Results:");
	chat->SendArenaMessage(a, "+-----+------+--------------------+--------------------+---------+---------+---------+");
	chat->SendArenaMessage(a, "|Place|Reward|Name                |Squad               |CheckP Tm|Finish Tm|Best Time|");
	chat->SendArenaMessage(a, "+-----+------+--------------------+--------------------+---------+---------+---------+");
	LLEnum(&ad->finished, (void *)processFinish);
	chat->SendArenaMessage(a, "+-----+------+--------------------+--------------------+---------+---------+---------+");
	if(ad->saveResults)
	{
		chat->SendArenaMessage(a, "Online results posted at http://www.sshyperspace.net/racing/?p=races&n=%d", ad->raceId);
		checkArenaRecord(a, ad);
	}
	cleanup(a);
	game->GivePrize(&t, PRIZE_WARP, 1);
	game->UnlockArena(a, 0, 0);
	MYUNLOCK;
}

local void processFinish(Player *p)
{
	char str[255];
	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	int reward = 0;

	if(ad->giveRewards)
	{
		if(rpd->place < 4)
			reward = ad->basereward / rpd->place;
		else
			reward = ad->basereward / (rpd->place + 1);

		hsdb->addMoney(p, MONEY_TYPE_EVENT, reward);
	}

	sprintf(str, "|%2d.  |$%-5d|%-20s|%-20s|%8s |%8s |%8s |", rpd->place, reward, p->name, p->squad, rpd->cptime, rpd->time, rpd->best);
	chat->SendArenaMessage(p->arena, "%s", str);

	if((rpd->finishTime < rpd->bestTime || rpd->bestTime == 0) && ad->saveResults)
	{
		if(rpd->bestTime != 0)
		{
			timeString(rpd->bestTime - rpd->finishTime, rpd->best);
			chat->SendMessage(p, "Congratulations! You beat your old best time by %s!", rpd->best);
		}
		rpd->bestTime = rpd->finishTime;
		timeString(rpd->bestTime, rpd->best);
	}

	if(ad->saveResults)
	{
		db->Query(NULL, NULL, 0, "insert into hs_race_results values (NULL, #, #, #, #, #, 0)",
		ad->raceId, getHSid(p), rpd->checkPointTime, rpd->finishTime, rpd->place);
	}
}

local void checkArenaRecord(Arena *arena, race_adata *ad)
{
	Player *p;
	race_pdata *pdata;
	int entrants = LLCount(&ad->finished);
	Link *link = LLGetHead(&ad->finished);
	int i;

	for(i = 1; i <= entrants; i++)
	{
		if(link)
		{
			p = (Player *)link->data;
			pdata = PPDATA(p, pdkey);
			if(pdata)
			{
				if(pdata->finishTime < ad->bestTime || ad->bestTime == 0)
				{
					if(ad->bestTime != 0)
					{
						timeString(ad->bestTime - pdata->finishTime, ad->best);
						if(ad->giveRewards)
						{
							chat->SendArenaMessage(arena, "%s wins an additional bonus of $%d for beating the old course record by %s!", p->name, 5000, ad->best);
							hsdb->addMoney(p, MONEY_TYPE_EVENT, 5000);
						}
						else
						{
							chat->SendArenaMessage(arena, "%s beat the old course record by %s!", p->name, ad->best);
						}
					}
					ad->bestTime = pdata->finishTime;
					timeString(ad->bestTime, ad->best);
					astrncpy(ad->recordHolder, p->name, 20);
				}
				link = link->next;
			}
		}
	}
}

local void cleanup(Arena *a)
{
	Player *p;
	Link *link;
	race_pdata *rpd;
	race_adata *ad = P_ARENA_DATA(a, adkey);
	Target t;
	t.type = T_PLAYER;

	ad->status = IDLE;
	MYLOCK;
	LLEmpty(&ad->racers);
	LLEmpty(&ad->finished);
	MYUNLOCK;

	pd->Lock();
	FOR_EACH_PLAYER_P(p, rpd, pdkey)
	{
		rpd->status = IDLE;
		t.u.p = p;
		game->Unlock(&t, 0);
	}
	pd->Unlock();
}

local int tick(void *param)
{
	Arena *a = (Arena *)param;
	race_adata *ad = P_ARENA_DATA(a, adkey);
	if(ad->status > READY)
	{
		ad->ticks++;

		if(ad->status <= POST_CHECKPOINT)
			chat->SendArenaSoundMessage(a, 105, "%d Seconds elapsed.", ad->ticks*15);

		if(ad->ticks == 8)
		{
			chat->SendArenaSoundMessage(a, 1, "Two minutes exceeded. All other racers cut off.");
			MYLOCK;
			LLEmpty(&ad->racers);
			MYUNLOCK;

			endRace(a);
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
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	rpd = PPDATA(p, pdkey);

	if (p->arena == NULL)
		return;

	if (ad->status <= READY)
		return;

	if (rpd->status == PRE_CHECKPOINT)
	{
		if(checkPos(CHECKPOINT, pos->x >> 4, pos->y >> 4))
		{
			long dT = current_millis() - ad->startTime;
			checkPoint(p, dT);
		}
	}
	else if (rpd->status == POST_CHECKPOINT)
	{
		if(checkPos(FINISH, pos->x >> 4, pos->y >> 4))
		{
			long dT = current_millis() - ad->startTime;
			addFinish(p, dT);
		}
	}
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

local char *timeString(long time, char *buffer)
{
	int minutes;
	int seconds;
	int millis;

	minutes = (time - (time % 60000)) / 60000;
	seconds = ((time - (minutes * 60000)) - ((time - (minutes * 60000)) % 1000)) / 1000;
	millis = (time - (minutes * 60000) - (seconds * 1000));

	//str = amalloc(sizeof(char [8]));
	snprintf(buffer, TIMEBUFFERSIZE-1, "%01d:%02d.%03d", minutes, seconds, millis);
	buffer[TIMEBUFFERSIZE-1] = 0;

	return buffer;
}

local void closeDoors(Arena *a)
{
	Player *p;
	Link *link;

	cs->ArenaOverride(a, ok_DoorMode, 255);

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		if(p->arena != a) continue;
		cs->SendClientSettings(p);
	}
	pd->Unlock();
}

local void openDoors(Arena *a)
{
	Player *p;
	Link *link;

	cs->ArenaUnoverride(a, ok_DoorMode);

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		if(p->arena != a) continue;
		cs->SendClientSettings(p);
	}
	pd->Unlock();
}

local int raceBegin(void * param)
{
	Arena *a = (Arena *)param;
	race_adata *ad = P_ARENA_DATA(a, adkey);
	openDoors(a);
	ad->startTime = current_millis();
	chat->SendArenaSoundMessage(a,104,"GOGOGOGOGOOOOO!");
	ml->SetTimer(tick, 1500, 1500, param, param);
	return FALSE;
}

local void shipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	race_pdata *rpd = PPDATA(p, pdkey);

	if(ad->status > IDLE)
	{
		if(newship == SHIP_SPEC)
		{
			MYLOCK;
			LLRemove(&ad->racers, p);
			rpd->status = IDLE;

			if (ad->status > READY)
			{
				if(LLCount(&ad->racers) < 1)
					endRace(p->arena);
			}

			MYUNLOCK;
		}
	}
}

local helptext_t enter_help =
"Targets: none\n"
"Args: none\n"
"Puts you into the race.";
local void C_race(const char *command, const char *params, Player *p, const Target *target)
{
	Target t;
	race_adata *rad = P_ARENA_DATA(p->arena, adkey);
	race_pdata *rpd = PPDATA(p, pdkey);
	t.type = T_PLAYER;
	t.u.p = p;

	if(rad->status == READY && rpd->status <= READY)
	{
		game->SetShipAndFreq(p, 0, 0);
		game->Lock(&t,0,0,0);
		game->WarpTo(&t,504,342);
        game->ShipReset(&t);
        if(rpd->status == IDLE)
        {
			MYLOCK;
			LLAdd(&rad->racers, p);
			MYUNLOCK;
			rpd->status = READY;
			chat->SendArenaMessage(p->arena, "%s has entered the race.", p->name);
		}
	}
}

local helptext_t start_help =
"Targets: none\n"
"Args: none\n"
"Starts the race.";
local void C_startrace(const char *command, const char *params, Player *p, const Target *target)
{
	Player *pl;
	Link *link;
	race_pdata *rpd;
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (!p->arena) return;

	MYLOCK;
	if(ad->status == READY)
	{
		if(LLCount(&ad->racers) > 0)
		{
			chat->SendArenaMessage(p->arena, "On your marks, get set...");

			pd->Lock();
			FOR_EACH_PLAYER_P(pl, rpd, pdkey)
			{
				if(rpd->status == READY)
					rpd->status = PRE_CHECKPOINT;
			}
			pd->Unlock();

			//SetDoors(OPEN);
			ml->SetTimer(raceBegin, 300, 300, (void *)p->arena, (void *)p->arena);
			ad->status = PRE_CHECKPOINT;
		}
		else
		{
			chat->SendMessage(p, "The race cannot start without players.");
		}
	}
	MYUNLOCK;
}

local helptext_t close_help =
"Targets: none\n"
"Args: none\n"
"Closes the race doors.";
local void C_closedoors(const char *command, const char *params, Player *p, const Target *target)
{
	closeDoors(p->arena);
}

local helptext_t new_help =
"Targets: none\n"
"Args: [-noreward] [-nodb]\n"
"Creates a new race. Will not give money rewards if -noreward is specified\n"
"and will not save results to the database if -nodb is specified.";
local void C_newrace(const char *command, const char *params, Player *p, const Target *target)
{
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	if(ad->status == IDLE)
	{
		setup(p->arena);
		if(strstr(params, "-noreward"))
			ad->giveRewards = FALSE;
		else
			ad->giveRewards = TRUE;

		if(strstr(params, "-nodb"))
			ad->saveResults = FALSE;
		else
			ad->saveResults = TRUE;

		game->LockArena(p->arena, 0, 0, 0, 1);
		chat->SendArenaMessage(p->arena, "A new race is beginning! Type ?race to join.");

		if(ad->saveResults)
		{
			db->Query(dbcb_newrace, ad, 0, "insert into hs_races values (NULL, #, now())", getHSid(p));
		}

		closeDoors(p->arena);
		//SetDoors(CLOSED);
	}
}

local helptext_t practice_help =
"Targets: none\n"
"Args: none\n"
"Creates a new practice race. Will not give money rewards or save times.";
local void C_raceprac(const char *command, const char *params, Player *p, const Target *target)
{
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);
	if(ad->status == IDLE)
	{
		setup(p->arena);
		ad->giveRewards = FALSE;
		ad->saveResults = FALSE;

		game->LockArena(p->arena, 0, 0, 0, 1);
		chat->SendArenaMessage(p->arena, "A new race is beginning! Type ?race to join.");

		closeDoors(p->arena);
	}
}

local helptext_t stats_help =
"Targets: none\n"
"Args: none\n"
"Displays your best time as well as the course record time.";
local void C_racestats(const char *command, const char *params, Player *p, const Target *target)
{
	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *rad = P_ARENA_DATA(p->arena, adkey);

	chat->SendMessage(p, "Your best time is %s.", rpd->best);
	chat->SendMessage(p, "Course record is %s (held by %s).", rad->best, rad->recordHolder);
}

local void dbcb_newrace(int status, db_res *res, void *clos)
{
	race_adata *ad = (race_adata *)clos;
	ad->raceId = db->GetLastInsertId();
}

local void C_rstat(const char *command, const char *params, Player *p, const Target *target)
{
	race_pdata *rpd = PPDATA(p, pdkey);
	race_adata *ad = P_ARENA_DATA(p->arena, adkey);

	chat->SendMessage(p, "Game state: %d", ad->status);
	chat->SendMessage(p, "Your state: %d", rpd->status);
}

EXPORT int MM_race(int action, Imodman *mm_, Arena *arena)
{
	if(action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		chat    = mm->GetInterface(I_CHAT,		 ALLARENAS);
		cmd	    = mm->GetInterface(I_CMDMAN,	 ALLARENAS);
		net     = mm->GetInterface(I_NET,		 ALLARENAS);
		pd      = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg     = mm->GetInterface(I_CONFIG,	 ALLARENAS);
		game    = mm->GetInterface(I_GAME,		 ALLARENAS);
		ml		= mm->GetInterface(I_MAINLOOP, 	 ALLARENAS);
		aman    = mm->GetInterface(I_ARENAMAN,   ALLARENAS);
		cs		= mm->GetInterface(I_CLIENTSET,  ALLARENAS);
		db		= mm->GetInterface(I_HSCORE_MYSQL, 	 ALLARENAS);
		lm 		= mm->GetInterface(I_LOGMAN, 	 ALLARENAS);
		hsdb 	= mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!chat || !cmd || !net || !pd || !ml || !cfg || !game || !aman ||
			!cs || !db || !lm || !hsdb)
			return MM_FAIL;

		//Make sure database tables exist
		init_db();

		//Data
		pdkey = pd->AllocatePlayerData( sizeof(race_pdata) );
		if (pdkey == -1) return MM_FAIL;
		adkey = aman->AllocateArenaData(sizeof(struct race_adata));
		if (adkey == -1) return MM_FAIL;
		ok_DoorMode = cs->GetOverrideKey("door", "doormode");

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&mymutex, &attr);
		pthread_mutexattr_destroy(&attr);

		//Callbacks
		net->AddPacket(C2S_POSITION, posUpdate);

		return MM_OK;
	}
	else if(action == MM_ATTACH)
	{
		race_adata *ad = P_ARENA_DATA(arena, adkey);

		MYLOCK;
		LLInit(&ad->racers);
		LLInit(&ad->finished);
		MYUNLOCK;

		cleanup(arena);
		db->Query(dbcb_loadadata, arena, 0, "SELECT res.msFinish, p.name, res.dnc FROM hs_race_results AS res, hs_players AS p WHERE res.playerId = p.id and res.dnc = 0 order by res.msFinish asc limit 1");
		//ad->race_times = cfg->OpenConfigFile("events", "race.conf", NULL, NULL);

		//Callbacks
		mm->RegCallback(CB_PLAYERACTION, pAction, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChange, arena);

		//Commands
		cmd->AddCommand("race", C_race, arena, enter_help);
		cmd->AddCommand("startrace", C_startrace, arena,  start_help);
		cmd->AddCommand("closedoors", C_closedoors, arena,  close_help);
		cmd->AddCommand("newrace", C_newrace, arena,  new_help);
		cmd->AddCommand("rstat", C_rstat, arena,  NULL);
		cmd->AddCommand("racestats", C_racestats, arena,  stats_help);
		cmd->AddCommand("raceprac", C_raceprac, arena,  practice_help);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		//race_adata *ad = P_ARENA_DATA(arena, adkey);
		//cfg->CloseConfigFile(ad->race_times);
		cleanup(arena);
		openDoors(arena);

		//Timers
		ml->ClearTimer(tick, arena);
		ml->ClearTimer(raceBegin, arena);

		//Callbacks
		mm->UnregCallback(CB_PLAYERACTION, pAction, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChange, arena);

		//Commands
		cmd->RemoveCommand("race", C_race, arena);
		cmd->RemoveCommand("startrace", C_startrace, arena);
		cmd->RemoveCommand("closedoors", C_closedoors, arena);
		cmd->RemoveCommand("newrace", C_newrace, arena);
		cmd->RemoveCommand("rstat", C_rstat, arena);
		cmd->RemoveCommand("racestats", C_racestats, arena);
		cmd->RemoveCommand("raceprac", C_raceprac, arena);

		return MM_OK;
	}
	else if(action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, posUpdate);

		pd->FreePlayerData(pdkey);
		aman->FreeArenaData(adkey);

		pthread_mutex_destroy(&mymutex);

		//Timers
		ml->ClearTimer(tick, NULL);
		ml->ClearTimer(raceBegin, NULL);

		//Interfaces
		mm->ReleaseInterface(db);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cs);
		mm->ReleaseInterface(hsdb);
		mm->ReleaseInterface(lm);

		return MM_OK;
	}
	return MM_FAIL;
}
