//modification of fm_normal.c
//search for HYPERSPACE MODIFIED STUFF to find the bulk of the modification

#include <string.h>
#include <limits.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_shipnames.h"

/* cfghelp: Team:InitialSpec, arena, bool, def: 0
 * If players entering the arena are always assigned to spectator mode. */
#define INITIALSPEC(ch) cfg->GetInt(ch, "Team", "InitialSpec", 0)

/* cfghelp: Team:IncludeSpectators, arena, bool, def: 0
 * Whether to include spectators when enforcing maximum freq sizes. */
#define INCLSPEC(ch) cfg->GetInt(ch, "Team", "IncludeSpectators", 0)

/* cfghelp: Team:MaxPerTeam, arena, int, def: 0
 * The maximum number of players on a public freq. Zero means no limit. */
#define MAXTEAM(ch) cfg->GetInt(ch, "Team", "MaxPerTeam", 0)

/* cfghelp: General:MaxPlaying, arena, int, def: 100
 * This is the most players that will be allowed to play in the arena at
 * once. Zero means no limit. */
#define MAXPLAYING(ch) cfg->GetInt(ch, "General", "MaxPlaying", 100)

/* cfghelp: Misc:MaxXres, arena, int, def: 0
 * Maximum screen width allowed in the arena. Zero means no limit. */
#define MAXXRES(ch) cfg->GetInt(ch, "Misc", "MaxXres", 0)

/* cfghelp: Misc:MaxYres, arena, int, def: 0
 * Maximum screen height allowed in the arena. Zero means no limit. */
#define MAXYRES(ch) cfg->GetInt(ch, "Misc", "MaxYres", 0)


local Iplayerdata *pd;
local Iconfig *cfg;
local Ichat *chat;
local Icapman *capman;
local Imodman *mm;
local Ihscoredatabase *database;

local int count_current_playing(Arena *arena)
{
	Player *p;
	Link *link;
	int playing = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    p->arena == arena &&
		    p->p_ship != SHIP_SPEC)
			playing++;
	pd->Unlock();
	return playing;
}


local int count_freq(Arena *arena, int freq, Player *excl, int inclspec)
{
	Player *p;
	Link *link;
	int t = 0;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->arena == arena &&
		    p->p_freq == freq &&
		    p != excl &&
		    ( p->p_ship != SHIP_SPEC || inclspec ) )
			t++;
	pd->Unlock();
	return t;
}


local int FindLegalShip(Player *p, int freq, int ship)
{
	/* cfghelp: Team:FrequencyShipTypes, arena, bool, def: 0
	 * If this is set, freq 0 will only be allowed to use warbirds, freq
	 * 1 can only use javelins, etc. */
	int clockwork = cfg->GetInt(p->arena->cfg,
			"Misc", "FrequencyShipTypes", 0);

	if (clockwork)
	{
		/* we don't want to switch the ships of speccers, even in FST */
		if (ship == SHIP_SPEC || freq < 0 || freq > SHIP_SHARK)
			return SHIP_SPEC;
		else
			return freq;
	}
	else
	{
		if (ship < 0 || ship >= SHIP_SPEC)
			return SHIP_SPEC;


		//------------------------------HYPERSPACE MODIFIED STUFF------------------------------

		if (cfg->GetInt(p->arena->cfg, shipNames[ship], "BuyPrice", 0) != 0) //ship is for sale
		{

			if (database->areShipsLoaded(p))
			{
				if (database->getPlayerShipHull(p, ship) == NULL)
				{
					chat->SendMessage(p, "You do not own a %s hull on this shipset. Please use \"?buy ships\" to examine the ship hulls for sale.", shipNames[ship]);
					return SHIP_SPEC;
				}
			}
			else
			{
				chat->SendMessage(p, "Your ship data is not loaded in this arena. If you just entered, please wait a moment and try again.");
				return SHIP_SPEC;
			}
		}

		return ship;
	}
}


local int BalanceFreqs(Arena *arena, Player *excl, int inclspec)
{
	Player *i;
	Link *link;
	int counts[CFG_MAX_DESIRED] = { 0 }, desired, min = INT_MAX, best = -1, max, j;

	max = MAXTEAM(arena->cfg);
	/* cfghelp: Team:DesiredTeams, arena, int, def: 2
	 * The number of teams that the freq balancer will form as players
	 * enter. */
	desired = cfg->GetInt(arena->cfg,
			"Team", "DesiredTeams", 2);

	if (desired < 1) desired = 1;
	if (desired > CFG_MAX_DESIRED) desired = CFG_MAX_DESIRED;

	/* get counts */
	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->arena == arena &&
		    i->p_freq < desired &&
		    i != excl &&
		    ( i->p_ship != SHIP_SPEC || inclspec ) )
			counts[i->p_freq]++;
	pd->Unlock();

	for (j = 0; j < desired; j++)
		if (counts[j] < min)
		{
			min = counts[j];
			best = j;
		}

	if (best == -1) /* shouldn't happen */
		return 0;
	else if (max == 0 || best < max) /* we found a spot */
		return best;
	else /* no spots within desired freqs */
	{
		/* try incrementing freqs until we find one with < max players */
		j = desired;
		while (count_freq(arena, j, excl, inclspec) >= max)
			j++;
		return j;
	}
}

local int screen_res_allowed(Player *p, ConfigHandle ch)
{
	int max_x = MAXXRES(ch);
	int max_y = MAXYRES(ch);
	if((max_x == 0 || p->xres <= max_x) && (max_y == 0 || p->yres <= max_y))
		return 1;

	if (chat)
		chat->SendMessage(p,
			"Maximum allowed screen resolution is %dx%d in this arena",
			max_x, max_y);

	return 0;
}

local void Initial(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	int f, s = *ship;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;

	if (count_current_playing(arena) >= MAXPLAYING(ch) ||
	    p->flags.no_ship ||
	    !screen_res_allowed(p, ch) ||
	    INITIALSPEC(ch))
		s = SHIP_SPEC;

	if (s == SHIP_SPEC)
	{
		f = arena->specfreq;
	}
	else
	{
		/* we have to assign him to a freq */
		int inclspec = INCLSPEC(ch);
		f = BalanceFreqs(arena, p, inclspec);
		/* and make sure the ship is still legal */
		s = FindLegalShip(p, f, s);
	}

	*ship = s; *freq = f;
}


local void Ship(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	int f = *freq, s = *ship;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;

	/* always allow switching to spec */
	if (s >= SHIP_SPEC)
	{
		f = arena->specfreq;
	}
	/* otherwise, he's changing to a ship */
	/* check lag */
	else if (p->flags.no_ship)
	{
		if (chat)
			chat->SendMessage(p,
					"You have too much lag to play in this arena.");
		goto deny;
	}
	/* allowed res; this prints out its own error message */
	else if (!screen_res_allowed(p, ch))
	{
		goto deny;
	}
	/* check if changing from spec and too many playing */
	else if (p->p_ship == SHIP_SPEC && count_current_playing(arena) >= MAXPLAYING(ch))
	{
		if (chat)
			chat->SendMessage(p,
					"There are too many people playing in this arena.");
		goto deny;
	}
	/* ok, allowed change */
	else
	{
		/* check if he's changing from spec */
		if (p->p_ship == SHIP_SPEC && p->p_freq == arena->specfreq)
		{
			/* leaving spec, we have to assign him to a freq */
			int inclspec = INCLSPEC(ch);
			f = BalanceFreqs(arena, p, inclspec);
			/* and make sure the ship is still legal */
			s = FindLegalShip(p, f, s);
		}
		else
		{
			/* don't touch freq, but make sure ship is ok */
			s = FindLegalShip(p, f, s);
		}
	}

	*ship = s; *freq = f;
	return;

deny:
	*ship = p->p_ship;
	*freq = p->p_freq;
}


local void Freq(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	int f = *freq, s = *ship;
	int count, max, inclspec, maxfreq, privlimit;
	ConfigHandle ch;

	if (!arena) return;

	ch = arena->cfg;
	inclspec = INCLSPEC(ch);
	/* cfghelp: Team:MaxFrequency, arena, int, range: 0-9999, def: 9999
	 * The highest frequency allowed. Set this below PrivFreqStart to
	 * disallow private freqs. */
	maxfreq = cfg->GetInt(ch, "Team", "MaxFrequency", 9999);
	/* cfghelp: Team:PrivFreqStart, arena, int, range: 0-9999, def: 100
	 * Freqs above this value are considered private freqs. */
	privlimit = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);

	if (f >= privlimit)
		/* cfghelp: Team:MaxPerPrivateTeam, arena, int, def: 0
		 * The maximum number of players on a private freq. Zero means
		 * no limit. */
		max = cfg->GetInt(ch, "Team", "MaxPerPrivateTeam", 0);
	else
		max = MAXTEAM(ch);

	/* special case: speccer re-entering spec freq */
	if (s == SHIP_SPEC && f == arena->specfreq)
		return;

	if (f < 0 || f > maxfreq)
		/* he requested a bad freq. drop him elsewhere. */
		f = BalanceFreqs(arena, p, inclspec);
	else
	{
		/* check to make sure the new freq is ok */
		count = count_freq(arena, f, p, inclspec);
		if (max > 0 && count >= max)
			/* the freq has too many people, assign him to another */
			f = BalanceFreqs(arena, p, inclspec);
		/* cfghelp: Team:ForceEvenTeams, arena, boolean, def: 0
		 * Whether players can switch to more populous teams. */
		else if (cfg->GetInt(ch, "Team", "ForceEvenTeams", 0))
		{
			int old = count_freq(arena, p->p_freq, p, inclspec);

			/* ForceEvenTeams */
			if (old < count)
			{
				if (chat)
					chat->SendMessage(p,
						"Changing frequencies would make the teams uneven.");
				*freq = p->p_freq;
				*ship = p->p_ship;
				return;
			}
		}
	}

	/* make sure he has an appropriate ship for this freq */
	s = FindLegalShip(p, f, s);

	/* check if this change brought him out of spec and there are too
	 * many people playing. */
	if (s != SHIP_SPEC &&
	    p->p_ship == SHIP_SPEC &&
	    count_current_playing(arena) >= MAXPLAYING(ch))
	{
		s = p->p_ship;
		f = p->p_freq;
		if (chat)
			chat->SendMessage(p,
					"There are too many people playing in this arena.");
	}

	*ship = s; *freq = f;
}


local Ifreqman fm_int =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "hscore-freqman")
	Initial, Ship, Freq
};

EXPORT const char info_hscore_freqman[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_freqman(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
		if (!pd || !cfg || !database)
			return MM_FAIL;
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (fm_int.head.refcount)
			return MM_FAIL;
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(database);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&fm_int, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&fm_int, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

