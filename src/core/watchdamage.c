
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"
#include "watchdamage.h"


#define WATCHCOUNT(wd) \
	( LLCount(&wd->watches) + wd->modwatch )


/* global data */

local Iplayerdata *pd;
local Ichat *chat;
local Ilogman *lm;
local Icmdman *cmd;
local Inet *net;
local Imodman *mm;

typedef struct
{
	LinkedList watches;
	short modwatch;
} watchdata;

local int wdkey;


/* functions to send packets */

local void ToggleWatch(Player *p, byte on)
{
	byte pk[2] = { S2C_TOGGLEDAMAGE, on };

	if (p->type == T_CONT)
	{
		net->SendToOne(p, pk, 2, NET_RELIABLE | NET_PRI_N1);

		/* for temp debugging to make sure we arn't sending more of these packets than we need
		chat->SendMessage(p, "(Your damage watching turned %s)", on ? "on" : "off");
		*/
	}
}


local int AddWatch(Player *p, Player *target)
{
	watchdata *wd = PPDATA(target, wdkey);
	Link *l;

	/* check to see if already on */
	for (l = LLGetHead(&wd->watches); l; l = l->next)
		if (l->data == p)
			return -1;

	/* add new int to end of list */
	LLAdd(&wd->watches, p);

	/* check to see if need to send a packet */
	if (WATCHCOUNT(wd) == 1)
		ToggleWatch(target, 1);

	return 1;
}

local void RemoveWatch(Player *p, Player *target)
{
	watchdata *wd = PPDATA(target, wdkey);

	if (WATCHCOUNT(wd) <= 0)
		return;

	LLRemoveAll(&wd->watches, p);

	/* check to see if need to send a packet */
	if (WATCHCOUNT(wd) == 0)
		ToggleWatch(target, 0);
}

local void ClearWatch(Player *p, int himtoo)
{
	watchdata *wd = PPDATA(p, wdkey);
	Player *i;
	Link *link;

	/* remove his watches on others */
	pd->Lock();
	FOR_EACH_PLAYER(i)
		RemoveWatch(p, i);
	pd->Unlock();

	/* remove people watching him */
	if (himtoo)
		LLEmpty(&wd->watches);
}

local void ModuleWatch(Player *p, int on)
{
	watchdata *wd = PPDATA(p, wdkey);

	if (on && WATCHCOUNT(wd) == 0)
		ToggleWatch(p, 1);
	else if (!on && WATCHCOUNT(wd) == 1)
		ToggleWatch(p, 0);

	if (on)
		wd->modwatch++;
	else
		wd->modwatch -= wd->modwatch > 0 ? 1 : 0;
}

local int WatchCount(Player *p)
{
	watchdata *wd = PPDATA(p, wdkey);
	return WATCHCOUNT(wd);
}


local helptext_t watchdamage_help =
"Targets: player, none\n"
"Args: [0 or 1]\n"
"Turns damage watching on and off. If sent to a player, an argument of 1\n"
"turns it on, 0 turns it off, and no argument toggles. If sent as a\n"
"public command, only {?watchdamage 0} is meaningful, and it turns off\n"
"damage watching on all players.\n";

local void Cwatchdamage(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type == T_ARENA)
	{
		if (params[0] == '0' && params[1] == 0)
		{
			/* if sent publicly, turns off all their watches */
			ClearWatch(p, 0);
			if (chat) chat->SendMessage(p, "All damage watching turned off.");
		}
	}
	else if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;

		if (t->type != T_CONT)
		{
			if (chat) chat->SendMessage(p, "Watchdamage requires %s to use Continuum.", t->name);
			return;
		}

		if (params[1] == 0 && (params[0] == '0' || params[0] == '1'))
		{
			/* force either on or off */
			if (params[0] == '0')
			{
				RemoveWatch(p, t);
				if (chat) chat->SendMessage(p, "Damage watching on %s turned off.", t->name);
			}
			else
			{
				AddWatch(p, t);
				if (chat) chat->SendMessage(p, "Damage watching on %s turned on.", t->name);
			}
		}
		else
		{
			/* toggle */
			if (AddWatch(p, t) < 0)
			{
				RemoveWatch(p, t);
				if (chat) chat->SendMessage(p, "Damage watching on %s turned off.", t->name);
			}
			else
				if (chat) chat->SendMessage(p, "Damage watching on %s turned on.", t->name);
		}
	}
}


local helptext_t watchwatchdamage_help = NULL;

local void Cwatchwatchdamage(const char *tc, const char *params, Player *p, const Target *target)
{
	int mw = 0, tot = 0;
	Link *link, *l;
	Player *i;
	watchdata *wd;
	int onkey = pd->AllocatePlayerData(sizeof(int));
#define ON(p) (*(int*)PPDATA(p, onkey))

	if (onkey == -1) return;

	pd->Lock();

	FOR_EACH_PLAYER(i)
		ON(p) = 0;

	FOR_EACH_PLAYER_P(i, wd, wdkey)
	{
		if (wd->modwatch)
			mw++;

		if (wd->modwatch || !LLIsEmpty(&wd->watches))
			tot++;

		for (l = LLGetHead(&wd->watches); l; l = l->next)
			ON((Player*)l->data)++;
	}

	chat->SendMessage(p, "Total players reporting damage: %d.", tot);
	chat->SendMessage(p, "Players reporting damage to modules: %d.", mw);
	FOR_EACH_PLAYER(i)
		if (ON(i))
			chat->SendMessage(p, "%s is watching damage on %d players.",
					i->name, ON(i));

	pd->Unlock();

	pd->FreePlayerData(onkey);
}


local void PAWatch(Player *p, int action, Arena *arena)
{
	/* if he leaves arena, clear all watches on him and his watches */
	if (action == PA_LEAVEARENA)
		ClearWatch(p, 1);
}

local void PDamage(Player *p, byte *pkt, int len)
{
	watchdata *wd = PPDATA(p, wdkey);
	struct C2SWatchDamage *c2swd = (struct C2SWatchDamage *)pkt;
	struct S2CWatchDamage *s2cwd;
	Arena *arena = p->arena;
	int count, s2clen;

	if (((len - offsetof(struct C2SWatchDamage, damage)) % sizeof(DamageData)) != 0)
	{
		if (lm) lm->LogP(L_MALICIOUS, "watchdamage", p, "bad damage packet len=%d", len);
		return;
	}

	if (!arena || p->status != S_PLAYING) return;

	count = (len - 5) / sizeof(DamageData);
	s2clen = 7 + count * sizeof(DamageData);

	s2cwd = alloca(s2clen);
	s2cwd->type = S2C_DAMAGE;
	s2cwd->damageuid = p->pid;
	s2cwd->tick = c2swd->tick;

	memcpy(s2cwd->damage, c2swd->damage, count * sizeof(DamageData));

	net->SendToSet(&wd->watches, (byte*)s2cwd, s2clen, NET_RELIABLE | NET_PRI_N1);

	/* do callbacks only if a module is watching, since these can go in mass spamming sometimes */
	if (wd->modwatch > 0)
		DO_CBS(CB_PLAYERDAMAGE, arena, PlayerDamage, (arena, p, s2cwd, count));
}


local Iwatchdamage _int =
{
	INTERFACE_HEAD_INIT(I_WATCHDAMAGE, "watchdamage")
	AddWatch, RemoveWatch, ClearWatch, ModuleWatch, WatchCount
};

EXPORT const char info_watchdamage[] = CORE_MOD_INFO("watchdamage");

EXPORT int MM_watchdamage(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		if (!pd || !cmd || !net) return MM_FAIL;

		wdkey = pd->AllocatePlayerData(sizeof(watchdata));
		if (wdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, PAWatch, ALLARENAS);

		net->AddPacket(C2S_DAMAGE, PDamage);

		cmd->AddCommand("watchdamage", Cwatchdamage, ALLARENAS, watchdamage_help);
		cmd->AddCommand("watchwatchdamage", Cwatchwatchdamage, ALLARENAS, watchwatchdamage_help);

		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		cmd->RemoveCommand("watchdamage", Cwatchdamage, ALLARENAS);
		cmd->RemoveCommand("watchwatchdamage", Cwatchwatchdamage, ALLARENAS);

		net->RemovePacket(C2S_DAMAGE, PDamage);

		mm->UnregCallback(CB_PLAYERACTION, PAWatch, ALLARENAS);

		pd->FreePlayerData(wdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);

		return MM_OK;
	}
	return MM_FAIL;
}


