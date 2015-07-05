#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "hscore_spawner.h"

typedef struct
{
	int is_flagging;
	int center_count;
} pdata;

typedef struct
{
	int priv_freq_start;
	int max_freq;
	Region *center;
	int center_limit;
	int can_change_on_flagging;

	int shuffle_on_flagreset;
	int shuffle_delay;
} adata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Iarenaman *aman;
local Iplayerdata *pd;
local Ichat *chat;
local Ifreqman *freqman;
local Icmdman *cmd;
local Imainloop *ml;
local Iflagcore *fc;
local Imapdata *mapdata;
local Igame *game;

local int pdkey;
local int adkey;

local int find_freq_pop(Arena *arena, int freq)
{
	Player *p;
	Link *link;
	int count = 0;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(p, arena)
	{
		if (!IS_STANDARD(p)) continue;

		if (p->p_freq == freq)
		{
			count++;
		}
	}
	pd->Unlock();

	return count;
}

local int has_full_energy(Player *p)
{
	int full = 1;
	if (p->p_ship == SHIP_SPEC)
	   return full;

	Ihscorespawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
	if (spawner)
	{
		int max = spawner->getFullEnergy(p);
		if (max != p->position.energy)
		{
				full = 0;
		}
	}
	mm->ReleaseInterface(spawner);

	return full;
}


/**
 * Performs the actual work of doing a freq/ship change through the freqman. This function will
 * attempt to keep the player in their ship if possible.
 *
 * @param Player *p
 *	The player on whom to perform a freq/ship change.
 *
 * @param int requested_freq
 *	The requested frequency the player is to be assigned.
 *
 * @param char *err_buf
 *	[In/Out] A buffer to receive any applicable error messages. Ignored if null.
 *
 * @param int buf_len
 *	The length of the buffer. Ignored if the buffer is null.
 *
 * @return bool
 *	True if the player can change to the given frequency; false otherwise.
 */
local int doFreqChange(Player *p, int requested_freq, char *err_buf, int buf_len)
{
	int ship = p->p_ship;
	shipmask_t mask;

	// Make sure they can use a ship on the requested frequency.
	if ((mask = freqman->GetAllowableShips(p, requested_freq))) {
		if (ship == SHIP_SPEC || !SHIPMASK_HAS(ship, mask)) {
			// Update ship!
			for (ship = SHIP_WARBIRD; ship < SHIP_SPEC && !SHIPMASK_HAS(ship, mask); ++ship);

			// Check that the change would be allowed...
			if (!freqman->CanChangeToShip(p, ship, err_buf, buf_len)) {
				return 0; // Nope.
			}
		}

		// Their ship is acceptible at this point. Check that the freq change is allowed.
		if (freqman->CanChangeToFreqWithShip(p, requested_freq, ship, err_buf, buf_len)) {
			// It is! We can skip the freqman for the actual assignment here since we know it's good at
			// this point.
			game->SetShipAndFreq(p, ship, requested_freq);
			return 1;
		}
	} else {
		if (err_buf && buf_len) {
			snprintf(err_buf, buf_len, "You do not own any usable ships for flagging. Please use \"?buy ships\" to examine the ship hulls for sale.");
		}
	}

	return 0;
}

local int freqchange_player(Player *p, char *err_buf, int buf_len)
{
  pdata *pdata = PPDATA(p, pdkey);
  adata *ad = P_ARENA_DATA(p->arena, adkey);

  int best_freq, best_pop;
  int i;

	// allow them to switch to privs
	pdata->is_flagging = 1;
	pdata->center_count = 0;

	// find the least populated freq
	best_freq = ad->priv_freq_start;
	best_pop = find_freq_pop(p->arena, best_freq);
	for (i = ad->priv_freq_start + 1; i < ad->max_freq; i++)
	{
		int pop = find_freq_pop(p->arena, i);
		if (pop < best_pop)
		{
			best_pop = pop;
			best_freq = i;
		}
	}


	// Try switching to the "best" team first.
	if (doFreqChange(p, best_freq, err_buf, buf_len)) {
		return 1;
	}

	// Step through the remaining teams in order.
	// (This is kinda shitty and should be replaced with something better.)
	for (i = ad->priv_freq_start; i < ad->max_freq; ++i) {
		if (i != best_freq && doFreqChange(p, i, err_buf, buf_len)) {
			return 1;
		}
	}

	return 0;
}

local helptext_t flag_help =
"Targets: none\n"
"Args: none\n"
"Puts you on a flagging frequency.\n";

local void Cflag(const char *command, const char *params, Player *p, const Target *target)
{
	char err_buf[80];
	pdata *pdata;
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;

	pdata = PPDATA(t, pdkey);
	if (pdata->is_flagging)
	{
		if (p == t)
		{
			chat->SendMessage(p, "You are already on a flagging frequency.");
		}
		else
		{
			chat->SendMessage(p, "%s is already on a flagging frequency.", t->name);
		}
		return;
	}

	if (!has_full_energy(p))
	{
		chat->SendMessage(p, "Not enough energy to change frequencies.");
		return;
	}

	err_buf[0] = 0;
	if (freqchange_player(t, err_buf, sizeof(err_buf))) return;

	if (p == t)
	{
		chat->SendMessage(p, "Error entering flagging frequency: \"%s\".", err_buf);
	}
	else
	{
		chat->SendMessage(p, "%s: Error entering flagging frequency: \"%s\".", t->name, err_buf);
	}

	pdata->is_flagging = 0;
}

local int CanChangeFreq(Player *p, int new_freq, int is_changing, char *err_buf, int buf_len)
{
	pdata *pdata = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(p->arena, adkey);

	if (new_freq >= ad->priv_freq_start)
	{
		if (!pdata->is_flagging)
		{
			if (err_buf)
			{
				snprintf(err_buf, buf_len, "Please use ?flag to change to a flagging frequency.");
			}
			return 0;
		}
		else if (p->p_freq >= ad->priv_freq_start && p->p_freq <= ad->max_freq)
		{
			if (ad->can_change_on_flagging || p->p_freq == p->arena->specfreq)
			{
				int found = 0;
				Player *i;
				Link *link;

				pd->Lock();
				FOR_EACH_PLAYER(i)
					if (i != p && i->arena == p->arena && i->p_freq == p->p_freq && IS_HUMAN(i))
					{
						found = 1;
						break;
					}
				pd->Unlock();

				if (!found)
				{
					if (err_buf)
					{
						snprintf(err_buf, buf_len, "Your team needs at least one player.");
					}
					return 0;
				}
			}
			else
			{
				if (err_buf)
				{
					snprintf(err_buf, buf_len, "You cannot switch between flagging teams.");
				}
				return 0;
			}
		}
	}
	return 1;
}


local int are_flags_in_center(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	FlagInfo flags[30];
	int i;
	int flagcount;

	flagcount = fc->GetFlags(arena, 0, flags, 30);

	for (i = 0; i < flagcount; i++)
	{
		switch (flags[i].state)
		{
			case FI_NONE:
				break;
			case FI_ONMAP:
				if (mapdata->Contains(ad->center, flags[i].x, flags[i].y))
				{
					return 1;
				}
				break;
			case FI_CARRIED:
				if (flags[i].carrier && mapdata->Contains(ad->center, flags[i].carrier->position.x >> 4, flags[i].carrier->position.y >> 4))
				{
					return 1;
				}
				break;
		}
	}

	return 0;
}

local int center_count_timer(void *clos)
{
	Arena *arena = (Arena*)clos;
	adata *ad = P_ARENA_DATA(arena, adkey);
	Player *p;
	Link *link;

	if (ad->center)
	{
		// check if there are any flags in the center
		if (!are_flags_in_center(arena))
		{
			// no flags in center
			pd->Lock();
			FOR_EACH_PLAYER_IN_ARENA(p, arena)
			{
				pdata *pdata = PPDATA(p, pdkey);
				if (pdata->is_flagging && mapdata->Contains(ad->center, p->position.x >> 4, p->position.y >> 4))
				{
					pdata->center_count++;
					if (pdata->center_count >= ad->center_limit)
					{
						chat->SendMessage(p, "You're being specced for lack of flagging on a flagging frequency");
						game->SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
					}
				}
				else
				{
					pdata->center_count = 0;
				}
			}
			pd->Unlock();
		}
		else
		{
			// flags in center
			pd->Lock();
			FOR_EACH_PLAYER_IN_ARENA(p, arena)
			{
				pdata *pdata = PPDATA(p, pdkey);
				pdata->center_count = 0;
			}
			pd->Unlock();
		}
	}

	return TRUE;
}

local void update_config(Arena *arena)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	ConfigHandle ch = arena->cfg;
	const char *region_name;

	ad->priv_freq_start = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);
	ad->max_freq = cfg->GetInt(ch, "Team", "MaxFrequency", ad->priv_freq_start+2);
	ad->center_limit = cfg->GetInt(ch, "Hyperspace", "CenterFlaggerLimit", 30); // measured in 10 second intervals

	region_name = cfg->GetStr(ch, "Hyperspace", "CenterRegionName");

	ad->can_change_on_flagging = cfg->GetInt(ch, "Hyperspace", "CanChangeOnFlagging", 1);

	ad->shuffle_on_flagreset = cfg->GetInt(ch, "Hyperspace", "ShuffleTeamsOnFlagReset", 1);
	ad->shuffle_delay = cfg->GetInt(ch, "Hyperspace", "ShuffleDelay", 100);

	if (region_name)
	{
		ad->center = mapdata->FindRegionByName(arena, region_name);
	}
	else
	{
		ad->center = mapdata->FindRegionByName(arena, "sector0");
	}
}

local void aaction(Arena *arena, int action)
{
	if (action == AA_CONFCHANGED)
	{
		update_config(arena);
	}
}

local void paction(Player *p, int action)
{
	if (action == PA_ENTERARENA)
	{
		pdata *pdata = PPDATA(p, pdkey);
		pdata->is_flagging = 0;
		pdata->center_count = 0;
	}
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	if (newfreq < ad->priv_freq_start || newfreq >= ad->max_freq)
	{
		pdata *pdata = PPDATA(p, pdkey);
		pdata->is_flagging = 0;
		pdata->center_count = 0;
	}
}

local void init_arena(Arena *arena)
{
	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(p, arena)
	{
			pdata *pdata = PPDATA(p, pdkey);
			pdata->is_flagging = 0;
			pdata->center_count = 0;
	}
	pd->Unlock();
}

local Aenforcer myadv =
{
  ADVISER_HEAD_INIT(A_ENFORCER)
  NULL,
  NULL,
  NULL,
  CanChangeFreq,
  NULL
};

local void shuffle_array(Link *array[], size_t n)
{
    if (n > 1)
	{
		size_t i;
		for (i = 0; i < n - 1; i++)
		{
			size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
			Link *t = array[j];
			array[j] = array[i];
			array[i] = t;
		}
    }
}

local void shuffle_list(LinkedList *list, int count)
{
	Link *links[count];
	Link *link;

	int i = 0;

	for (link = LLGetHead(list); link; link = link->next)
	{
		links[i] = link;
		i++;
	}

	shuffle_array(links, count);

	list->start = links[0];

	for (i = 0; i < count - 1; i++)
	{
        link = links[i];
        link->next = links[i + 1];
    }

    list->end = links[count-1];
    list->end->next = NULL;
}

local int shuffle_timer(void *clos)
{
	Arena *arena = (Arena*)clos;
	LinkedList flaggers = LL_INITIALIZER;

	pdata *pdata;
	Link *link;
	Player *p;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(p, arena)
	{
		pdata = PPDATA(p, pdkey);

		if (pdata->is_flagging)
		{
			LLAdd(&flaggers, p);
		}
	}
	pd->Unlock();

	FOR_EACH(&flaggers, p, link)
	{
		game->SetFreq(p, arena->specfreq);
	}

	int count = LLCount(&flaggers);
	if (count == 0) return 0;

	shuffle_list(&flaggers, count);

	char err_buf[80];

	FOR_EACH(&flaggers, p, link)
	{
		pdata = PPDATA(p, pdkey);
		pdata->is_flagging = 1;

		if (!freqchange_player(p, err_buf, sizeof(err_buf)))
		{
			lm->LogP(L_WARN, "hs_flagprivs", p, "Error entering flagging frequency: \"%s\".", err_buf);

			game->SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
			pdata->is_flagging = 0;
		}
	}

	return 0;
}

local void flagreset(Arena *arena, int freq, int points)
{
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (ad->shuffle_on_flagreset)
	{
		ml->SetTimer(shuffle_timer, ad->shuffle_delay, ad->shuffle_delay, arena, arena);
	}
}

EXPORT const char info_hs_flagprivs[] = "v1.1 Dr Brain <drbrain@gmail.com>, SpiderNL";

EXPORT int MM_hs_flagprivs(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		freqman = mm->GetInterface(I_FREQMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!lm || !cfg || !aman || !pd || !chat || !freqman || !cmd || !ml || !fc || !mapdata || !game) return MM_FAIL;


		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;
		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(freqman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(fc);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(game);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegCallback(CB_ARENAACTION, aaction, arena);
		mm->RegCallback(CB_PLAYERACTION, paction, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_FLAGRESET, flagreset, arena);

		update_config(arena);
		init_arena(arena);

		cmd->AddCommand("flag", Cflag, arena, flag_help);

		mm->RegAdviser(&myadv, arena);

		ml->SetTimer(center_count_timer, 1000, 1000, arena, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregAdviser(&myadv, arena);

		cmd->RemoveCommand("flag", Cflag, arena);

		ml->ClearTimer(center_count_timer, arena);

		mm->UnregCallback(CB_ARENAACTION, aaction, arena);
		mm->UnregCallback(CB_PLAYERACTION, paction, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_FLAGRESET, flagreset, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
