#include <stdio.h>
#include <string.h>

#include "asss.h"

local Ilogman *lm;
local Icmdman *cmd;
local Ichat *chat;
local Inet *net;
local Iplayerdata *pd;

local int pdata_key;

local FILE *logfile;
local pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

local helptext_t ppktest_help =
"Targets: player\n"
"Args: none\n"
"Toggles packet logging on the target player.\n";

local void Cppktest(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;
	int *pdata = PPDATA(t, pdata_key);
	
	if (*pdata)
	{
		chat->SendMessage(p, "Packet logging on %s disabled", t->name);
		*pdata = 0;
	}
	else
	{
		chat->SendMessage(p, "Packet logging on %s enabled", t->name);
		*pdata = 1;
	}
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	int *pdata = PPDATA(p, pdata_key);

	if (len < 22)
		return;

	/* handle common errors */
	if (!p->arena || !*pdata) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (!logfile)
		return;

	pthread_mutex_lock(&logmtx);
	fprintf(logfile, "%s,%d,%d,%d,%d,%d\n", p->name, pos->time, pos->x, pos->y, pos->xspeed, pos->yspeed);
	pthread_mutex_unlock(&logmtx);
}

EXPORT const char info_hs_ppktest[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_ppktest(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

		if (!lm || !cmd || !chat || !net || !pd)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(pd);
			return MM_FAIL;
		}

		// open the log file
		logfile = fopen("log/packet.log", "a");
		if (logfile)
		{
			fputs("===Opening Log File===\n", logfile);
		}

		pdata_key = pd->AllocatePlayerData(sizeof(int));
		if (pdata_key == -1) return MM_FAIL;

		cmd->AddCommand("ppktest", Cppktest, ALLARENAS, ppktest_help);
		
		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("ppktest", Cppktest, ALLARENAS);

		net->RemovePacket(C2S_POSITION, Pppk);

		pd->FreePlayerData(pdata_key);

		if (logfile)
		{
			fputs("===Closing Log File===\n", logfile);
			fclose(logfile);
		}

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);

		return MM_OK;
	}
	return MM_FAIL;
}

