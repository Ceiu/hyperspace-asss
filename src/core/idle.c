
/* dist: public */

#include <time.h>
#include "asss.h"
#include "idle.h"

local Iplayerdata *pd;
local Inet *net;
local Ichatnet *chatnet;
local Ichat *chat;
local Icmdman *cmd;

local int pdkey;

#define IDLE_TIME 120

typedef struct pd_idle
{
	time_t lastActive;
	int notAvailable : 1;
} pd_idle;

local int GetIdle(Player *p)
{
	pd_idle *pdat = PPDATA(p, pdkey);
	time_t lastevt = pdat->lastActive, now = time(NULL);
	return now - lastevt;
}

local void ResetIdle(Player *p)
{
	pd_idle *pdat = PPDATA(p, pdkey);
	time(&pdat->lastActive);
}

local int isAvailable(Player *p)
{
	if ((p->status == S_PLAYING) && (p->type != T_FAKE))
	{
		pd_idle *pdat = PPDATA(p, pdkey);
		if (!pdat->notAvailable)
			return 1;
	}

	return 0;
}

local void ppk(Player *p, byte *pkt, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)pkt;
	if (pos->weapon.type && (pos->time & 7) == 0)
		ResetIdle(p);
}

local void packetfunc(Player *p, byte *pkt, int len)
{
	ResetIdle(p);
}

local void messagefunc(Player *p, const char *line)
{
	ResetIdle(p);
}

local helptext_t available_help =
"Targets: self\n"
"Syntax:\n"
"  ?available\n"
"Command Aliases:\n"
"  ?av\n"
"Marks you as Available. Certain games require you to be Available to be picked.\n";
local void Cavailable(const char *cmd, const char *params, Player *p, const Target *target)
{
	pd_idle *pdat = PPDATA(p, pdkey);
	pdat->notAvailable = 0;
	chat->SendMessage(p, "Marked as Available.");
}

local helptext_t notavailable_help =
"Targets: self\n"
"Syntax:\n"
"  ?notavailable\n"
"Command Aliases:\n"
"  ?nav\n"
"Marks you as Not Available. Certain games will prevent you from being picked when Not Available.\n";
local void Cnotavailable(const char *cmd, const char *params, Player *p, const Target *target)
{
	pd_idle *pdat = PPDATA(p, pdkey);
	pdat->notAvailable = 1;
	chat->SendMessage(p, "Marked as Not Available.");
}

local helptext_t actives_help =
"Targets: arena\n"
"Syntax:\n"
"  ?specactives\n"
"Lists everyone who is Available and not Idle.\n"
"See Also:\n"
"  ?idles ?specactives\n";
local void Cactives(const char *cmd, const char *params, Player *p, const Target *target)
{
	Player *x;
	Link *link;
	StringBuffer sb;
	int total = 0;

	SBInit(&sb);
	pd->Lock();
	FOR_EACH_PLAYER(x)
	{
		pd_idle *pdat = PPDATA(x, pdkey);
		if ( (x->status == S_PLAYING) && (x->arena == p->arena) && (!pdat->notAvailable) &&
			 ( ((time(0) - pdat->lastActive) < IDLE_TIME) || (x->p_ship != SHIP_SPEC) )
			)
		{
			++total;
			SBPrintf(&sb, ", %s", x->name);
		}
	}
	pd->Unlock();

	chat->SendMessage(p, "Arena '%s': %d active", p->arena->name, total);
	chat->SendWrappedText(p, SBText(&sb, 2));
	SBDestroy(&sb);
}

local helptext_t idles_help =
"Targets: arena\n"
"Syntax:\n"
"  ?idles\n"
"Lists everyone who is Idle.\n"
"See Also:\n"
"  ?actives\n";
local void Cidles(const char *cmd, const char *params, Player *p, const Target *target)
{
	Player *x;
	Link *link;
	StringBuffer sb;
	int total = 0;

	SBInit(&sb);
	pd->Lock();
	FOR_EACH_PLAYER(x)
	{
		pd_idle *pdat = PPDATA(x, pdkey);
		if ((x->status == S_PLAYING) && (x->arena == p->arena) && (x->p_ship == SHIP_SPEC) && ((time(0) - pdat->lastActive) >= IDLE_TIME))
		{
			++total;
			SBPrintf(&sb, ", %s", x->name);
		}
	}
	pd->Unlock();

	chat->SendMessage(p, "Arena '%s': %d idle", p->arena->name, total);
	chat->SendWrappedText(p, SBText(&sb, 2));
	SBDestroy(&sb);
}

local helptext_t specactives_help =
"Targets: arena\n"
"Syntax:\n"
"  ?actives\n"
"Lists everyone who is Available, in Spectator Mode, and not Idle.\n"
"See Also:\n"
"  ?idles ?actives\n";
local void Cspecactives(const char *cmd, const char *params, Player *p, const Target *target)
{
	Player *x;
	Link *link;
	StringBuffer sb;
	int total = 0;

	SBInit(&sb);
	pd->Lock();
	FOR_EACH_PLAYER(x)
	{
		pd_idle *pdat = PPDATA(x, pdkey);
		if ( (x->status == S_PLAYING) && (x->arena == p->arena) && (!pdat->notAvailable) &&
			 ( ((time(0) - pdat->lastActive) < IDLE_TIME) && (x->p_ship == SHIP_SPEC) )
			)
		{
			++total;
			SBPrintf(&sb, ", %s", x->name);
		}
	}
	pd->Unlock();

	chat->SendMessage(p, "Arena '%s': %d active in spec", p->arena->name, total);
	chat->SendWrappedText(p, SBText(&sb, 2));
	SBDestroy(&sb);
}


local Iidle myint =
{
	INTERFACE_HEAD_INIT(I_IDLE, "idle")
	GetIdle, ResetIdle, isAvailable
};

EXPORT int MM_idle(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!pd) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pd_idle));
		if (pdkey == -1) return MM_FAIL;

		if (net)
		{
			net->AddPacket(C2S_GOTOARENA, packetfunc);
			net->AddPacket(C2S_CHAT, packetfunc);
			net->AddPacket(C2S_SPECREQUEST, packetfunc);
			net->AddPacket(C2S_SETFREQ, packetfunc);
			net->AddPacket(C2S_SETSHIP, packetfunc);
			net->AddPacket(C2S_BRICK, packetfunc);
			net->AddPacket(C2S_POSITION, ppk);
		}
		if (chatnet)
		{
			chatnet->AddHandler("GO", messagefunc);
			chatnet->AddHandler("CHANGEFREQ", messagefunc);
			chatnet->AddHandler("SEND", messagefunc);
		}

		cmd->AddCommand("notavailable", Cnotavailable, ALLARENAS, notavailable_help);
		cmd->AddCommand("nav", Cnotavailable, ALLARENAS, notavailable_help);
		cmd->AddCommand("available", Cavailable, ALLARENAS, available_help);
		cmd->AddCommand("av", Cavailable, ALLARENAS, available_help);
		cmd->AddCommand("idles", Cidles, ALLARENAS, idles_help);
		cmd->AddCommand("actives", Cactives, ALLARENAS, actives_help);
		cmd->AddCommand("specactives", Cspecactives, ALLARENAS, specactives_help);


		mm->RegInterface(&myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myint, ALLARENAS))
			return MM_FAIL;

		cmd->RemoveCommand("notavailable", Cnotavailable, ALLARENAS);
		cmd->RemoveCommand("nav", Cnotavailable, ALLARENAS);
		cmd->RemoveCommand("available", Cavailable, ALLARENAS);
		cmd->RemoveCommand("av", Cavailable, ALLARENAS);
		cmd->RemoveCommand("idles", Cidles, ALLARENAS);
		cmd->RemoveCommand("actives", Cactives, ALLARENAS);
		cmd->RemoveCommand("specactives", Cspecactives, ALLARENAS);
	
		if (net)
		{
			net->RemovePacket(C2S_GOTOARENA, packetfunc);
			net->RemovePacket(C2S_CHAT, packetfunc);
			net->RemovePacket(C2S_SPECREQUEST, packetfunc);
			net->RemovePacket(C2S_SETFREQ, packetfunc);
			net->RemovePacket(C2S_SETSHIP, packetfunc);
			net->RemovePacket(C2S_BRICK, packetfunc);
			net->RemovePacket(C2S_POSITION, ppk);
		}
		if (chatnet)
		{
			chatnet->RemoveHandler("GO", messagefunc);
			chatnet->RemoveHandler("CHANGEFREQ", messagefunc);
			chatnet->RemoveHandler("SEND", messagefunc);
		}
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		return MM_OK;
	}
	else
		return MM_FAIL;
}

