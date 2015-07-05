
/* dist: public */

#include <stdio.h>
#include <string.h>

#include "asss.h"


/* the delay (in ticks) between a ?freqkick and its effect */
#define KICKDELAY 300

/* commands */
local void Cgiveowner(const char *, const char *, Player *, const Target *);
local void Cfreqkick(const char *, const char *, Player *, const Target *);
local void Ctakeownership(const char *tc, const char *params, Player *p, const Target *target);
local helptext_t giveowner_help, freqkick_help, takeownership_help;

/* callbacks */
local void MyPA(Player *p, int action, Arena *arena);
local void MyShipFreqCh(Player *p, int newship, int oldship, int newfreq, int oldfreq);

/* data */
local int ofkey;
#define OWNSFREQ(p) (*(char*)PPDATA(p, ofkey))

local Imainloop *ml;
local Iplayerdata *pd;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmd;
local Iconfig *cfg;
local Ichat *chat;
local Imodman *mm;

EXPORT const char info_freqowners[] = CORE_MOD_INFO("freqowners");

EXPORT int MM_freqowners(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		if (!ml || !pd || !aman || !game || !cmd || !cfg) return MM_FAIL;

		ofkey = pd->AllocatePlayerData(sizeof(char));
		if (ofkey == -1) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, MyPA, ALLARENAS);
		mm->RegCallback(CB_SHIPFREQCHANGE, MyShipFreqCh, ALLARENAS);

		cmd->AddCommand("giveowner", Cgiveowner, ALLARENAS, giveowner_help);
		cmd->AddCommand("freqkick", Cfreqkick, ALLARENAS, freqkick_help);
		cmd->AddCommand("takeownership", Ctakeownership, ALLARENAS, takeownership_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("giveowner", Cgiveowner, ALLARENAS);
		cmd->RemoveCommand("freqkick", Cfreqkick, ALLARENAS);
		cmd->RemoveCommand("takeownership", Ctakeownership, ALLARENAS);

		mm->UnregCallback(CB_PLAYERACTION, MyPA, ALLARENAS);
		mm->UnregCallback(CB_SHIPFREQCHANGE, MyShipFreqCh, ALLARENAS);
		pd->FreePlayerData(ofkey);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		return MM_OK;
	}
	return MM_FAIL;
}


local void count_freq(Arena *arena, int freq, Player *excl, int *total,
		int *hasowner, StringBuffer *sb)
{
	int t = 0;
	Player *i;
	Link *link;

	*hasowner = FALSE;
	pd->Lock();
	FOR_EACH_PLAYER(i)
		if (i->arena == arena &&
		    i->p_freq == freq &&
		    i != excl)
		{
			t++;
			if (OWNSFREQ(i))
			{
				*hasowner = TRUE;
				if (sb)
					SBPrintf(sb, ", %s", i->name);
			}
		}
	pd->Unlock();
	*total = t;
}


local int owner_allowed(Arena *arena, int freq)
{
	ConfigHandle ch = arena->cfg;
	/* cfghelp: Team:AllowFreqOwners, arena, bool, def: 1
	 * Whether to enable the freq ownership feature in this arena. */
	return cfg->GetInt(ch, "Team", "AllowFreqOwners", 1) &&
		freq >= cfg->GetInt(ch, "Team", "PrivFreqStart", 100) &&
		freq != arena->specfreq;
}


local int kick_timer(void *param)
{
	Player *t = param;
	chat->SendMessage(t, "You have been kicked off your freq.");
	game->SetShipAndFreq(t, SHIP_SPEC, t->arena->specfreq);
	return FALSE;
}


local helptext_t giveowner_help =
"Module: freqowners\n"
"Targets: player\n"
"Args: none\n"
"Allows you to share freq ownership with another player on your current\n"
"private freq. You can't remove ownership once you give it out, but you\n"
"are safe from being kicked off yourself, as long as you have ownership.\n";

void Cgiveowner(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->u.p;

	if (!OWNSFREQ(p))
		chat->SendMessage(p, "You are not the owner of your freq.");
	else if (target->type != T_PLAYER ||
	         t->arena != p->arena ||
	         t->p_freq != p->p_freq)
		chat->SendMessage(p, "You must send this command to a player on your freq.");
	else if (OWNSFREQ(t))
		chat->SendMessage(p, "That player is already an owner of your freq.");
	else
	{
		OWNSFREQ(t) = 1;
		chat->SendMessage(p, "You have granted ownership to %s.", t->name);
		chat->SendMessage(t, "%s has granted you ownership of your freq.", p->name);
	}
}


local helptext_t takeownership_help =
"Module: freqowners\n"
"Targets: none\n"
"Args: none\n"
"Makes you become owner of your freq, if your freq doesn't\n"
"have an owner already.\n";

void Ctakeownership(const char *tc, const char *params, Player *p, const Target *target)
{
	if (!owner_allowed(p->arena, p->p_freq))
		chat->SendMessage(p, "Your freq can not have an owner.");
	else if (OWNSFREQ(p))
		chat->SendMessage(p, "You are already an owner of your freq.");
	else
	{
		int total, hasowner;
		StringBuffer sb;

		SBInit(&sb);
		count_freq(p->arena, p->p_freq, p, &total, &hasowner, &sb);

		if (hasowner)
		{
			chat->SendMessage(p, "Your freq already has one or more owners:");
			chat->SendWrappedText(p, SBText(&sb, 2));
		}
		else
		{
			OWNSFREQ(p) = 1;
			chat->SendMessage(p, "You are now the owner of your freq.");
		}
		SBDestroy(&sb);
	}
}


local helptext_t freqkick_help =
"Module: freqowners\n"
"Targets: player\n"
"Args: none\n"
"Kicks the player off of your freq. The player must be on your freq and\n"
"must not be an owner himself. The player giving the command, of course,\n"
"must be an owner.\n";

void Cfreqkick(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->u.p;

	if (!OWNSFREQ(p))
		chat->SendMessage(p, "You are not the owner of your freq.");
	else if (target->type != T_PLAYER ||
	         t->arena != p->arena ||
	         t->p_freq != p->p_freq)
		chat->SendMessage(p, "You must send this command to a player on your freq.");
	else if (OWNSFREQ(t))
		chat->SendMessage(p, "You can not kick other freq owners off your freq.");
	else
	{
		chat->SendMessage(p, "%s will be kicked off momentarily.", t->name);
		ml->SetTimer(kick_timer, KICKDELAY, 0, t, t);
	}
}


void MyPA(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA || action == PA_LEAVEARENA)
	{
		OWNSFREQ(p) = 0;
		ml->ClearTimer(kick_timer, p);
	}
}


void MyShipFreqCh(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	Arena *arena = p->arena;

	OWNSFREQ(p) = 0;
	ml->ClearTimer(kick_timer, p);

	if (owner_allowed(arena, newfreq))
	{
		int total, hasowner;
		count_freq(arena, newfreq, p, &total, &hasowner, NULL);

		if (total == 0)
		{
			OWNSFREQ(p) = 1;
			chat->SendMessage(p, "You are the now the owner of freq %d. "
					"You can kick people off your freq by sending them "
					"the private message \"?freqkick\", and you can share "
					"your ownership by sending people \"?giveowner\".", newfreq);
		}
		else if (hasowner)
		{
			chat->SendMessage(p,
					"This freq has an owner. Be aware that you can be kicked off any time.");
			chat->SendMessage(p,
					"If the owner leaves, you can use ?takeownership to become the owner.");
		}
	}
}

