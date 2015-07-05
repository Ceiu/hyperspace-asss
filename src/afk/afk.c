/*
 * afk
 *
 * [AFK]
 * Kick=0
 * Spec=1
 * InactiveTime=6000
 * InactiveSafeTime=12000
 *
 * Specs or kicks a player that is not moving, not firing a weapon or not
 *  chatting after a certain amount of time.
 *
 * 14/11/04 - Created by Smong
 *
 * 13/01/05 - Moved all all operations (messages and setship) outside of the
 *  loop to avoid possible deadlock.
 *  Oops, copy/paste error.
 *
 * 08/05/05 - Speccing now moves the player to the spec freq.
 *
 */

#include <stdlib.h>
#include "asss.h"

#define LEN_PPK_WEAPON (sizeof(struct C2SPosition) - sizeof(struct ExtraPosData))

struct pdata
{
	ticks_t lastactive;
	int xspeed, yspeed, rotation;
};

struct adata
{
	u16 cfg_kick, cfg_spec;
	u32 cfg_inactivetime, cfg_inactivesafetime;
};

local void ArenaAction(Arena *a, int action);
local void PlayerAction(Player *p, int action, Arena *arena);
local void ChatMsg(Player *p, int type, int sound, Player *target, int freq,
	const char *text);
local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local void Pppk(Player *p, byte *pkt, int len);
local int timer(void *arena_);

local int adkey, pdkey;

local Imodman     *mm;
local Iconfig     *cfg;
local Iplayerdata *pd;
local Igame       *game;
local Ichat       *chat;
local Iarenaman   *aman;
local Imainloop   *ml;
local Inet        *net;

local void ArenaAction(Arena *a, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		struct adata *ad = P_ARENA_DATA(a, adkey);
		ad->cfg_kick = cfg->GetInt(a->cfg, "AFK", "Kick", 0);
		ad->cfg_spec = cfg->GetInt(a->cfg, "AFK", "Spec", 1);
		ad->cfg_inactivetime = cfg->GetInt(a->cfg, "AFK",
			"InactiveTime", 6000);
		ad->cfg_inactivesafetime = cfg->GetInt(a->cfg, "AFK",
			"InactiveSafeTime", 12000);
	}
}

local void PlayerAction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
	{
		struct pdata *d = PPDATA(p, pdkey);
		d->lastactive = current_ticks();
		d->xspeed = -1;
		d->yspeed = -1;
		d->rotation = -1;
  	}
}

local void ChatMsg(Player *p, int type, int sound, Player *target, int freq,
	const char *text)
{
	struct pdata *d = PPDATA(p, pdkey);
	d->lastactive = current_ticks();
}

local void ShipFreqChange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	struct pdata *d = PPDATA(p, pdkey);
	d->lastactive = current_ticks();
}

local void Pppk(Player *p, byte *pkt, int len)
{
	if (p->arena && p->p_ship != SHIP_SPEC)
	{
		struct pdata *d = PPDATA(p, pdkey);
		struct C2SPosition *pos = (struct C2SPosition *)pkt;
		if (len >= LEN_PPK_WEAPON && pos->weapon.type != W_NULL)
			d->lastactive = current_ticks();
	}
}

local int timer(void *arena_)
{
	LinkedList tokill = LL_INITIALIZER;
	LinkedList tospec = LL_INITIALIZER;
	Arena *arena = arena_;
	struct pdata *d;
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	Link *link;
	Player *p;
	u32 cfg_inactivetime = ad->cfg_inactivetime;
	u32 inactivetime;
	ticks_t now = current_ticks();

	pd->Lock();
	FOR_EACH_PLAYER_P(p, d, pdkey)
	{
		if (p->arena != arena || p->p_ship == SHIP_SPEC ||
			!IS_STANDARD(p)|| p->status != S_PLAYING)
			continue;

		/* check position */
		if (d->xspeed != p->position.xspeed ||
			d->yspeed != p->position.yspeed ||
			d->rotation != p->position.rotation)
		{
			d->lastactive = now;
		}
		else if (abs(d->xspeed >> 1) >= 200 || abs(d->yspeed >> 1) >= 200)
		{
			/* if your position hasn't changed by 200 pixels then
			 * you are inactive */
			d->lastactive = now;
		}

		/* update position */
		d->xspeed = p->position.xspeed;
		d->yspeed = p->position.yspeed;
		d->rotation = p->position.rotation;

		/* safe zone check. could use smc here. */
		if (p->position.status & STATUS_SAFEZONE)
			inactivetime = ad->cfg_inactivesafetime;
		else
			inactivetime = cfg_inactivetime;

		if (now - d->lastactive >= inactivetime)
		{
			if (ad->cfg_kick)
				LLAdd(&tokill, p);
			else
				LLAdd(&tospec, p);
		}
	}
	pd->Unlock();

	for (link = LLGetHead(&tokill); link; link = link->next)
	{
		chat->SendMessage(link->data, "You are being kicked due to "
			"inactivity.");
		pd->KickPlayer(link->data);
	}
	LLEmpty(&tokill);

	for (link = LLGetHead(&tospec); link; link = link->next)
	{
		Player *p = link->data;
		chat->SendMessage(p, "You are being specced due to inactivity.");
		game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
	}
	LLEmpty(&tospec);

	return 1;
}

EXPORT const char info_afk[] = "v1.2 smong <soinsg@hotmail.com>";

EXPORT int MM_afk(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm   = mm_;
		cfg  = mm->GetInterface(I_CONFIG,     ALLARENAS);
		pd   = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		game = mm->GetInterface(I_GAME,       ALLARENAS);
		chat = mm->GetInterface(I_CHAT,       ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN,   ALLARENAS);
		ml   = mm->GetInterface(I_MAINLOOP,   ALLARENAS);
		net  = mm->GetInterface(I_NET,        ALLARENAS);
		if (!cfg || !pd || !game || !chat || !aman || !ml || !net)
			return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(struct pdata));
		if (pdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		ml->ClearTimer(timer, NULL);
		net->RemovePacket(C2S_POSITION, Pppk);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		/* support load-n-play on already created arenas */
		ArenaAction(arena, AA_CONFCHANGED);
		mm->RegCallback(CB_PLAYERACTION,   PlayerAction,   arena);
		mm->RegCallback(CB_CHATMSG,        ChatMsg,        arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
		ml->SetTimer(timer, 500, 500, arena, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregCallback(CB_PLAYERACTION,   PlayerAction,   arena);
		mm->UnregCallback(CB_CHATMSG,        ChatMsg,        arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, ShipFreqChange, arena);
		ml->ClearTimer(timer, arena);
		return MM_OK;
	}

	return MM_FAIL;
}

