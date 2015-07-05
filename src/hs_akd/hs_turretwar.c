#include "asss.h"

#define MODULENAME hs_turretwar
#define SZMODULENAME "hs_turretwar"
#define INTERFACENAME Ihs_turretwar

#include "akd_asss.h"
#include "clientset.h"
#include "hscore.h"
#include <string.h>


//other interfaces we want to use besides the usual
local Ihscoredatabase *database;
local Iclientset *cs;

//other globals
local pthread_mutex_t globalmutex;

typedef enum hs_turretwar_state
{
	tws_off,
	tws_paused,
	tws_on
} hs_turretwar_state;


DEF_PARENA_TYPE
	hs_turretwar_state state;
	Player **captains;
	int *score;
	int *recentKills;
	Player *HRP;

	i8 defaultTeams;
	i8 teams;

	override_key_t kcg,kct,kcb,kcm;
	override_key_t kpg,kpt,kpb,kpm;

	int time;
	int scoreLimit;

	short killedPlayerReward;
	short killedCaptainReward;
	short killBonusMultiplier;

	int captainShip : 4;
	int playerShip : 4;

	int disabledWeapons : 2;

ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
	int score;
	i8 respawning;
ENDDEF_PPLAYER_TYPE;

//prototype internal functions here.

typedef int (*captain_func)(Arena *, Player **, const void *extra);
local Player **captain_iterate(Arena *, captain_func, const void *extra);

local int findCaptainSpot(Arena *, Player **, const void *nothing);
local int checkCaptain(Arena *, Player **, const void *player);
local int removeCaptain(Arena *, Player **, const void *player);

local void checkHRP(Arena *, Player *);

local void forceAttach(Player *turret, Player *base);
local void forceOthersToAttach(Player *base);

local void sendSettings(Arena *a);
local void disableWeapons(Arena *);
local void enableWeapons(Arena *);
local void fixShips(Arena *);

local void endGame(Arena *);

local helptext_t newturretwar_help =
"Targets: arena\n"
"Syntax:\n"
"  ?turretwar [teams]\n"
"Starts a new turret war game.\n"
"If you specify no teams value, 2 teams will be assumed.\n";
local void Cnewturretwar(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t pauseunpause_help =
"Targets: arena\n"
"Syntax:\n"
"  ?pause\n"
"  ?unpause\n"
"Pauses/unpauses the current turretwar game.\n";
local void Cpause(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t captain_help =
"Targets: arena, player\n"
"Syntax:\n"
"  ?captain <name>\n"
" -or- /?captain\n"
"Sets a player as a captain of turrets on the next available team.\n";
local void Ccaptain(const char *cmd, const char *params, Player *p, const Target *target);

local helptext_t set_help =
"Targets: arena\n"
"Syntax:\n"
"  ?settimelimit <seconds>\n"
"  ?setscorelimit <points>\n"
"  ?setcaptainship <1-8>\n"
"  ?setplayership <1-8>\n"
"Sets various hs_turretwar settings temporarily.\n";
local void Cset(const char *cmd, const char *params, Player *p, const Target *target);

//callbacks
local void shipfreqchange(Player *, int newship, int oldship, int newfreq, int oldfreq);
local void playeraction(Player *, int action, Arena *);
local void playerkill(Arena *, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);

local void playerattach(Player *p, byte *pkt, int len);

//timers
local int perSecond(void *a);
local int respawn(void *p);

EXPORT const char info_hs_turretwar[] = "v1.0 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_hs_turretwar(int action, Imodman *mm_, Arena *arena)
{
	MM_FUNC_HEADER();

	if (action == MM_LOAD)
	{
		mm = mm_;

		GET_USUAL_INTERFACES();
		GETINT(database, I_HSCORE_DATABASE);
		GETINT(cs, I_CLIENTSET);

		REG_PARENA_DATA();
		BREG_PPLAYER_DATA();

		INIT_MUTEX(globalmutex);

		net->AddPacket(C2S_ATTACHTO, playerattach);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		ml->ClearTimer(perSecond, 0);

		net->RemovePacket(C2S_ATTACHTO, playerattach);

		DESTROY_MUTEX(globalmutex);

		UNREG_PARENA_DATA();
		UNREG_PPLAYER_DATA();

Lfailload:
		RELEASE_USUAL_INTERFACES();
		RELEASEINT(database);
		RELEASEINT(cs);

		DO_RETURN();
	}


	else if (action == MM_ATTACH)
	{

		/* cfghelp: TurretWar:CaptainShip, arena, int, def: 7, mod: hs_turretwar
		 * The default ship for turretwar captains to use. */

		/* cfghelp: TurretWar:PlayerShip, arena, int, def: 8, mod: hs_turretwar
		 * The default ship for turretwar turrets to use. */

		/* cfghelp: TurretWar:KilledPlayerReward, arena, int, def: 10, mod: hs_turretwar
		 * How many points gotten for killing a turret. */

		/* cfghelp: TurretWar:KilledCaptainReward, arena, int, def: 50, mod: hs_turretwar
		 * How many points gotten for killing a captain. */

		/* cfghelp: TurretWar:KillBonusMultiplier, arena, int, def: 1, mod: hs_turretwar
		 * Amount to multiply the streak bonus by. */
		ALLOC_ARENA_DATA(ad);

		ad->defaultTeams = cfg->GetInt(arena->cfg, "team", "desiredteams", 2);
		ad->captainShip = cfg->GetInt(arena->cfg, "turretwar", "captainship", 7) - 1;
		ad->playerShip = cfg->GetInt(arena->cfg, "turretwar", "playership", 8) - 1;

		ad->killedPlayerReward = cfg->GetInt(arena->cfg, "turretwar", "killedplayerreward", 10);
		ad->killedCaptainReward = cfg->GetInt(arena->cfg, "turretwar", "killedcaptainreward", 50);
		ad->killBonusMultiplier = cfg->GetInt(arena->cfg, "turretwar", "killbonusmultiplier", 1);

		ad->teams = 0;
		ad->captains = 0;
		ad->score = 0;
		ad->recentKills = 0;
		ad->HRP = 0;
		ad->state = tws_off;

		ad->kcg = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "bulletfireenergy");
		ad->kct = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "multifireenergy");
		ad->kcb = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "bombfireenergy");
		ad->kcm = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "landminefireenergy");
		ad->kpg = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "bulletfireenergy");
		ad->kpt = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "multifireenergy");
		ad->kpb = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "bombfireenergy");
		ad->kpm = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "landminefireenergy");

		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_PLAYERACTION, playeraction, arena);
		mm->RegCallback(CB_KILL, playerkill, arena);

		cmd->AddCommand("pause", Cpause, arena, pauseunpause_help);
		cmd->AddCommand("unpause", Cpause, arena, pauseunpause_help);
		cmd->AddCommand("newturretwar", Cnewturretwar, arena, newturretwar_help);
		cmd->AddCommand("captain", Ccaptain, arena, captain_help);
		cmd->AddCommand("settimelimit", Cset, arena, set_help);
		cmd->AddCommand("setscorelimit", Cset, arena, set_help);
		cmd->AddCommand("setcaptainship", Cset, arena, set_help);
		cmd->AddCommand("setplayership", Cset, arena, set_help);


		ml->SetTimer(perSecond, 100, 100, arena, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		Link *link;
		Player *p;

		cmd->RemoveCommand("pause", Cpause, arena);
		cmd->RemoveCommand("unpause", Cpause, arena);
		cmd->RemoveCommand("newturretwar", Cnewturretwar, arena);
		cmd->RemoveCommand("captain", Ccaptain, arena);
		cmd->RemoveCommand("settimelimit", Cset, arena);
		cmd->RemoveCommand("setscorelimit", Cset, arena);
		cmd->RemoveCommand("setcaptainship", Cset, arena);
		cmd->RemoveCommand("setplayership", Cset, arena);

		ml->ClearTimer(perSecond, arena);
		PDLOCK;
		FOR_EACH_PLAYER(p)
		{
			if (!IS_IN(p, arena))
				continue;
			ml->ClearTimer(respawn, p);
		}
		PDUNLOCK;

		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_PLAYERACTION, playeraction, arena);
		mm->UnregCallback(CB_KILL, playerkill, arena);

		MYGLOCK;
		if (ad->teams)
		{
			afree(ad->captains);
			afree(ad->score);
			afree(ad->recentKills);
			ad->teams = 0;
		}
		MYGUNLOCK;

//Lfailattach:
		FREE_ARENA_DATA(ad);

		DO_RETURN();
	}


	return MM_FAIL;
}

local Player **captain_iterate(Arena *a, captain_func func, const void *extra)
{
	DEF_AD(a);
	int i;
	Player **result = 0;

	if (!ad)
		return 0;

	MYGLOCK;
	for (i = 0; i < ad->teams; ++i)
	{
		int found;
		found = func(a, &ad->captains[i], extra);
		if (found)
		{
			result = &ad->captains[i];
			break;
		}
	}
	MYGUNLOCK;

	return result;
}

local int findCaptainSpot(Arena *a, Player **p, const void *nothing)
{
	if (!*p)
		return 1;
	return 0;
}

local int checkCaptain(Arena *a, Player **p, const void *player)
{
	if (*p == player)
		return 1;
	return 0;
}

local int removeCaptain(Arena *a, Player **p, const void *player)
{
	if (*p == player)
	{
		*p = 0;
		return 1;
	}
	return 0;
}

local void checkHRP(Arena *a, Player *p)
{
	DEF_AD(a);
	Link *link;
	Player *x;
	Player *hrp;
	int high;

	if (!ad)
		return;

	/*
	if a player was specified, we are only checking to see if someone has more points than them, if no one does then they will
	be set as the hrp. if no player is specified, recalculate the hrp from scratch.
	*/
	if (p)
	{
		BDEF_PD(p);
		hrp = p;
		high = pdat->score;
	}
	else
	{
		hrp = 0;
		high = 0;
	}

	MYGLOCK;

	PDLOCK;
	FOR_EACH_PLAYER(x)
	{
		BDEF_PD_ALT(x, xdat);
		if (!IS_IN(x, a))
			continue;

		if (IS_SPEC(x))
			continue;

		if (x->p_freq >= ad->teams)
			continue;

		if (!hrp)
		{
			high = xdat->score;
			hrp = x;
		}
		else if (xdat->score > high)
		{
			if (p)
			{
				break;
			}
			else
			{
				high = xdat->score;
				hrp = x;
			}
		}
	}
	PDUNLOCK;

	ad->HRP = hrp;
	MYGUNLOCK;
}

local void forceAttach(Player *turret, Player *base)
{
	BDEF_PD(base);
	struct SimplePacket attach = { S2C_TURRET, turret->pid, base?base->pid:-1 };

	if (base && pdat->respawning)
		return;

	if (turret == base)
		return;

	if (turret->p_attached == attach.d2)
		return;

	net->SendToArena(turret->arena, NULL, (byte*)&attach, 5, NET_RELIABLE);
	turret->p_attached = attach.d2;
}

local void forceOthersToAttach(Player *base)
{
	Player *x;
	Link *link;
	int freq = base->p_freq;

	PDLOCK;
	FOR_EACH_PLAYER(x)
	{
		if (!IS_PLAYING(x, base->arena))
			continue;
		if (x->p_freq != freq)
			continue;
		if (x != base)
			forceAttach(x, base);
	}
	PDUNLOCK;
}

local void sendSettings(Arena *a)
{
	Player *p;
	Link *link;

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		if (IS_IN(p, a))
		{
			cs->SendClientSettings(p);
		}
	}
	PDUNLOCK;
}

local void disableWeapons(Arena *a)
{
	DEF_AD(a);
	DEF_T_A(a);

	if (!ad)
		return;

	ad->disabledWeapons = 1;

	cs->ArenaOverride(a, ad->kcg, 32000);
	cs->ArenaOverride(a, ad->kct, 32000);
	cs->ArenaOverride(a, ad->kcb, 32000);
	cs->ArenaOverride(a, ad->kcm, 32000);
	cs->ArenaOverride(a, ad->kpg, 32000);
	cs->ArenaOverride(a, ad->kpt, 32000);
	cs->ArenaOverride(a, ad->kpb, 32000);
	cs->ArenaOverride(a, ad->kpm, 32000);
	sendSettings(a);

	game->GivePrize(&t, -PRIZE_SHUTDOWN, 1);
	game->GivePrize(&t, -PRIZE_BURST, 10);
	game->GivePrize(&t, -PRIZE_THOR, 10);

}

local void enableWeapons(Arena *a)
{
	DEF_AD(a);

	if (!ad)
		return;

	ad->disabledWeapons = 0;

	cs->ArenaUnoverride(a, ad->kcg);
	cs->ArenaUnoverride(a, ad->kct);
	cs->ArenaUnoverride(a, ad->kcb);
	cs->ArenaUnoverride(a, ad->kcm);
	cs->ArenaUnoverride(a, ad->kpg);
	cs->ArenaUnoverride(a, ad->kpt);
	cs->ArenaUnoverride(a, ad->kpb);
	cs->ArenaUnoverride(a, ad->kpm);
	sendSettings(a);
}

local void fixShips(Arena *a)
{
	DEF_AD(a);
	Link *link;
	Player *p;

	if (!ad)
		return;

	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		int freq = p->p_freq;

		if (!IS_IN(p, a))
			continue;
		if (IS_SPEC(p))
			continue;
		if (freq >= ad->teams)
			game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
		if (ad->captains[freq] == p)
		{
			if (p->p_ship != ad->captainShip)
				game->SetShip(p, ad->captainShip);
		}
		else if (p->p_ship != ad->playerShip)
		{
			game->SetShip(p, ad->playerShip);
		}
	}
	PDUNLOCK;
}

local void endGame(Arena *a)
{
	DEF_AD(a);
	int i;
	int best = -1;
	int winner = 0;
	int tie = 0;
	Player *p;
	Link *link;
	int hrpfreq = ad->HRP?ad->HRP->p_freq:0;

	if (!ad)
		return;

	if (!ad->teams)
	{
		return;
	}

	cfg->SetInt(a->cfg, "team", "desiredteams", ad->defaultTeams, "", 0);

	chat->SendArenaSoundMessage(a, 103, "GAME OVER!");

	chat->SendArenaMessage(a, "Final Scores:");

	MYGLOCK;
	for (i = 0; i < ad->teams; ++i)
	{
		if (ad->score[i] > best)
		{
			best = ad->score[i];
			winner = i;
			tie = 0;
		}
		else if (ad->score[i] == best)
		{
			tie = 1;
		}

		chat->SendArenaMessage(a, "  %s - Freq %i: %i", ad->captains[i]?ad->captains[i]->name:"-no captain-", i, ad->score[i]);
	}

	if (!tie)
	{
		chat->SendArenaMessage(a, "Freq %i wins!", winner);
		if (ad->HRP && ((hrpfreq >= 0) && (hrpfreq < ad->teams)))
		{
			BDEF_PD(ad->HRP);
			chat->SendArenaMessage(a, "HRP: %s - Total reward: $%i", ad->HRP->name, pdat->score + ad->score[hrpfreq]);
		}
	}
	else
	{
		chat->SendArenaMessage(a, "Tie game!");
		if (ad->HRP && ((hrpfreq >= 0) && (hrpfreq < ad->teams)))
		{
			BDEF_PD(ad->HRP);
			chat->SendArenaMessage(a, "HRP: %s - Total reward: $%i", ad->HRP->name, pdat->score + ad->score[hrpfreq]);
		}
	}

	/*
	give team points + personal points in money to the winners, and personal points to the losers
	*/
	PDLOCK;
	FOR_EACH_PLAYER(p)
	{
		BDEF_PD(p);
		int freq = p->p_freq;
		if (!IS_PLAYING(p, a))
			continue;
		if (freq >= ad->teams)
			continue;

		if ((ad->score[freq] == best) || (p == ad->HRP))
		{
			if (tie && (p != ad->HRP))
			{
				chat->SendMessage(p, "Your reward: $%i", pdat->score + (ad->score[freq] / 2));
				database->addMoney(p, MONEY_TYPE_EVENT, pdat->score + (ad->score[freq] / 2));
			}
			else
			{
				chat->SendMessage(p, "Your reward: $%i", pdat->score + ad->score[freq]);
				database->addMoney(p, MONEY_TYPE_EVENT, pdat->score + ad->score[freq]);
			}
		}
		else
		{
			chat->SendMessage(p, "Your reward: $%i", pdat->score);
			database->addMoney(p, MONEY_TYPE_EVENT, pdat->score);
		}
	}
	PDUNLOCK;

	afree(ad->captains);
	afree(ad->score);
	afree(ad->recentKills);
	ad->teams = 0;
	ad->HRP = 0;
	ad->state = tws_off;

	MYGUNLOCK;
}

local int perSecond(void *arena)
{
	Arena *a = (Arena *)arena;
	DEF_AD(a);
	int i;

	if (!ad)
		return 0;

	MYGLOCK;
	for (i = 0; i < ad->teams; ++i)
	{
		if (--ad->recentKills[i] < 0)
			ad->recentKills[i] = 0;
	}

	if (!ad->scoreLimit && (ad->state == tws_on))
	{
		int mins;
		if (!(ad->time--))
		{
			endGame(a);
		}
		else
		{
			mins = (ad->time + 1) / 60;

			if (!(ad->time % 60) && (mins % 3 == 1))
			{
				chat->SendArenaMessage(a, "%i %s", mins, (mins==1)?"minute remains":"minutes remain");
			}
		}
	}

	MYGUNLOCK;

	return 1;
}

local int respawn(void *player)
{
	Player *p = (Player *)player;
	int freq = p->p_freq;
	DEF_AD(p->arena);
	BDEF_PD(p);

	pdat->respawning = 0;

	if (!ad)
		return 0;

	if (!p->arena)
		return 0;

	if (freq >= ad->teams)
		return 0;

	/*
	if this is a normal player, attach them to their captain, otherwise attach all players on their team to them
	*/
	if (p != ad->captains[freq])
	{
		forceAttach(p, ad->captains[freq]);
	}
	else
	{
		forceOthersToAttach(p);
	}

	return 0;
}

local void shipchange(Player *p, int newship, int newfreq)
{
	DEF_AD(p->arena);

	if (!ad)
		return;

	if (!ad->teams)
		return;

	//if they are going to spec, make sure they are removed as a captain
	if (newship == SHIP_SPEC)
	{
		ml->ClearTimer(respawn, p);
		captain_iterate(p->arena, removeCaptain, p);
		if (ad->HRP == p)
		{
			ad->HRP = 0;
			checkHRP(p->arena, 0);
		}
		return;
	}

	//don't let them on an invalid team
	if (newfreq >= ad->teams)
	{
		game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
		return;
	}

	/*
	if they are a captain, make sure they are in the captain ship and reattach everyone,
	if they are not a captain, make sure they are in the player ship and attach them to the captain
	*/
	if (captain_iterate(p->arena, checkCaptain, p))
	{
		if (newship != ad->captainShip)
			game->SetShip(p, ad->captainShip);
		forceOthersToAttach(p);
		checkHRP(p->arena, p);
	}
	else
	{
		if (newship != ad->playerShip)
			game->SetShip(p, ad->playerShip);
		forceAttach(p, ad->captains[newfreq]);
		checkHRP(p->arena, p);
	}
}

local void freqchange(Player *p, int newfreq)
{
	DEF_AD(p->arena);
	if (!ad)
		return;

	captain_iterate(p->arena, removeCaptain, p);

	if (newfreq >= ad->teams)
	{
		if (newfreq != p->arena->specfreq)
			game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
	}
	else
	{
		game->SetShip(p, ad->playerShip);
	}
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	if (newship != oldship)
	{
		shipchange(p, newship, newfreq);
	}
	else
	{
		freqchange(p, newfreq);
	}
}

local void playeraction(Player *p, int action, Arena *a)
{
	BDEF_PD(p);

	if ((p->type != T_VIE) && (p->type != T_CONT))
		return;

	//actions applying to an arena.
	if (a)
	{
		DEF_AD(a);
		if (!ad)
			return;

		if (action == PA_ENTERARENA)
		{
			pdat->score = 0;
			if (ad->state != tws_off)
				game->SetShipAndFreq(p, SHIP_SPEC, a->specfreq);
		}
		else if (action == PA_LEAVEARENA)
		{
			//generic cleanup...

			ml->ClearTimer(respawn, p);

			captain_iterate(a, removeCaptain, p);

			if (ad->HRP == p)
			{
				ad->HRP = 0;
				checkHRP(a, 0);
			}
		}
	}
}


local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	DEF_AD(a);
	BDEF_PD(killer);

	int reward;
	int respawntime = cfg->GetInt(a->cfg, "kill", "enterdelay", 0);

	int freq = killer->p_freq;
	if (!ad)
		return;
	if (ad->state != tws_on)
		return;

	if (freq >= ad->teams)
	{
		*pts = 0;
		return;
	}

	//no reward for a teamkill.
	if (killed->p_freq == killer->p_freq)
	{
		*pts = 0;
		return;
	}

	reward = ad->recentKills[freq] * ad->killBonusMultiplier;

	if (killed->p_ship == ad->captainShip)
	{
		reward += ad->killedCaptainReward;
	}
	else
	{
		reward += ad->killedPlayerReward;
	}

	*pts = reward;

	if ((ad->score[freq] % 1000) > ((ad->score[freq] + reward) % 1000))
	{
		chat->SendArenaMessage(a, "Freq %i (%s) Score: %i", freq, ad->captains[freq]?ad->captains[freq]->name:"-no captain-", ad->score[freq] + reward);
	}

	ad->score[freq] += reward;
	++ad->recentKills[freq];
	pdat->score += reward;
	checkHRP(a, killer);
	if (ad->scoreLimit && (ad->score[freq] >= ad->scoreLimit))
	{
		endGame(a);
	}
	else
	{
		BDEF_PD_ALT(killed, pdat2);
		pdat2->respawning = 1;
		ml->SetTimer(respawn, respawntime, respawntime, killed, killed);
	}
}

local void playerattach(Player *p, byte *pkt, int len)
{
	int pid = ((struct SimplePacket*)pkt)->d1;
	int freq = p->p_freq;
	DEF_AD(p->arena);
	if (!ad)
		return;

	if (!ad->teams)
		return;
	if (freq >= ad->teams)
		return;

	if (pid == -1)
		forceAttach(p, ad->captains[freq]);
}

local void Cnewturretwar(const char *cmd, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);
	int teams;
	Player *x;
	Link *link;

	DEF_T_A(p->arena);
	if (!ad)
		return;
	if (ad->state != tws_off)
	{
		chat->SendMessage(p, "There is already a game running.");
		return;
	}

	disableWeapons(p->arena);

	chat->SendArenaMessage(p->arena, "A new turret war is beginning!");

	teams = atoi(params);
	if (teams < 2)
		teams = 2;
	if (teams > 99)
		teams = 99;

	cfg->SetInt(p->arena->cfg, "team", "desiredteams", teams, "", 0);

	MYGLOCK;
	ad->teams = teams;
	ad->captains = amalloc(sizeof(Player *) * teams);

	ad->score = amalloc(sizeof(int) * teams);
	ad->recentKills = amalloc(sizeof(int) * teams);

	/* cfghelp: TurretWar:ScoreLimit, arena, int, def: 0, mod: hs_turretwar
	 * If non-zero, sets turretwar games to be decided by reaching the specified
	 * team score first. If zero, the game will use a time limit based on TurretWar:TimeLimit */

	/* cfghelp: TurretWar:TimeLimit, arena, int, def: 600, mod: hs_turretwar
	 * If ScoreLimit is zero, sets turretwar games to be decided by having the most points
	 * after the specified number of seconds. */
	ad->scoreLimit = cfg->GetInt(p->arena->cfg, "turretwar", "scorelimit", 0);
	ad->time = ad->scoreLimit?0:cfg->GetInt(p->arena->cfg, "turretwar", "timelimit", 600);
	ad->HRP = 0;
	MYGUNLOCK;

	game->Lock(&t, 0, 1, 0);
	game->Unlock(&t, 0);

	PDLOCK;
	FOR_EACH_PLAYER(x)
	{
		BDEF_PD(x);
		if (!IS_IN(x, p->arena))
			continue;
		pdat->score = 0;
	}
	PDUNLOCK;
}

local void Ccaptain(const char *cmd, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	Player *t;
	Player **spot;
	if (!ad)
		return;
	if (target->type == T_PLAYER)
	{
		t = target->u.p;
	}
	else
	{
		t = pd->FindPlayer(params);
	}

	if (!t)
	{
		chat->SendMessage(p, "Player not found.");
		return;
	}

	MYGLOCK;
	if ((spot = captain_iterate(p->arena, findCaptainSpot, 0)))
	{
		int freq;

		captain_iterate(p->arena, removeCaptain, t);
		*spot = t;

		/*
		determine the freq by determining the difference between the returned address and the first address,
		using lovely pointer subtraction.
		*/
		freq = (int)(spot - ad->captains);

		if ((freq < 0) || (freq >= ad->teams))
		{
			lm->LogA(L_ERROR, "hs_turretwar", p->arena, "findCaptainSpot callback returned invalid address (pointing to freq %i, max freq is %i)", freq, ad->teams);
		}
		else
		{
			game->SetShipAndFreq(t, ad->captainShip, freq);
		}

		chat->SendArenaMessage(p->arena, " ** %s appoints %s captain of freq %i.", p->name, t->name, freq);
	}
	else
	{
		if (p == t)
			chat->SendMessage(p, "There are no remaining captain spots for you.");
		else
			chat->SendMessage(p, "There are no remaining captain spots for %s.", t->name);
	}
	MYGUNLOCK;
}

local void Cpause(const char *cmd, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);
	if (!ad)
		return;
	if (!ad->teams)
	{
		chat->SendMessage(p, "There is no game right now. Use ?newturretwar to begin a game.");
		return;
	}

	if (!strcasecmp(cmd, "unpause"))
	{
		DEF_T_A(p->arena);

		enableWeapons(p->arena);
		if (ad->state == tws_paused)
			chat->SendArenaMessage(p->arena, " ** %s unpauses the game.", p->name);
		else
			chat->SendArenaMessage(p->arena, " ** %s starts the game!", p->name);
		fixShips(p->arena);
		ad->state = tws_on;
		game->GivePrize(&t, PRIZE_WARP, 1);
	}
	else if (!strcasecmp(cmd, "pause"))
	{
		disableWeapons(p->arena);
		chat->SendArenaMessage(p->arena, " ** %s pauses the game.", p->name);
		ad->state = tws_paused;
	}
}

local void Cset(const char *cmd, const char *params, Player *p, const Target *target)
{
	DEF_AD(p->arena);

	MYGLOCK;
	if (!strcasecmp(cmd, "settimelimit"))
	{
		ad->time = atoi(params);
		if (ad->time <= 0)
			ad->time = 1;
		ad->scoreLimit = 0;
		chat->SendArenaMessage(p->arena, " ** %s sets a time limit of %i:%02i", p->name, ad->time/60, ad->time%60);
	}
	else if (!strcasecmp(cmd, "setscorelimit"))
	{
		ad->time = 0;
		ad->scoreLimit = atoi(params);
		if (ad->scoreLimit <= 0)
			ad->scoreLimit = 1;
		chat->SendArenaMessage(p->arena, " ** %s sets a score limit of %i", p->name, ad->scoreLimit);
	}
	else if (!strcasecmp(cmd, "setcaptainship"))
	{
		int captainShip = atoi(params) - 1;
		if ((captainShip < SHIP_WARBIRD) || (captainShip >= SHIP_SPEC))
		{
			chat->SendMessage(p, "Invalid input.");
		}
		else
		{
			ad->captainShip = captainShip;
			chat->SendArenaMessage(p->arena, " ** %s sets the captain ship to %s", p->name, cfg->SHIP_NAMES[captainShip]);

			if (ad->state != tws_off)
				fixShips(p->arena);

			/*
			remove overrides from old captain settings if applicable. replace the keys and then
			put in new overrides if applicable.
			*/
			if (ad->disabledWeapons)
			{
				cs->ArenaUnoverride(p->arena, ad->kcg);
				cs->ArenaUnoverride(p->arena, ad->kct);
				cs->ArenaUnoverride(p->arena, ad->kcb);
				cs->ArenaUnoverride(p->arena, ad->kcm);
			}

			ad->kcg = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "bulletfireenergy");
			ad->kct = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "multifireenergy");
			ad->kcb = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "bombfireenergy");
			ad->kcm = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->captainShip], "landminefireenergy");

			if (ad->disabledWeapons)
			{
				cs->ArenaOverride(p->arena, ad->kcg, 32000);
				cs->ArenaOverride(p->arena, ad->kct, 32000);
				cs->ArenaOverride(p->arena, ad->kcb, 32000);
				cs->ArenaOverride(p->arena, ad->kcm, 32000);
				sendSettings(p->arena);
			}
		}
	}
	else if (!strcasecmp(cmd, "setplayership"))
	{
		int playerShip = atoi(params) - 1;
		if ((playerShip < SHIP_WARBIRD) || (playerShip >= SHIP_SPEC))
		{
			chat->SendMessage(p, "Invalid input.");
		}
		else
		{
			ad->playerShip = playerShip;
			chat->SendArenaMessage(p->arena, " ** %s sets the player ship to %s", p->name, cfg->SHIP_NAMES[playerShip]);

			/*
			remove overrides from old player settings if applicable. replace the keys and then
			put in new overrides if applicable.
			*/
			if (ad->disabledWeapons)
			{
				cs->ArenaUnoverride(p->arena, ad->kpg);
				cs->ArenaUnoverride(p->arena, ad->kpt);
				cs->ArenaUnoverride(p->arena, ad->kpb);
				cs->ArenaUnoverride(p->arena, ad->kpm);
			}

			ad->kpg = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "bulletfireenergy");
			ad->kpt = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "multifireenergy");
			ad->kpb = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "bombfireenergy");
			ad->kpm = cs->GetOverrideKey(cfg->SHIP_NAMES[ad->playerShip], "landminefireenergy");

			if (ad->disabledWeapons)
			{
				cs->ArenaOverride(p->arena, ad->kpg, 32000);
				cs->ArenaOverride(p->arena, ad->kpt, 32000);
				cs->ArenaOverride(p->arena, ad->kpb, 32000);
				cs->ArenaOverride(p->arena, ad->kpm, 32000);
				sendSettings(p->arena);
			}
		}
		if (ad->state != tws_off)
			fixShips(p->arena);
	}
	MYGUNLOCK;
}
