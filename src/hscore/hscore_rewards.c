//HSCore Rewards
//D1st0rt and Bomook
//5/31/05

#include "asss.h"
#include "fg_wz.h"
#include "hscore.h"
#include <math.h>
#include "hscore_teamnames.h"
#include "hscore_shipnames.h"
#include "jackpot.h"
#include "persist.h"
#include "formula.h"

typedef struct BountyMap
{
	int size; // one dimensional size
	short *bounty;
	ticks_t *timeout;
} BountyMap;

typedef struct PData
{
	int min_kill_money_to_notify;
	int min_kill_exp_to_notify;
	int min_shared_money_to_notify;

	int periodic_tally;

	int edit_ppk;
	int show_exp;
} PData;

typedef struct AData
{
	int on;
	Formula *kill_money_formula;
	Formula *kill_exp_formula;
	Formula *kill_jp_formula;			// Currently does nothing. -C
	Formula *bonus_kill_money_formula;
	Formula *bonus_kill_exp_formula;
	Formula *flag_money_formula;
	Formula *loss_flag_money_formula;
	Formula *flag_exp_formula;
	Formula *loss_flag_exp_formula;
	Formula *periodic_money_formula;
	Formula *periodic_exp_formula;

	Region *periodic_include_region;
	Region *periodic_exclude_region;
	Region *bonus_region;

	double teammate_max[8];
	double dist_coeff[8];

	int periodic_tally;
	int reset;

	int winning_freq;
	int max_flag_money;
	int max_flag_exp;
	int max_loss_money;
	int max_loss_exp;
} AData;

//modules
local Imodman *mm;
local Ilogman *lm;
local Iplayerdata *pd;
local Ichat *chat;
local Iconfig *cfg;
local Icmdman *cmd;
local Ipersist *persist;
local Iformula *formula;
local Iarenaman *aman;
local Imapdata *mapdata;
local Imainloop *ml;
local Iflagcore *flagcore;
local Ihscoredatabase *database;
local Ihscoreitems *items;

local int pdkey;
local int adkey;
local BountyMap bounty_map;

local FormulaVariable * player_exp_callback(Player *p)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_DOUBLE;
	var->value = (double)database->getExp(p);

	return var;
}

local FormulaVariable * player_money_callback(Player *p)
{
	FormulaVariable *var = amalloc(sizeof(FormulaVariable));
	var->name = NULL;
	var->type = VAR_TYPE_DOUBLE;
	var->value = (double)database->getMoney(p);

	return var;
}

local helptext_t killmessages_help =
"Targets: none\n"
"Args: none\n"
"Toggles the kill reward messages on and off";

local void Ckillmessages(const char *command, const char *params, Player *p, const Target *target)
{
	PData *pdata = PPDATA(p, pdkey);

	if (pdata->min_kill_money_to_notify == 0)
	{
		pdata->min_kill_money_to_notify = -1;
		pdata->min_kill_exp_to_notify = -1;
		pdata->min_shared_money_to_notify = -1;

		chat->SendMessage(p, "Kill messages disabled!");
	}
	else
	{
		pdata->min_kill_money_to_notify = 0;
		pdata->min_kill_exp_to_notify = 0;
		pdata->min_shared_money_to_notify = 20;

		chat->SendMessage(p, "Kill messages enabled!");
	}
}

local helptext_t bountytype_help =
"Targets: none\n"
"Args: none\n"
"Toggles the bounty displayed on players";

local void Cbountytype(const char *command, const char *params, Player *p, const Target *target)
{
	PData *pdata = PPDATA(p, pdkey);

	if (pdata->edit_ppk == 0)
	{
		pdata->edit_ppk = 1;
		pdata->show_exp = 0;
		chat->SendMessage(p, "Set to display reward money.");
	}
	else
	{
		if (pdata->show_exp == 0)
		{
			pdata->show_exp = 1;
			chat->SendMessage(p, "Set to display reward exp.");
		}
		else
		{
			pdata->edit_ppk = 0;
			chat->SendMessage(p, "Set to display player bounty");
		}
	}
}

local int GetPersistData(Player *p, void *data, int len, void *clos)
{
	PData *pdata = PPDATA(p, pdkey);

	PData *persist_data = (PData*)data;

	persist_data->min_kill_money_to_notify = pdata->min_kill_money_to_notify;
	persist_data->min_kill_exp_to_notify = pdata->min_kill_exp_to_notify;
	persist_data->min_shared_money_to_notify = pdata->min_shared_money_to_notify;
	persist_data->show_exp = pdata->show_exp;
	persist_data->edit_ppk = pdata->edit_ppk;

	return sizeof(PData);
}

local void SetPersistData(Player *p, void *data, int len, void *clos)
{
	PData *pdata = PPDATA(p, pdkey);

	PData *persist_data = (PData*)data;

	pdata->min_kill_money_to_notify = persist_data->min_kill_money_to_notify;
	pdata->min_kill_exp_to_notify = persist_data->min_kill_exp_to_notify;
	pdata->min_shared_money_to_notify = persist_data->min_shared_money_to_notify;
	pdata->show_exp = persist_data->show_exp;
	pdata->edit_ppk = persist_data->edit_ppk;
}

local void ClearPersistData(Player *p, void *clos)
{
	PData *pdata = PPDATA(p, pdkey);

	pdata->min_kill_money_to_notify = 0;
	pdata->min_kill_exp_to_notify = 0;
	pdata->min_shared_money_to_notify = 20;
	pdata->edit_ppk = 0;
	pdata->show_exp = 0;
}

local PlayerPersistentData my_persist_data =
{
	11504, INTERVAL_FOREVER, PERSIST_GLOBAL,
	GetPersistData, SetPersistData, ClearPersistData
};

local void update_flag_rewards(Arena *arena, int freq)
{
	AData *adata = P_ARENA_DATA(arena, adkey);

	if (adata->flag_money_formula || adata->flag_exp_formula)
	{
		int money = 0;
		int loss_money = 0;
		int exp = 0;
		int loss_exp = 0;
		char error_buf[200];
		error_buf[0] = '\0';

		HashTable *vars = HashAlloc();

		FormulaVariable arena_var, winner_var, loser_var;
		arena_var.name = NULL;
		arena_var.type = VAR_TYPE_ARENA;
		arena_var.arena = arena;

		winner_var.name = NULL;
		winner_var.type = VAR_TYPE_FREQ;
		winner_var.freq.arena = arena;
		winner_var.freq.freq = freq;

		int priv_freq_start = cfg->GetInt(arena->cfg, "Team", "PrivFreqStart", 100);
	  int max_freq = cfg->GetInt(arena->cfg, "Team", "MaxFrequency", (priv_freq_start + 2));

		// IMPL NOTE: This is a hack to have a single winner/loser freq. It operates under the assumtion
		// that only two frequences exist for flagging (90/91 at the time of writing). If more freqs are
		// added, this function will fail. Hard. Probably.

		if(max_freq - priv_freq_start > 2) {
			// We have more than two teams. The loser will be an arena reference.
			loser_var.name = NULL;
			loser_var.type = VAR_TYPE_ARENA;
			loser_var.arena = arena;
		} else {
			// Two teams. Winner is winner freq, loser is the other priv freq.
			loser_var.name = NULL;
			loser_var.type = VAR_TYPE_FREQ;
			loser_var.freq.arena = arena;
			loser_var.freq.freq = (priv_freq_start == freq ? (freq + 1) : priv_freq_start);
		}

		HashAdd(vars, "arena", &arena_var);
		HashAdd(vars, "winner", &winner_var);
		HashAdd(vars, "loser", &loser_var);

		if (adata->flag_money_formula)
		{
			money = formula->EvaluateFormulaInt(adata->flag_money_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				money = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with flag money formula: %s", error_buf);
			}
		}

		if (adata->loss_flag_money_formula)
		{
			loss_money = formula->EvaluateFormulaInt(adata->loss_flag_money_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				loss_money = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with loss flag money formula: %s", error_buf);
			}
		}

		if (adata->flag_exp_formula)
		{
			exp = formula->EvaluateFormulaInt(adata->flag_exp_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				exp = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with flag exp formula: %s", error_buf);
			}
		}

		if (adata->loss_flag_exp_formula)
		{
			loss_exp = formula->EvaluateFormulaInt(adata->loss_flag_exp_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				loss_exp = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with loss flag exp formula: %s", error_buf);
			}
		}

		if (adata->winning_freq != freq || adata->max_flag_money < money)
		{
			adata->winning_freq = freq;
			adata->max_flag_money = money;
			adata->max_flag_exp = exp;
			adata->max_loss_money = loss_money;
			adata->max_loss_exp = loss_exp;
		}

		HashFree(vars);
	}
}

local int flag_reward_timer(void *clos)
{
	Arena *arena = clos;
	AData *adata = P_ARENA_DATA(arena, adkey);
	FlagInfo flags[255];
	int i, n;
	int freq = -1;

	n = flagcore->GetFlags(arena, 0, flags, 255);

	for (i = 0; i < n; i++)
	{
		int flag_freq;
		switch (flags[i].state)
		{
			case FI_NONE:
				adata->winning_freq = -1;
				return TRUE;
			case FI_ONMAP:
				flag_freq = flags[i].freq;
				break;
			case FI_CARRIED:
				flag_freq = flags[i].carrier->p_freq;
				break;
		}
		if (freq == -1)
		{
			freq = flag_freq;
		}
		else if (freq != flag_freq)
		{
			adata->winning_freq = -1;
			return TRUE;
		}
	}

	if (freq != -1)
	{
		update_flag_rewards(arena, freq);
	}

	return TRUE;
}

//This is assuming we're using fg_wz.py
local void flagWinCallback(Arena *arena, int freq, int *pts)
{
	AData *adata = P_ARENA_DATA(arena, adkey);

	if (adata->flag_money_formula || adata->flag_exp_formula)
	{
		Player *i;
		Link *link;
		Iteamnames *teamnames;

		update_flag_rewards(arena, freq);

		teamnames = mm->GetInterface(I_TEAMNAMES, arena);
		if (teamnames)
		{
			const char *name = teamnames->getFreqTeamName(freq, arena);
			if (name != NULL)
			{
				chat->SendArenaMessage(arena, "%s won flag game. Reward: $%d (%d exp)", name, adata->max_flag_money, adata->max_flag_exp);
			}
			else
			{
				chat->SendArenaMessage(arena, "Unidentified team won flag game. Reward: $%d (%d exp)", adata->max_flag_money, adata->max_flag_exp);
			}
		}
		else
		{
			chat->SendArenaMessage(arena, "Base reward: $%d (%d exp)", adata->max_flag_money, adata->max_flag_exp);
		}

		// Impl note:
		// More hackery to make sure only players on the flag teams get rewarded.
		int priv_freq_start = cfg->GetInt(arena->cfg, "Team", "PrivFreqStart", 100);
		int max_freq = cfg->GetInt(arena->cfg, "Team", "MaxFrequency", (priv_freq_start + 2));

		 //Distribute Wealth
		pd->Lock();
		FOR_EACH_PLAYER(i)
		{
			if(i->arena == arena && i->p_ship != SHIP_SPEC)
			{
				if (i->p_freq == freq) {
					int exp_reward = adata->max_flag_exp;
					int hsd_reward = adata->max_flag_money;

					// int pmul_exp = items->getPropertySum(i, i->p_ship, "exp_multiplier", 100);
					// float exp_mul = ((float) pmul_exp / 100.0);
					// int pmul_hsd = items->getPropertySum(i, i->p_ship, "hsd_multiplier", 100);
					// float hsd_mul = ((float) pmul_hsd / 100.0);

					// exp_reward *= exp_mul;
					// hsd_reward *= hsd_mul;

					//no need to send message, as the team announcement works just fine
					database->addMoney(i, MONEY_TYPE_FLAG, hsd_reward);
					database->addExp(i, exp_reward);
				} else if (i->p_freq >= priv_freq_start && i->p_freq < max_freq) {
					int exp_reward = adata->max_loss_exp;
					int hsd_reward = adata->max_loss_money;

					// int pmul_exp = items->getPropertySum(i, i->p_ship, "exp_multiplier", 100);
					// float exp_mul = ((float) pmul_exp / 100.0);
					// int pmul_hsd = items->getPropertySum(i, i->p_ship, "hsd_multiplier", 100);
					// float hsd_mul = ((float) pmul_hsd / 100.0);

					// exp_reward *= exp_mul;
					// hsd_reward *= hsd_mul;

					database->addMoney(i, MONEY_TYPE_FLAG, hsd_reward);
					database->addExp(i, exp_reward);

					if (exp_reward && hsd_reward) {
						chat->SendMessage(i, "You received $%d and %d exp for a flag loss.", hsd_reward, exp_reward);

					} else if (exp_reward) {
						chat->SendMessage(i, "You received %d exp for a flag loss.", exp_reward);

					} else if (hsd_reward) {
						chat->SendMessage(i, "You received $%d for a flag loss.", hsd_reward);
					}
				}
			}
		}
		pd->Unlock();

		adata->winning_freq = -1;
	}
}

local int calculateKillExpReward(Arena *arena, Player *killer, Player *killed, int bounty, int bonus)
{
	AData *adata = P_ARENA_DATA(arena, adkey);
	int exp = 0;
	HashTable *vars = HashAlloc();
	char error_buf[200];

	FormulaVariable killer_var, killed_var, bounty_var, arena_var;
	killer_var.name = NULL;
	killer_var.type = VAR_TYPE_PLAYER;
	killer_var.p = killer;
	killed_var.name = NULL;
	killed_var.type = VAR_TYPE_PLAYER;
	killed_var.p = killed;
	bounty_var.name = NULL;
	bounty_var.type = VAR_TYPE_DOUBLE;
	bounty_var.value = (double)bounty;
	arena_var.name = NULL;
	arena_var.type = VAR_TYPE_ARENA;
	arena_var.arena = arena;

	error_buf[0] = '\0';

	HashAdd(vars, "killer", &killer_var);
	HashAdd(vars, "killed", &killed_var);
	HashAdd(vars, "bounty", &bounty_var);
	HashAdd(vars, "arena", &arena_var);

	if (adata->kill_exp_formula)
	{
		exp = formula->EvaluateFormulaInt(adata->kill_exp_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
		if (error_buf[0] != '\0')
		{
			exp = 0;
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error with kill exp formula: %s", error_buf);
		}
	}

	if (bonus && adata->bonus_kill_exp_formula)
	{
		if (adata->bonus_region == NULL || mapdata->Contains(adata->bonus_region, killer->position.x >> 4, killer->position.y >> 4))
		{
			int bonus = formula->EvaluateFormulaInt(adata->bonus_kill_exp_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				bonus = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with bonus kill exp formula: %s", error_buf);
			}
			exp += bonus;
		}
	}

	HashFree(vars);

	int pmul = items->getPropertySum(killer, killer->p_ship, "exp_multiplier", 100);
	float multiplier = ((float) pmul / 100.0);

	exp *= multiplier;

	return exp;
}

local int calculateKillMoneyReward(Arena *arena, Player *killer, Player *killed, int bounty, int bonus)
{
	AData *adata = P_ARENA_DATA(arena, adkey);
	int money = 0;
	HashTable *vars = HashAlloc();
	char error_buf[200];

	FormulaVariable killer_var, killed_var, bounty_var, arena_var;
	killer_var.name = NULL;
	killer_var.type = VAR_TYPE_PLAYER;
	killer_var.p = killer;
	killed_var.name = NULL;
	killed_var.type = VAR_TYPE_PLAYER;
	killed_var.p = killed;
	bounty_var.name = NULL;
	bounty_var.type = VAR_TYPE_DOUBLE;
	bounty_var.value = (double)bounty;
	arena_var.name = NULL;
	arena_var.type = VAR_TYPE_ARENA;
	arena_var.arena = arena;

	error_buf[0] = '\0';

	HashAdd(vars, "killer", &killer_var);
	HashAdd(vars, "killed", &killed_var);
	HashAdd(vars, "bounty", &bounty_var);
	HashAdd(vars, "arena", &arena_var);

	if (adata->kill_money_formula)
	{
		money = formula->EvaluateFormulaInt(adata->kill_money_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
		if (error_buf[0] != '\0')
		{
			money = 0;
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error with kill money formula: %s", error_buf);
		}
	}

	if (bonus && adata->bonus_kill_money_formula)
	{
		if (adata->bonus_region == NULL || mapdata->Contains(adata->bonus_region, killer->position.x >> 4, killer->position.y >> 4))
		{
			int bonus = formula->EvaluateFormulaInt(adata->bonus_kill_money_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				bonus = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with bonus kill money formula: %s", error_buf);
			}
			money += bonus;
		}
	}

	HashFree(vars);

	int pmul = items->getPropertySum(killer, killer->p_ship, "hsd_multiplier", 100);
	float multiplier = ((float) pmul / 100.0);

	money *= multiplier;

	return money;
}

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	PData *pdata = PPDATA(killer, pdkey);
	AData *adata = P_ARENA_DATA(arena, adkey);
	int killerexp;

	if (killer == killed) return;

	if(killer->p_freq == killed->p_freq)
	{
		chat->SendMessage(killer, "No reward for teamkill of %s.", killed->name);
	}
	else
	{
		/* cfghelp: Hyperspace:MinBonusPlayers, arena, int, def: 4, mod: hscore_rewards
		 * Minimum number of players in game required for bonus money. */
		int minBonusPlayers  = cfg->GetInt(arena->cfg, "Hyperspace", "MinBonusPlayers",  4);

		//Calculate Earned Money
		int bonus = arena->playing >= minBonusPlayers;
		int money = calculateKillMoneyReward(arena, killer, killed, bounty, bonus);
		int experience = calculateKillExpReward(arena, killer, killed, bounty, bonus);

		int notify_for_exp = pdata->min_kill_exp_to_notify != -1 && pdata->min_kill_exp_to_notify <= experience;
		int notify_for_money = pdata->min_kill_money_to_notify != -1 && pdata->min_kill_money_to_notify <= money;

		//Distribute Wealth
		database->addExp(killer, experience);
		database->addMoney(killer, MONEY_TYPE_KILL, money);

		if (experience && notify_for_exp && money == 0)
		{
			chat->SendMessage(killer, "You received %d exp for killing %s.", experience, killed->name);
		}
		else if (money && notify_for_money && experience == 0)
		{
			chat->SendMessage(killer, "You received $%d for killing %s.", money, killed->name);
		}
		else if ((money && notify_for_money) || (experience && notify_for_exp))
		{
			chat->SendMessage(killer, "You received $%d and %d exp for killing %s.", money, experience, killed->name);
		}

		killerexp = database->getExp(killer);

		//give money to teammates
		Player *p;
		Link *link;
		pd->Lock();
		FOR_EACH_PLAYER(p)
		{
			if(p->arena == killer->arena && p->p_freq == killer->p_freq && p->p_ship != SHIP_SPEC && p != killer && !(p->position.status & STATUS_SAFEZONE))
			{
				double maxReward;
				if (database->getExp(p) > killerexp)
				{
					maxReward = adata->teammate_max[p->p_ship] * calculateKillMoneyReward(arena, p, killed, bounty, bonus);
				}
				else
				{
					maxReward = adata->teammate_max[p->p_ship] * money;
				}

				int xdelta = (p->position.x - killer->position.x);
				int ydelta = (p->position.y - killer->position.y);
				double distPercentage = ((double)(xdelta * xdelta + ydelta * ydelta)) / adata->dist_coeff[p->p_ship];

				int reward = (int)(maxReward * exp(-distPercentage));

				database->addMoney(p, MONEY_TYPE_KILL, reward);

				PData *tdata = PPDATA(p, pdkey);
				if (tdata->min_shared_money_to_notify != -1 && tdata->min_shared_money_to_notify <= reward)
				{
					chat->SendMessage(p, "You received $%d for %s's kill of %s.", reward, killer->name, killed->name);
				}
			}
		}
		pd->Unlock();
	}
}

local int edit_ppk_bounty(Player *p, Player *t, struct C2SPosition *pos, int *extralen)
{
	if (t->p_ship != SHIP_SPEC)
	{
		PData *pdata = PPDATA(t, pdkey);
		if (pdata->edit_ppk)
		{
			int index = p->pid * bounty_map.size + t->pid;
			ticks_t gtc = current_ticks();
			if (bounty_map.timeout[index] < gtc)
			{
				if (pdata->show_exp)
				{
					int minBonusPlayers = cfg->GetInt(p->arena->cfg, "Hyperspace", "MinBonusPlayers",  4);
					int bonus = p->arena->playing >= minBonusPlayers;
					int exp = calculateKillExpReward(p->arena, t, p, pos->bounty, bonus);
					bounty_map.bounty[index] = exp;
				}
				else
				{
					int minBonusPlayers = cfg->GetInt(p->arena->cfg, "Hyperspace", "MinBonusPlayers",  4);
					int bonus = p->arena->playing >= minBonusPlayers;
					int money = calculateKillMoneyReward(p->arena, t, p, pos->bounty, bonus);
					bounty_map.bounty[index] = 	money;
				}
				bounty_map.timeout[index] = gtc + 100;
			}
			pos->bounty = bounty_map.bounty[index];
			return 1;
		}
	}
	return 0;
}

local int periodic_tick(void *clos)
{
	Arena *arena = clos;
	AData *adata = P_ARENA_DATA(arena, adkey);
	Player *p;
	Link *link;
	int reset = 0;

	pd->Lock();
	if (adata->reset)
	{
		reset = 1;
		adata->reset = 0;
		adata->periodic_tally = 0;
	}
	else
	{
		adata->periodic_tally++;
	}

	FOR_EACH_PLAYER(p)
	{
		if(p->arena == arena)
		{
			PData *pdata = PPDATA(p, pdkey);
			if (reset)
			{
				pdata->periodic_tally = 0;
			}
			else
			{
				if (adata->periodic_include_region != NULL)
				{
					if (mapdata->Contains(adata->periodic_include_region, p->position.x >> 4, p->position.y >> 4))
					{
						pdata->periodic_tally++;
					}
				}
				else if (adata->periodic_exclude_region != NULL)
				{
					if (!mapdata->Contains(adata->periodic_exclude_region, p->position.x >> 4, p->position.y >> 4))
					{
						pdata->periodic_tally++;
					}
				}
				else
				{
					pdata->periodic_tally++;
				}
			}
		}
	}
	pd->Unlock();

	return TRUE;
}

local void shipFreqChangeCallback(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	PData *pdata = PPDATA(p, pdkey);
	pdata->periodic_tally = 0;
}

local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
	{
		PData *pdata = PPDATA(p, pdkey);
		pdata->periodic_tally = 0;
	}
}

local int getPeriodicPoints(Arena *arena, int freq, int freqplayers, int totalplayers, int flagsowned)
{
	AData *adata = P_ARENA_DATA(arena, adkey);

	if (adata->periodic_money_formula || adata->periodic_exp_formula)
	{
		char *flagstring;
		int money = 0;
		int exp = 0;
		Player *p;
		Link *link;
		HashTable *vars = HashAlloc();

		FormulaVariable arena_var, freq_var, flags_var;
		arena_var.name = NULL;
		arena_var.type = VAR_TYPE_ARENA;
		arena_var.arena = arena;
		freq_var.name = NULL;
		freq_var.type = VAR_TYPE_FREQ;
		freq_var.freq.arena = arena;
		freq_var.freq.freq = freq; /* lol */
		flags_var.name = NULL;
		flags_var.type = VAR_TYPE_DOUBLE;
		flags_var.value = flagsowned;

		char error_buf[200];
		error_buf[0] = '\0';

		HashAdd(vars, "arena", &arena_var);
		HashAdd(vars, "freq", &freq_var);
		HashAdd(vars, "flags", &flags_var);

		if (adata->periodic_money_formula)
		{
			money = formula->EvaluateFormulaInt(adata->periodic_money_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				money = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with periodic money formula: %s", error_buf);
			}
		}

		if (adata->periodic_exp_formula)
		{
			exp = formula->EvaluateFormulaInt(adata->periodic_exp_formula, vars, NULL, error_buf, sizeof(error_buf), 0);
			if (error_buf[0] != '\0')
			{
				exp = 0;
				lm->LogA(L_WARN, "hscore_rewards", arena, "Error with periodic exp formula: %s", error_buf);
			}
		}

		HashFree(vars);

		if (flagsowned == 1)
		{
			flagstring = "flag";
		}
		else
		{
			flagstring = "flags";
		}

		pd->Lock();
		FOR_EACH_PLAYER(p)
		{
			if(p->arena == arena && p->p_freq == freq && p->p_ship != SHIP_SPEC)
			{
				PData *pdata = PPDATA(p, pdkey);
				int p_money, p_exp;

				if (adata->periodic_tally)
				{
					p_money = (money * pdata->periodic_tally) / adata->periodic_tally;
					p_exp = (exp * pdata->periodic_tally) / adata->periodic_tally;
				}
				else
				{
					p_money = money;
					p_exp = exp;
				}

				int pmul_exp = items->getPropertySum(p, p->p_ship, "exp_multiplier", 100);
				float exp_mul = ((float) pmul_exp / 100.0);
				int pmul_hsd = items->getPropertySum(p, p->p_ship, "hsd_multiplier", 100);
				float hsd_mul = ((float) pmul_hsd / 100.0);

				p_exp *= exp_mul;
				p_money *= hsd_mul;

				database->addMoney(p, MONEY_TYPE_FLAG, p_money);
				database->addExp(p, p_exp);
				if (p_money && p_exp)
				{
					chat->SendMessage(p, "You received $%d and %d exp for holding %d %s.", p_money, p_exp, flagsowned, flagstring);
				}
				else if (p_money)
				{
					chat->SendMessage(p, "You received $%d for holding %d %s.", p_money, flagsowned, flagstring);
				}
				else if (p_exp)
				{
					chat->SendMessage(p, "You received %d exp for holding %d %s.", p_exp, flagsowned, flagstring);
				}
			}
		}

		adata->reset = 1;
		pd->Unlock();

		return money;
	}

	return 0;
}

local void free_formulas(Arena *arena)
{
	AData *adata = P_ARENA_DATA(arena, adkey);

	if (adata->kill_money_formula)
	{
		formula->FreeFormula(adata->kill_money_formula);
		adata->kill_money_formula = NULL;
	}

	if (adata->kill_exp_formula)
	{
		formula->FreeFormula(adata->kill_exp_formula);
		adata->kill_exp_formula = NULL;
	}

	if (adata->kill_jp_formula)
	{
		formula->FreeFormula(adata->kill_jp_formula);
		adata->kill_jp_formula = NULL;
	}

	if (adata->bonus_kill_money_formula)
	{
		formula->FreeFormula(adata->bonus_kill_money_formula);
		adata->bonus_kill_money_formula = NULL;
	}

	if (adata->bonus_kill_exp_formula)
	{
		formula->FreeFormula(adata->bonus_kill_exp_formula);
		adata->bonus_kill_exp_formula = NULL;
	}

	if (adata->flag_money_formula)
	{
		formula->FreeFormula(adata->flag_money_formula);
		adata->flag_money_formula = NULL;
	}

	if (adata->loss_flag_money_formula)
	{
		formula->FreeFormula(adata->loss_flag_money_formula);
		adata->loss_flag_money_formula = NULL;
	}

	if (adata->loss_flag_exp_formula)
	{
		formula->FreeFormula(adata->loss_flag_exp_formula);
		adata->loss_flag_exp_formula = NULL;
	}

	if (adata->flag_exp_formula)
	{
		formula->FreeFormula(adata->flag_exp_formula);
		adata->flag_exp_formula = NULL;
	}

	if (adata->periodic_money_formula)
	{
		formula->FreeFormula(adata->periodic_money_formula);
		adata->periodic_money_formula = NULL;
	}

	if (adata->periodic_exp_formula)
	{
		formula->FreeFormula(adata->periodic_exp_formula);
		adata->periodic_exp_formula = NULL;
	}
}

local void get_formulas(Arena *arena)
{
	AData *adata = P_ARENA_DATA(arena, adkey);
	const char *kill_money, *kill_exp, *kill_jp;
	const char *bonus_kill_money, *bonus_kill_exp;
	const char *flag_money, *loss_flag_money, *flag_exp, *loss_flag_exp;
	const char *periodic_money, *periodic_exp;
	const char *include_rgn, *exclude_rgn, *bonus_rgn;
	char error[200];
	error[0] = '\0';


	// free the formulas if they already exist
	free_formulas(arena);

	kill_money = cfg->GetStr(arena->cfg, "Hyperspace", "KillMoneyFormula");
	kill_exp = cfg->GetStr(arena->cfg, "Hyperspace", "KillExpFormula");
	kill_jp = cfg->GetStr(arena->cfg, "Hyperspace", "KillJackpotFormula");
	bonus_kill_money = cfg->GetStr(arena->cfg, "Hyperspace", "BonusKillMoneyFormula");
	bonus_kill_exp = cfg->GetStr(arena->cfg, "Hyperspace", "BonusKillExpFormula");
	flag_money = cfg->GetStr(arena->cfg, "Hyperspace", "FlagMoneyFormula");
	loss_flag_money = cfg->GetStr(arena->cfg, "Hyperspace", "LossFlagMoneyFormula");
	flag_exp = cfg->GetStr(arena->cfg, "Hyperspace", "FlagExpFormula");
	loss_flag_exp = cfg->GetStr(arena->cfg, "Hyperspace", "LossFlagExpFormula");
	periodic_money = cfg->GetStr(arena->cfg, "Hyperspace", "PeriodicMoneyFormula");
	periodic_exp = cfg->GetStr(arena->cfg, "Hyperspace", "PeriodicExpFormula");

	bonus_rgn = cfg->GetStr(arena->cfg, "Hyperspace", "BonusRegion");
	if (bonus_rgn)
	{
		adata->bonus_region = mapdata->FindRegionByName(arena, bonus_rgn);
	}
	else
	{
		adata->bonus_region = NULL;
	}

	include_rgn = cfg->GetStr(arena->cfg, "Hyperspace", "PeriodicIncludeRegion");
	if (include_rgn)
	{
		adata->periodic_include_region = mapdata->FindRegionByName(arena, include_rgn);
	}
	else
	{
		adata->periodic_include_region = NULL;
	}

	exclude_rgn = cfg->GetStr(arena->cfg, "Hyperspace", "PeriodicExcludeRegion");
	if (exclude_rgn)
	{
		adata->periodic_exclude_region = mapdata->FindRegionByName(arena, exclude_rgn);
	}
	else
	{
		adata->periodic_exclude_region = NULL;
	}

	if (kill_money && *kill_money)
	{
		adata->kill_money_formula = formula->ParseFormula(kill_money, error, sizeof(error));
		if (adata->kill_money_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing kill money reward formula: %s", error);
		}
	}

	if (kill_exp && *kill_exp)
	{
		adata->kill_exp_formula = formula->ParseFormula(kill_exp, error, sizeof(error));
		if (adata->kill_exp_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing kill exp reward formula: %s", error);
		}
	}

	if(kill_jp && *kill_jp) {
		adata->kill_jp_formula = formula->ParseFormula(kill_jp, error, sizeof(error));
		if (adata->kill_jp_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing kill jp reward formula: %s", error);
		}
	}

	if (bonus_kill_money && *bonus_kill_money)
	{
		adata->bonus_kill_money_formula = formula->ParseFormula(bonus_kill_money, error, sizeof(error));
		if (adata->bonus_kill_money_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing bonus kill money reward formula: %s", error);
		}
	}

	if (bonus_kill_exp && *bonus_kill_exp)
	{
		adata->bonus_kill_exp_formula = formula->ParseFormula(bonus_kill_exp, error, sizeof(error));
		if (adata->bonus_kill_exp_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing bonus kill exp reward formula: %s", error);
		}
	}

	if (flag_money && *flag_money)
	{
		adata->flag_money_formula = formula->ParseFormula(flag_money, error, sizeof(error));
		if (adata->flag_money_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing flag money reward formula: %s", error);
		}
	}

	if (loss_flag_money && *loss_flag_money)
	{
		adata->loss_flag_money_formula = formula->ParseFormula(loss_flag_money, error, sizeof(error));
		if (adata->loss_flag_money_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing loss flag money reward formula: %s", error);
		}
	}

	if (loss_flag_exp && *loss_flag_exp)
	{
		adata->loss_flag_exp_formula = formula->ParseFormula(loss_flag_exp, error, sizeof(error));
		if (adata->loss_flag_exp_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing loss flag exp reward formula: %s", error);
		}
	}

	if (flag_exp && *flag_exp)
	{
		adata->flag_exp_formula = formula->ParseFormula(flag_exp, error, sizeof(error));
		if (adata->flag_exp_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing flag exp reward formula: %s", error);
		}
	}

	if (periodic_money && *periodic_money)
	{
		adata->periodic_money_formula = formula->ParseFormula(periodic_money, error, sizeof(error));
		if (adata->periodic_money_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing periodic money reward formula: %s", error);
		}
	}

	if (periodic_exp && *periodic_exp)
	{
		adata->periodic_exp_formula = formula->ParseFormula(periodic_exp, error, sizeof(error));
		if (adata->periodic_exp_formula == NULL)
		{
			lm->LogA(L_WARN, "hscore_rewards", arena, "Error parsing periodic exp reward formula: %s", error);
		}
	}

	for (int i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
	{
		/* cfghelp: All:TeammateReward, arena, int, def: 500, mod: hscore_rewards
		 * The percentage (max) money that a teammate can receive from a kill.
		 * 1000 = 100%*/
		adata->teammate_max[i] = (double)cfg->GetInt(arena->cfg, shipNames[i], "TeammateReward", 500) / 1000.0;
		/* cfghelp: All:DistFalloff, arena, int, def: 1440000, mod: hscore_rewards
		 * Kill reward distance falloff divisor in pixels^2. */
		adata->dist_coeff[i] = (double)cfg->GetInt(arena->cfg, shipNames[i], "DistFalloff", 1440000); // pixels^2
	}
}

local void aaction(Arena *arena, int action)
{
	if (action == AA_CONFCHANGED)
	{
		get_formulas(arena);
	}
}

local void newplayer(Player *p, int isnew)
{
	if (p->pid >= bounty_map.size)
	{
		int i, j;
		int newsize = bounty_map.size * 2;
		short *newbounty = amalloc(sizeof(*bounty_map.bounty) * newsize * newsize);
		ticks_t *newtimeout = amalloc(sizeof(*bounty_map.timeout) * newsize * newsize);
		short *oldbounty = bounty_map.bounty;
		ticks_t *oldtimeout = bounty_map.timeout;

		for (i = 0; i < bounty_map.size; i++)
		{
			for (j = 0; j < bounty_map.size; j++)
			{
				int new_index = i * newsize + j;
				int old_index = i * bounty_map.size + j;
				newbounty[new_index] = oldbounty[old_index];
				newtimeout[new_index] = oldtimeout[old_index];
			}
		}

		bounty_map.bounty = newbounty;
		bounty_map.timeout = newtimeout;
		bounty_map.size = newsize;

		afree(oldbounty);
		afree(oldtimeout);
	}
}

local Iperiodicpoints periodicInterface =
{
	INTERFACE_HEAD_INIT(I_PERIODIC_POINTS, "pp-basic")
	getPeriodicPoints
};

local Appk myadv =
{
	ADVISER_HEAD_INIT(A_PPK)
	NULL, edit_ppk_bounty
};

EXPORT const char info_hscore_rewards[] = "v1.6 D1st0rt, Dr Brain & Ceiu";

EXPORT int MM_hscore_rewards(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		formula = mm->GetInterface(I_FORMULA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);

		if (!lm || !chat || !cfg || !pd || !cmd || !persist || !formula || !aman || !mapdata || !ml || !flagcore || !database)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(persist);
			mm->ReleaseInterface(formula);
			mm->ReleaseInterface(aman);
			mm->ReleaseInterface(mapdata);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(flagcore);
			mm->ReleaseInterface(database);
			mm->ReleaseInterface(items);

			return MM_FAIL;
		}

		// setup the bounty cache
		bounty_map.size = 64;
		bounty_map.bounty = amalloc(sizeof(*bounty_map.bounty) * bounty_map.size * bounty_map.size);
		bounty_map.timeout = amalloc(sizeof(*bounty_map.timeout) * bounty_map.size * bounty_map.size);

		pdkey = pd->AllocatePlayerData(sizeof(PData));
		if (pdkey == -1) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(AData));
		if (adkey == -1) return MM_FAIL;

		mm->RegCallback(CB_NEWPLAYER, newplayer, ALLARENAS);

		persist->RegPlayerPD(&my_persist_data);

		formula->RegPlayerProperty("exp", player_exp_callback);
		formula->RegPlayerProperty("money", player_money_callback);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		formula->UnregPlayerProperty("exp", player_exp_callback);
		formula->UnregPlayerProperty("money", player_money_callback);

		persist->UnregPlayerPD(&my_persist_data);

		mm->UnregCallback(CB_NEWPLAYER, newplayer, ALLARENAS);

		pd->FreePlayerData(pdkey);
		aman->FreeArenaData(adkey);

		afree(bounty_map.bounty);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(formula);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(database);
		mm->ReleaseInterface(items);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		AData *adata = P_ARENA_DATA(arena, adkey);
		mm->RegInterface(&periodicInterface, arena);
		mm->RegAdviser(&myadv, arena);

		adata->on = 1;
		adata->kill_money_formula = NULL;
		adata->kill_exp_formula = NULL;
		adata->flag_money_formula = NULL;
		adata->flag_exp_formula = NULL;
		adata->periodic_money_formula = NULL;
		adata->periodic_exp_formula = NULL;
		adata->winning_freq = -1;
		get_formulas(arena);

		mm->RegCallback(CB_WARZONEWIN, flagWinCallback, arena);
		mm->RegCallback(CB_KILL, killCallback, arena);
		mm->RegCallback(CB_ARENAACTION, aaction, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->RegCallback(CB_PLAYERACTION, paction, arena);

		cmd->AddCommand("killmessages", Ckillmessages, arena, killmessages_help);
		cmd->AddCommand("bountytype", Cbountytype, arena, bountytype_help);

		adata->reset = 1;
		ml->SetTimer(periodic_tick, 0, 100, arena, arena);
		ml->SetTimer(flag_reward_timer, 3000, 3000, arena, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		AData *adata = P_ARENA_DATA(arena, adkey);
		mm->UnregInterface(&periodicInterface, arena);
		mm->UnregAdviser(&myadv, arena);

		cmd->RemoveCommand("killmessages", Ckillmessages, arena);
		cmd->RemoveCommand("bountytype", Cbountytype, arena);

		mm->UnregCallback(CB_WARZONEWIN, flagWinCallback, arena);
		mm->UnregCallback(CB_KILL, killCallback, arena);
		mm->UnregCallback(CB_ARENAACTION, aaction, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->UnregCallback(CB_PLAYERACTION, paction, arena);

		ml->ClearTimer(periodic_tick, arena);
		ml->ClearTimer(flag_reward_timer, arena);

		adata->on = 0;
		free_formulas(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
