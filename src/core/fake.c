
/* dist: public */

#include <string.h>

#include "asss.h"
#include "fake.h"

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Inet *net;
local Ichatnet *chatnet;
local Icmdman *cmd;
local Ilogman *lm;
local Imainloop *ml;

local Player * CreateFakePlayer(const char *name, Arena *arena, int ship, int freq)
{
	Player *p;

	/* create pid */
	p = pd->NewPlayer(T_FAKE);
	if (!p) return NULL;

	/* set up pdata struct and pretend he's logged in */
	strncpy(p->pkt.name, name, 20);
	astrncpy(p->name, name, 21);
	strncpy(p->pkt.squad, "", 20);
	astrncpy(p->squad, "", 21);
	astrncpy(p->clientname, "<internal fake player>", sizeof(p->clientname));
	p->p_ship = ship;
	p->p_freq = freq;
	p->arena = arena;
	SET_SEND_DAMAGE(p);

	/* enter arena */
	if (net) net->SendToArena(arena, p, (byte*)&p->pkt, 64, NET_RELIABLE);
	if (chatnet) chatnet->SendToArena(arena, p,
			"ENTERING:%s:%d:%d", p->name, ship, freq);
	p->status = S_PLAYING;

	if (lm)
		lm->Log(L_INFO, "<fake> {%s} [%s] fake player created",
				arena->name,
				p->name);

	return p;
}

local int end_fake_timer_callback(void *clos)
{
	Player *p = clos;
	Arena *arena = p->arena;
	struct SimplePacket pk = { S2C_PLAYERLEAVING };

	/* leave arena */
	if (arena)
	{
		pk.d1 = p->pid;
		if (net) net->SendToArena(arena, p, (byte*)&pk, 3, NET_RELIABLE);
		if (chatnet) chatnet->SendToArena(arena, p,
				"LEAVING:%s", p->name);
	}

	/* log before freeing pid to avoid races */
	if (lm)
		lm->Log(L_INFO, "<fake> {%s} [%s] fake player destroyed",
				arena ? arena->name : "???",
				p->name);

	/* leave game */
	pd->FreePlayer(p);

	return FALSE;
}

local int EndFaked(Player *p)
{
	if (!p)
		return 0;
	if (p->type != T_FAKE)
		return 0;
	if (p->status != S_PLAYING || !p->arena)
		lm->LogP(L_WARN, "fake", p, "fake player with bad status");

	ml->SetTimer(end_fake_timer_callback, 0, 0, p, p);	

	return 1;
}


local void Cmakefake(const char *tc, const char *params, Player *p, const Target *target)
{
	CreateFakePlayer(params, p->arena, SHIP_SPEC, 9999);
}


local void Ckillfake(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
		EndFaked(target->u.p);
}


local Ifake _int =
{
	INTERFACE_HEAD_INIT(I_FAKE, "fake")
	CreateFakePlayer, EndFaked
};

EXPORT const char info_fake[] = CORE_MOD_INFO("fake");

EXPORT int MM_fake(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!pd || !aman || !cmd || !ml) return MM_FAIL;

		cmd->AddCommand("makefake", Cmakefake, ALLARENAS, NULL);
		cmd->AddCommand("killfake", Ckillfake, ALLARENAS, NULL);
		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		cmd->RemoveCommand("makefake", Cmakefake, ALLARENAS);
		cmd->RemoveCommand("killfake", Ckillfake, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		return MM_OK;
	}
	return MM_FAIL;
}


