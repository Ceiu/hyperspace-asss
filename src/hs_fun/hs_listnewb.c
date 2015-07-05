#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"

local Imodman *mm;
local Iconfig *cfg;
local Iplayerdata *pd;
local Icmdman *cmd;
local Ichat *chat;
local Igame *game;
local Icapman *capman;

local ConfigHandle newb_conf;

local helptext_t listnewb_help =
"Targets: none\n"
"Args: none\n"
"Lists all online players with staff specified newbie titles.\n";

local void Clistnewb(const char *command, const char *params, Player *p, const Target *target)
{
	int seehid = capman->HasCapability(p, CAP_SEEPRIVARENA);

	Player *i;
	Link *link;

	const char *grp;

	chat->SendMessage(p, "+----------------------+------------+--------------------------------------------------------------+");

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		if (i->status != S_PLAYING)
			continue;

		grp = cfg->GetStr(newb_conf, "Newbie", i->name);

		if (!grp)
			continue;

		if (strcasecmp(grp, "none") == 0)
			continue;

		chat->SendMessage(p, "| %-20s | %-10s | %-60s |", i->name,
				(i->arena->name[0] != '#' || seehid || p->arena == i->arena) ? i->arena->name : "(private)", grp);
	}
	pd->Unlock();

	chat->SendMessage(p, "+----------------------+------------+--------------------------------------------------------------+");
}

local helptext_t addnewb_help =
"Targets: player\n"
"Args: <newb title>\n"
"Adds the specified newbie title for the player under ?listnewb.\n";

local void Caddnewb(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = target->u.p;

	time_t tm = time(NULL);
	char info[128];

	snprintf(info, sizeof(info), "set by %s on ", p->name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	cfg->SetStr(newb_conf, "Newbie", t->name, params, info, 1);
	chat->SendMessage(p, "Set player's newbie title to '%s'.", params);
}

local helptext_t removenewb_help =
"Targets: player\n"
"Args: none\n"
"Removes a player's newbie entry.\n";

local void Cremovenewb(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = target->u.p;

	time_t tm = time(NULL);
	char info[128];

	snprintf(info, sizeof(info), "set by %s on ", p->name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	cfg->SetStr(newb_conf, "Newbie", t->name, "none", info, 1);

	chat->SendMessage(p, "Set player's newbie title to 'none'.");
}

EXPORT const char info_hs_listnewb[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_listnewb(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		if (!pd || !cfg || !cmd || !chat || !game || !capman)
		{
			return MM_FAIL;
		}

		newb_conf = cfg->OpenConfigFile(NULL, "newb.conf", NULL, NULL);

		cmd->AddCommand("listnewb", Clistnewb, ALLARENAS, listnewb_help);
		cmd->AddCommand("addnewb", Caddnewb, ALLARENAS, addnewb_help);
		cmd->AddCommand("removenewb", Cremovenewb, ALLARENAS, removenewb_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("listnewb", Clistnewb, ALLARENAS);
		cmd->RemoveCommand("addnewb", Caddnewb, ALLARENAS);
		cmd->RemoveCommand("removenewb", Cremovenewb, ALLARENAS);

		cfg->CloseConfigFile(newb_conf);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}
