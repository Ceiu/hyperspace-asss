#include "asss.h"

#define MODULENAME antisafelame
#define SZMODULENAME "antisafelame"
#define INTERFACENAME Iantisafelame

#define NOT_USING_SHIP_NAMES 1
#include "akd_asss.h"
#include <string.h>

local Istats *stats;

DEF_PARENA_TYPE
	unsigned short minBounty;
	int interval;
ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
	int leftSafe;
	int safeTime;
	int deathTimeout;
	int attemptingFix;
ENDDEF_PPLAYER_TYPE;

local void setBounty(Player *, int);

local void playeraction(Player *, int action, Arena *);
local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void shipfreqchange(Player *p, int, int, int, int);

local int perSecond(void *a);

EXPORT const char info_antisafelame[] = "v1.0 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_antisafelame(int action, Imodman *mm_, Arena *arena)
{
	BMM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		mm = mm_;

		GET_USUAL_INTERFACES();
		GETINT(stats, I_STATS);

		BREG_PARENA_DATA();
		BREG_PPLAYER_DATA();

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		ml->ClearTimer(perSecond, 0);

		UNREG_PARENA_DATA();
		UNREG_PPLAYER_DATA();

Lfailload:
		RELEASEINT(stats);
		RELEASE_USUAL_INTERFACES();

		DO_RETURN();
	}


	else if (action == MM_ATTACH)
	{
		/* cfghelp: antisafelame:MinBounty, arena, int, def: 10, mod: antisafelame
		 * The amount of bounty where no more bounty is removed from people in safe. */
		ad->minBounty = cfg->GetInt(arena->cfg, "antisafelame", "minbounty", 10);

		/* cfghelp: antisafelame:SubtractInterval, arena, int, def: 2, mod: antisafelame
		 * How often in seconds to remove one bounty from people in safe zones. */
		ad->interval = cfg->GetInt(arena->cfg, "antisafelame", "subtractinterval", 2);

		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_PLAYERACTION, playeraction, arena);
		mm->RegCallback(CB_KILL, playerkill, arena);

		ml->SetTimer(perSecond, 100, 100, arena, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		ml->ClearTimer(perSecond, arena);

		mm->UnregCallback(CB_KILL, playerkill, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_PLAYERACTION, playeraction, arena);
//Lfailattach:
		DO_RETURN();
	}


	return MM_FAIL;
}

local void setBounty(Player *p, int amt)
{
	struct S2CWeapons wpn = {
		S2C_WEAPON, p->position.rotation, (current_ticks()) & 0xFFFF,
		p->position.x, p->position.yspeed, p->pid, p->position.xspeed, 0,
		p->position.status, 0, p->position.y, (short)amt
	};

	game->DoWeaponChecksum(&wpn);
	net->SendToOne(p, (byte*)&wpn, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_PRI_P4);
}

local void playeraction(Player *p, int action, Arena *a)
{
	BDEF_PD(p);

	if ((p->type != T_VIE) && (p->type != T_CONT))
		return;

	//actions applying to an arena.
	if (a)
	{
		if (action == PA_PREENTERARENA || action == PA_ENTERARENA)
		{
			pdat->leftSafe = 0;
			pdat->safeTime = 0;
			pdat->deathTimeout = 6;
		}
		else if (action == PA_LEAVEARENA)
		{
			if (pdat->leftSafe)
				stats->IncrementStat(p, STAT_DEATHS, 1);
			pdat->leftSafe = 0;
			pdat->safeTime = 0;
			pdat->deathTimeout = 6;
		}
	}
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	BDEF_PD(p);
	pdat->deathTimeout = 6;
	pdat->safeTime = 0;
	if (newship != oldship)
	{
		pdat->safeTime = 0;
		pdat->deathTimeout = 5;
		if (pdat->leftSafe)
			stats->IncrementStat(p, STAT_DEATHS, 1);
		pdat->leftSafe = 0;
	}
}

local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	BDEF_PD(killed);
	pdat->deathTimeout = 6;
	pdat->safeTime = 0;
	pdat->leftSafe = 0;
}

local int perSecond(void *arena)
{
	Arena *a = (Arena *)arena;
	Link *link;
	Player *p;
	BDEF_AD(a);

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		BDEF_PD(p);

		if (p->type == T_FAKE)
			continue;

		if (p->arena != a)
			continue;

		if (p->p_ship == SHIP_SPEC)
		{
			pdat->safeTime = 0;
			pdat->deathTimeout = 1;
			continue;
		}

		//this person has recently died/spawned... ignore whatever bullshit positions they may or may not be sending.
		if (pdat->deathTimeout > 0)
		{
			--pdat->deathTimeout;
			continue;
		}

		if (!(p->position.status & STATUS_SAFEZONE))
			pdat->leftSafe = 1;
		else if (pdat->leftSafe)
		{
			stats->IncrementStat(p, STAT_DEATHS, 1);
			pdat->leftSafe = 0;
		}

		if ((p->position.status & STATUS_SAFEZONE) || (p->position.bounty >= 30000))
		{
			if (++pdat->safeTime >= ad->interval)
			{
				int newbty = p->position.bounty - (p->position.bounty / 2);
				pdat->safeTime = 0;
				//if the adjusted bounty would be less than the minimum bounty, then the adjusted bounty becomes the minimum.
				if (newbty < ad->minBounty)
					newbty = ad->minBounty;

				//band-aid to people who get 65k bounty and then try to work it down to something that will still give them rewards
				if (p->position.bounty >= 30000)
				{
					if (pdat->attemptingFix)
					{
						lm->Log(L_ERROR, "we are unable to fix %s's 65k bounty.. speccing and advising to reconnect", p->name);
						game->SetShipAndFreq(p, SHIP_SPEC, a->specfreq);
						chat->SendMessage(p, "There is an error on your connection. If it continues, please reconnect to the zone.");
					}
					else
					{
						lm->Log(L_ERROR, "interestingly enough, %s has %i bounty.. let's try to fix that.", p->name, p->position.bounty);
						newbty = ad->minBounty;
						pdat->attemptingFix = 1;
					}
				}
				else
				{
					pdat->attemptingFix = 0;
				}

				//if we need to adjust the bounty, do it
				if (p->position.bounty > ad->minBounty)
					setBounty(p, newbty);
			}
			else //if (pdat->safeTime < ad->interval)
			{
				if (pdat->safeTime < 0)
				{
					lm->Log(L_ERROR, "interestingly enough, pdat->safeTime < 0. this should not happen. %s %i", p->name, pdat->safeTime);
					pdat->safeTime = 0;
				}
			}
		}
		else
		{
			if (--pdat->safeTime < 0)
				pdat->safeTime = 0;
		}
	}
	PDUNLOCK;

	return 1;
}
