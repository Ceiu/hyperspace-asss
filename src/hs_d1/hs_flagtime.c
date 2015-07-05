/*
 * Flag Time
 * 08.01.05 D1st0rt
 */

#define MODULENAME hs_flagtime
#define SZMODULENAME "hs_flagtime"
#define INTERFACENAME Ihs_flagtime

#include "akd_asss.h"
#include "hs_flagtime.h"
#include "fg_wz.h"
#include "string.h"

// Defines
#define TIMEBUFFERSIZE 12
#define MAXFLAGS 255
#define PAUSED 0
#define ACTIVE 1

// Globals
local pthread_mutex_t globalmutex;

DEF_PARENA_TYPE
	LinkedList *FlagTeams;
	FlagOwner *FlagOwners[MAXFLAGS];
	int SpecFreq;
	int Status;
ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
	int DroppedFlags;
	int FlagSeconds;
	char FlagTime[TIMEBUFFERSIZE];
ENDDEF_PPLAYER_TYPE;

// Prototypes
local char *timeString(int time, char *buffer);
local FlagTeam *GetFlagTeam(Arena *a, int freq);
local FlagTeam *CreateFlagTeam(Arena *a, int freq);
local FlagOwner *GetFlagOwner(Arena *a, int flagID);
local int GetFlagSeconds(Player *p);
local void LoadFlagData(Player *p, int freq);
local void ClearFlagTeam(FlagTeam *team);
local void ClearFlagOwners(Arena *arena);
local int GetStatus(Arena *arena);
local void SetStatus(Arena *arena, int status);
local void Reset(Arena *arena);
local void DumpScores(FlagTeam *team);
local int DumpScore(const char *key, void *val, void *clos);

// Callbacks
local void FlagGain(Arena *a, Player *p, int fid, int how);
local void FlagLost(Arena *a, Player *p, int fid, int how);
local void WarzoneWin(Arena *a, int freq, int *points);
local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void PlayerAction(Player *p, int action, Arena *a);

// Timers
local int UpdateScores(void *a);

// Commands
local helptext_t flagtime_help =
"Targets: none\n"
"Args: none\n"
"Displays a breakdown of the current flag time scores.";
local void C_flagtime(const char *command, const char *params, Player *p, const Target *target);

MYINTERFACE =
{
	INTERFACE_HEAD_INIT(I_HS_FLAGTIME, "hs_flagtime")
	GetFlagTeam,
	GetFlagOwner,
	GetFlagSeconds,
	GetStatus,
	SetStatus,
	Reset
};

DEF_GLOBALINTERFACE;

EXPORT const char info_hs_flagtime[] = "08.01.22 by D1st0rt <d1st0rter@gmail.com>";
EXPORT int MM_hs_flagtime(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		mm = mm_;

		GET_USUAL_INTERFACES();

		REG_PARENA_DATA();
		BREG_PPLAYER_DATA();

		INIT_MUTEX(globalmutex);
		INIT_GLOBALINTERFACE();
		REG_GLOBALINTERFACE();

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		UNREG_GLOBALINTERFACE();
		DESTROY_MUTEX(globalmutex);

		UNREG_PARENA_DATA();
		UNREG_PPLAYER_DATA();

Lfailload:
		RELEASE_USUAL_INTERFACES();

		DO_RETURN();
	}

	else if (action == MM_ATTACH)
	{
		ALLOC_ARENA_DATA(ad);
		ad->FlagTeams = LLAlloc();
		ad->SpecFreq = cfg->GetInt(arena->cfg, "Team", "SpectatorFrequency", 8025);

		//Attached interfaces

		//Callbacks
		mm->RegCallback(CB_FLAGGAIN, FlagGain, arena);
		mm->RegCallback(CB_FLAGLOST, FlagLost, arena);
		mm->RegCallback(CB_WARZONEWIN, WarzoneWin, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
		mm->RegCallback(CB_PLAYERACTION, PlayerAction, arena);

		//Timers
		ml->SetTimer(UpdateScores, 100, 100, arena, arena);

		//Commands
		cmd->AddCommand("flagtime", C_flagtime, arena, flagtime_help);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		//Timers
		ml->ClearTimer(UpdateScores, arena);

		Reset(arena);
		DEF_AD(arena);
		LLFree(ad->FlagTeams);

		//Callbacks
		mm->UnregCallback(CB_FLAGGAIN, FlagGain, arena);
		mm->UnregCallback(CB_FLAGLOST, FlagLost, arena);
		mm->UnregCallback(CB_WARZONEWIN, WarzoneWin, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, arena);

		//Commands
		cmd->RemoveCommand("flagtime", C_flagtime, arena);

Lfailattach:
		//Attached interfaces

		FREE_ARENA_DATA(ad);
		DO_RETURN();
	}

	return MM_FAIL;
}

local char *timeString(int seconds, char *buffer)
{
	int minutes = seconds / 60;
	int hours = minutes / 60;
	minutes = minutes % 60;
	seconds = seconds % 60;

	snprintf(buffer, TIMEBUFFERSIZE-1, "%02d:%02d:%02d", hours, minutes, seconds);
	buffer[TIMEBUFFERSIZE-1] = 0;

	return buffer;
}

local FlagTeam *CreateFlagTeam(Arena *a, int freq)
{
	DEF_AD(a);

	FlagTeam *team = amalloc(sizeof(FlagTeam));
	team->Arena = a;
	team->Freq = freq;
	team->DroppedFlags = 0;
	team->FlagSeconds = 0;
	team->Breakdown = HashAlloc();

	MYGLOCK;
	LLAdd(ad->FlagTeams, team);
	MYGUNLOCK;

	return team;
}

local FlagTeam *GetFlagTeam(Arena *a, int freq)
{
	DEF_AD(a);

	MYGLOCK;
	FlagTeam *team = NULL;
	Link *link = LLGetHead(ad->FlagTeams);
	int freqs = LLCount(ad->FlagTeams);
	for(int i = 0; i < freqs; i++)
	{
		if(link)
		{
			team = (FlagTeam*)link->data;
			if(team->Freq == freq)
			{
				break;
			}
			else
			{
				team = NULL;
			}
			link = link->next;
		}
	}
	MYGUNLOCK;

	if(!team)
	{
		team = CreateFlagTeam(a, freq);
	}


	return team;
}

local FlagOwner *GetFlagOwner(Arena *a, int flagID)
{
	DEF_AD(a);
	FlagOwner *owner = NULL;
	if(flagID >= 0 && flagID < MAXFLAGS)
	{
		owner = ad->FlagOwners[flagID];
	}
	return owner;
}

local int GetFlagSeconds(Player *p)
{
	int seconds = 0;
	if(p)
	{
		BDEF_PD(p);
		seconds = pdat->FlagSeconds;
	}
	return seconds;
}

local void LoadFlagData(Player *p, int freq)
{
	DEF_AD(p->arena);
	BDEF_PD(p);

	pdat->DroppedFlags = 0;
	pdat->FlagSeconds = 0;
	FlagTeam *team = GetFlagTeam(p->arena, freq);
	MYGLOCK;
	if(team)
	{
		int *seconds = (int*)HashGetOne(team->Breakdown, p->name);
		if(seconds)
		{
			pdat->FlagSeconds = (int)seconds;
		}
	}

	for(int i = 0; i < MAXFLAGS; i++)
	{
		FlagOwner *owner = ad->FlagOwners[i];
		if(owner)
		{
			if(!strcmp(owner->Name, p->name))
			{
				pdat->DroppedFlags++;
			}
		}
	}
	MYGUNLOCK;
}

local void ClearFlagTeam(FlagTeam *team)
{
	MYGLOCK;
	HashFree(team->Breakdown);
	afree(team);
	MYGUNLOCK;
}

local void ClearFlagOwners(Arena *arena)
{
	DEF_AD(arena);

	MYGLOCK;
	for(int i = 0; i < MAXFLAGS; i++)
	{
		FlagOwner *owner = ad->FlagOwners[i];
		ad->FlagOwners[i] = NULL;
		afree(owner);
	}
	MYGUNLOCK;
}

local int GetStatus(Arena *arena)
{
	int status = 0;
	DEF_AD(arena);
	if(ad)
	{
		status = ad->Status;
	}
	return status;
}

local void SetStatus(Arena *arena, int status)
{
	DEF_AD(arena);
	ad->Status = status;
}

local void Reset(Arena *arena)
{
	DEF_AD(arena);
	Player *p;
	Link *link;

	ClearFlagOwners(arena);
	MYGLOCK;
	LLEnum(ad->FlagTeams, (void *)ClearFlagTeam);
	LLEmpty(ad->FlagTeams);
	MYGUNLOCK;

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		BDEF_PD(p);
		pdat->DroppedFlags = 0;
		pdat->FlagSeconds = 0;
	}
	PDUNLOCK;
}

local void DumpScores(FlagTeam *team)
{
	char time[TIMEBUFFERSIZE];
	chat->SendArenaMessage(team->Arena, "Freq %d : %s", team->Freq, timeString(team->FlagSeconds, time));
	MYGLOCK;
	HashEnum(team->Breakdown, DumpScore, team->Arena);
	MYGUNLOCK;
	chat->SendArenaMessage(team->Arena, "------------------------------");
}

local int DumpScore(const char *key, void *val, void *clos)
{
	char time[TIMEBUFFERSIZE];
	int *seconds = (int *)val;
	chat->SendArenaMessage((Arena *)clos, "%s : %s", key, timeString(*seconds, time));
	return FALSE;
}

local void FlagGain(Arena *a, Player *p, int fid, int how)
{
	DEF_AD(a);
	BDEF_PD(p);

	if(how == FLAGGAIN_PICKUP)
	{
		FlagOwner *owner = ad->FlagOwners[fid];
		if(owner)
		{
			FlagTeam *team = GetFlagTeam(a, owner->Freq);
			team->DroppedFlags--;

			Player *ownerp = pd->FindPlayer(owner->Name);
			if(ownerp)
			{
				pdat = BPD(ownerp);
				if(pdat->DroppedFlags > 0) //could be left over from when they were on another team
				{
					pdat->DroppedFlags--;
				}
			}

			MYGLOCK;
			ad->FlagOwners[fid] = NULL;
			afree(owner);
			MYGUNLOCK;
		}
	}
}

local void FlagLost(Arena *a, Player *p, int fid, int how)
{
	DEF_AD(a);
	BDEF_PD(p);

	if(how == CLEANUP_DROPPED)
	{
		FlagOwner *owner = amalloc(sizeof(FlagOwner));
		owner->Arena = a;
		astrncpy(owner->Name, p->name, sizeof(p->name));
		owner->Freq = p->pkt.freq;
		owner->FlagID = fid;
		ad->FlagOwners[fid] = owner;

		pdat->DroppedFlags++;
		FlagTeam *team = GetFlagTeam(a, p->pkt.freq);
		team->DroppedFlags++;
	}
}

local void WarzoneWin(Arena *a, int freq, int *points)
{
	ClearFlagOwners(a);
}

local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	DEF_AD(p->arena);
	if(oldfreq != newfreq && newfreq != ad->SpecFreq)
	{
		LoadFlagData(p, newfreq);
	}
}

local void PlayerAction(Player *p, int action, Arena *a)
{
	DEF_AD(a);
	if(action == PA_ENTERARENA)
	{
		if(p->pkt.freq != ad->SpecFreq)
		{
			LoadFlagData(p, p->pkt.freq);
		}
	}
}

local int UpdateScores(void *a)
{
	DEF_AD((Arena *)a);
	myPDType *pdata;

	for(int i = 0; i < MAXFLAGS; i++)
	{
		FlagOwner *owner = ad->FlagOwners[i];
		if(owner)
		{
			//lm->LogA(L_ERROR, "hs_flagtime", a, "owner of flag %d is %s", i, owner->Name);
			Player *p = pd->FindPlayer(owner->Name);
			if(p && p->pkt.freq == owner->Freq)
			{
				pdata = BPD(p);
				pdata->FlagSeconds++;
			}

			FlagTeam *team = GetFlagTeam(a, owner->Freq);
			team->FlagSeconds++;

			MYGLOCK;
			int *seconds = HashGetOne(team->Breakdown, owner->Name);
			if(seconds)
			{
				*seconds = *seconds + 1;
				HashReplace(team->Breakdown, owner->Name, seconds);
				//lm->LogA(L_ERROR, "hs_flagtime", a, "%s flag seconds: %d", owner->Name, *seconds);
			}
			else
			{
				seconds = amalloc(sizeof(*seconds));
				*seconds = 1;
				HashReplace(team->Breakdown, owner->Name, seconds);
				//lm->LogA(L_ERROR, "hs_flagtime", a, "%s new flag seconds: %d", owner->Name, *seconds);
			}
			MYGUNLOCK;

		}
	}
	return TRUE;
}

local void C_flagtime(const char *command, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);
	MYGLOCK;
	LLEnum(ad->FlagTeams, (void *)DumpScores);
	MYGUNLOCK;
}
