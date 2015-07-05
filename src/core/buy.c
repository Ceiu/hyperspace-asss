
/* dist: public */

#include <string.h>

#include "asss.h"


local Iconfig *cfg;
local Iplayerdata *pd;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmd;
local Istats *stats;
local Ichat *chat;
local Ilogman *lm;

local const struct
{
	const char *string;
	const char *setting;
	int type;
}
items[] =
{
	/* cfghelp: Cost:XRadar, arena, int, def: 0
	 * Points cost for XRadar. 0 to disallow purchase. */
	{ "x",        "XRadar",     6 },
	/* cfghelp: Cost:Recharge, arena, int, def: 0
	 * Points cost for Recharge Upgrade. 0 to disallow purchase. */
	{ "recharge", "Recharge",   1 },
	/* cfghelp: Cost:Energy, arena, int, def: 0
	 * Points cost for Energy Upgrade. 0 to disallow purchase. */
	{ "energy",   "Energy",     2 },
	/* cfghelp: Cost:Rotation, arena, int, def: 0
	 * Points cost for Rotation Upgrade. 0 to disallow purchase. */
	{ "rot",      "Rotation",   3 },
	/* cfghelp: Cost:Stealth, arena, int, def: 0
	 * Points cost for Stealth Ability. 0 to disallow purchase. */
	{ "stealth",  "Stealth",    4 },
	/* cfghelp: Cost:Cloak, arena, int, def: 0
	 * Points cost for Cloak Ability. 0 to disallow purchase. */
	{ "cloak",    "Cloak",      5 },
	/* cfghelp: Cost:Gun, arena, int, def: 0
	 * Points cost for Gun Upgrade. 0 to disallow purchase. */
	{ "gun",      "Gun",        8 },
	/* cfghelp: Cost:Bomb, arena, int, def: 0
	 * Points cost for Bomb Upgrade. 0 to disallow purchase. */
	{ "bomb",     "Bomb",       9 },
	/* cfghelp: Cost:Bounce, arena, int, def: 0
	 * Points cost for Bouncing Bullets. 0 to disallow purchase. */
	{ "bounce",   "Bounce",    10 },
	/* cfghelp: Cost:Thrust, arena, int, def: 0
	 * Points cost for Thrust Upgrade. 0 to disallow purchase. */
	{ "thrust",   "Thrust",    11 },
	/* cfghelp: Cost:Speed, arena, int, def: 0
	 * Points cost for Top Speed. 0 to disallow purchase. */
	{ "speed",    "Speed",     12 },
	/* cfghelp: Cost:MultiFire, arena, int, def: 0
	 * Points cost for MultiFire. 0 to disallow purchase. */
	{ "multi",    "MultiFire", 15 },
	/* cfghelp: Cost:Prox, arena, int, def: 0
	 * Points cost for Proximity Bombs. 0 to disallow purchase. */
	{ "prox",     "Prox",      16 },
	/* cfghelp: Cost:Super, arena, int, def: 0
	 * Points cost for Super. 0 to disallow purchase. */
	{ "super",    "Super",     17 },
	/* cfghelp: Cost:Shield, arena, int, def: 0
	 * Points cost for Shields. 0 to disallow purchase. */
	{ "shield",   "Shield",    18 },
	/* cfghelp: Cost:Shrap, arena, int, def: 0
	 * Points cost for Shrapnel Upgrade. 0 to disallow purchase. */
	{ "shrap",    "Shrap",     19 },
	/* cfghelp: Cost:AntiWarp, arena, int, def: 0
	 * Points cost for AntiWarp Ability. 0 to disallow purchase. */
	{ "anti",     "AntiWarp",  20 },
	/* cfghelp: Cost:Repel, arena, int, def: 0
	 * Points cost for Repel. 0 to disallow purchase. */
	{ "rep",      "Repel",     21 },
	/* cfghelp: Cost:Burst, arena, int, def: 0
	 * Points cost for Burst. 0 to disallow purchase. */
	{ "burst",    "Burst",     22 },
	/* cfghelp: Cost:Decoy, arena, int, def: 0
	 * Points cost for Decoy. 0 to disallow purchase. */
	{ "decoy",    "Decoy",     23 },
	/* cfghelp: Cost:Thor, arena, int, def: 0
	 * Points cost for Thor. 0 to disallow purchase. */
	{ "thor",     "Thor",      24 },
	/* cfghelp: Cost:Brick, arena, int, def: 0
	 * Points cost for Brick. 0 to disallow purchase. */
	{ "brick",    "Brick",     26 },
	/* cfghelp: Cost:Rocket, arena, int, def: 0
	 * Points cost for Rocket. 0 to disallow purchase. */
	{ "rocket",   "Rocket",    27 },
	/* cfghelp: Cost:Portal, arena, int, def: 0
	 * Points cost for Portal. 0 to disallow purchase. */
	{ "port",     "Portal",    28 },
};


local void print_costs(ConfigHandle ch, Player *p)
{
	int i, avail = 0;

	for (i = 0; i < sizeof(items)/sizeof(items[0]); i++)
	{
		int cost = cfg->GetInt(ch, "Cost", items[i].setting, 0);
		if (cost)
		{
			chat->SendMessage(p, "buy: %-9s %6d", items[i].setting, cost);
			avail++;
		}
	}

	if (avail == 0)
		chat->SendMessage(p, "There are no items available to purchase in this arena.");
}


local void Cbuy(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	ConfigHandle ch = arena->cfg;

	if (!arena) return;

	if (params[0] == 0)
		print_costs(ch, p);
	else
	{
		int i, item = -1;
		for (i = 0; i < sizeof(items)/sizeof(items[0]); i++)
			if (strstr(params, items[i].string))
				item = i;

		if (item == -1)
			chat->SendMessage(p, "Invalid item specified for purchase.");
		else
		{
			int cost = cfg->GetInt(ch, "Cost", items[item].setting, 0);
			if (cost == 0)
				chat->SendMessage(p, "That item isn't available for purchase.");
			else
			{
				int pts, anywhere;

				if (p->p_ship == SHIP_SPEC)
				{
					chat->SendMessage(p, "Spectators cannot purchase items.");
					return;
				}

				/* cfghelp: Cost:PurchaseAnytime, arena, bool, def: 0
				 * Whether players can buy items outside a safe zone. */
				anywhere = cfg->GetInt(ch, "Cost", "PurchaseAnytime", 0);

				if (!anywhere && !(p->position.status & 0x20))
				{
					chat->SendMessage(p, "You must be in a safe zone to purchase items.");
					return;
				}

				pts = stats->GetStat(p, STAT_KILL_POINTS, INTERVAL_RESET) +
				      stats->GetStat(p, STAT_FLAG_POINTS, INTERVAL_RESET);
				if (pts < cost)
					chat->SendMessage(p, "You don't have enough points to purchase that item.");
				else
				{
					Target t;
					t.type = T_PLAYER;
					t.u.p = p;
					/* deduct from flag points to keep kill average the same. */
					stats->IncrementStat(p, STAT_FLAG_POINTS, -cost);
					stats->SendUpdates(NULL);
					game->GivePrize(&t, items[item].type, 1);
					chat->SendMessage(p, "Bought %s.", items[item].setting);
					if (lm) lm->LogP(L_DRIVEL, "buy", p, "bought %s", items[item].setting);
				}
			}
		}
	}
}

EXPORT const char info_buy[] = CORE_MOD_INFO("buy");

EXPORT int MM_buy(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(stats);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		cmd->AddCommand("buy", Cbuy, arena, NULL);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		cmd->RemoveCommand("buy", Cbuy, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

