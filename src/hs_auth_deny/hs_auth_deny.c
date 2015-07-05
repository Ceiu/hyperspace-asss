#include <string.h>
#include <stdio.h>

#include "asss.h"

local Imodman *mm;
local Ilogman *lm;
local Iauth *oldauth;
local Iplayerdata *pd;
local Iconfig *cfg;
local Icmdman *cmd;
local Ichat *chat;
local ConfigHandle denyFile;

local void authenticate(Player *p, struct LoginPacket *lp, int lplen,
		void (*done)(Player *p, AuthData *data))
{
	const char *setting;
	char name[32];

	/* copy to local storage in case it's not null terminated */
	astrncpy(name, lp->name, sizeof(name));

	setting = cfg->GetStr(denyFile, "DenyName", name);

	if (setting != NULL && strcasecmp(setting, "deny") == 0)
	{
		AuthData ad;

		memset(&ad, 0, sizeof(ad));
		ad.authenticated = FALSE;
		ad.code = AUTH_BADNAME;
		astrncpy(ad.name, name, sizeof(ad.name));
		astrncpy(ad.sendname, name, sizeof(ad.sendname));

		done(p, &ad);

		lm->Log(L_MALICIOUS, "<hs_auth_deny> denied player [%s] from %s", lp->name, p->ipaddr);
	}
	else
		oldauth->Authenticate(p, lp, lplen, done);
}

local helptext_t denyNameHelp =
"Module: hs_auth_deny\n"
"Targets: none or player\n"
"Args: <player>\n"
"Adds the targeted name to deny.conf.\n";

local void denyNameCommand(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *name = NULL;

	if (target->type == T_ARENA)
		name = params;
	else if (target->type == T_PLAYER)
		name = target->u.p->name;

	if (!name || !*name)
	{
		chat->SendMessage(p, "Invalid syntax. See ?help denyname.");
		return;
	}

	char info[128];
	time_t tm = time(NULL);
	snprintf(info, 100, "set by %s with ?denyname on ", p->name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	cfg->SetStr(denyFile, "DenyName", name, "deny", info, TRUE);

	chat->SendMessage(p, "Player %s denied.", name);
}

local helptext_t allowNameHelp =
"Module: hs_auth_deny\n"
"Targets: none or player\n"
"Args: <player>\n"
"Removes the targeted name from deny.conf.\n";

local void allowNameCommand(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *name = NULL;

	if (target->type == T_ARENA)
		name = params;
	else if (target->type == T_PLAYER)
		name = target->u.p->name;

	if (!name || !*name)
	{
		chat->SendMessage(p, "Invalid syntax. See ?help allowname.");
		return;
	}

	char info[128];
	time_t tm = time(NULL);
	snprintf(info, 100, "set by %s with ?allowname on ", p->name);
	ctime_r(&tm, info + strlen(info));
	RemoveCRLF(info);

	cfg->SetStr(denyFile, "DenyName", name, "allow", info, TRUE);

	chat->SendMessage(p, "Player %s allowed.", name);
}

local Iauth interface =
{
	INTERFACE_HEAD_INIT(I_AUTH, "hs-auth-deny")
	authenticate
};


EXPORT int MM_hs_auth_deny(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!oldauth || !lm || !pd || !cfg || !chat || !cmd) return MM_FAIL;

		denyFile = cfg->OpenConfigFile(NULL, "deny.conf", NULL, NULL);

		if (!denyFile) return MM_FAIL;

		cmd->AddCommand("denyname", denyNameCommand, ALLARENAS, denyNameHelp);
		cmd->AddCommand("allowname", allowNameCommand, ALLARENAS, allowNameHelp);

		mm->RegInterface(&interface, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
			return MM_FAIL;

		cmd->RemoveCommand("denyname", denyNameCommand, ALLARENAS);
		cmd->RemoveCommand("allowname", allowNameCommand, ALLARENAS);

		cfg->CloseConfigFile(denyFile);

		mm->ReleaseInterface(oldauth);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);

		return MM_OK;
	}
	return MM_FAIL;
}

