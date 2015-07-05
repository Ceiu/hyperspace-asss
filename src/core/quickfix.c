
/* dist: public */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "cfghelp.h"
#include "filetrans.h"


local Iplayerdata *pd;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Ilogman *lm;
local Icfghelp *cfghelp;
local Ifiletrans *filetrans;
local Inet *net;
local Icapman *capman;


local void try_section(const char *limit, const struct section_help *sh,
		ConfigHandle ch, FILE *f, const char *secname)
{
	int secgood, keygood, j;
	const struct key_help *kh;
	char min[16];
	const char *max;

	secgood = !limit || strcasestr(secname, limit) != NULL;
	for (j = 0; j < sh->keycount; j++)
	{
		kh = &sh->keys[j];
		keygood = !limit || strcasestr(kh->name, limit) != NULL;
		if ((secgood || keygood) && strcmp(kh->loc, "Arena") == 0)
		{
			const char *val = cfg->GetStr(ch, secname, kh->name);
			if (val == NULL)
				val = "<unset>";
			if (kh->range)
			{
				max = delimcpy(min, kh->range, 16, '-');
				if (max == NULL) max = "";
			}
			else
			{
				min[0] = 0;
				max = "";
			}
			/* sec:key:val:min:max:help */
			fprintf(f, "%s:%s:%s:%s:%s:%s\r\n",
					secname, kh->name,
					val, min, max, kh->helptext);
		}
	}
}

local void do_quickfix(Player *p, const char *limit)
{
	int i, fd;
	Arena *arena;
	ConfigHandle ch;
	const struct section_help *sh;
	char name[] = "tmp/quickfix-XXXXXX";
	FILE *f;
	long pos;

	arena = p->arena;
	if (!arena) return;
	ch = arena->cfg;

	fd = mkstemp(name);

	if (fd == -1)
	{
		lm->Log(L_WARN, "<quickfix> can't create temp file. Make sure tmp/ exists.");
		chat->SendMessage(p, "Error: can't create temporary file.");
		return;
	}

	f = fdopen(fd, "wb");

	/* construct server.set */
	for (i = 0; i < cfghelp->section_count; i++)
	{
		sh = &cfghelp->sections[i];
		if (strcmp(sh->name, "All"))
			try_section(limit, sh, ch, f, sh->name);
		else
		{
			try_section(limit, sh, ch, f, "Warbird");
			try_section(limit, sh, ch, f, "Javelin");
			try_section(limit, sh, ch, f, "Spider");
			try_section(limit, sh, ch, f, "Leviathan");
			try_section(limit, sh, ch, f, "Terrier");
			try_section(limit, sh, ch, f, "Weasel");
			try_section(limit, sh, ch, f, "Lancaster");
			try_section(limit, sh, ch, f, "Shark");
		}
	}

	pos = ftell(f);
	fclose(f);

	/* send and delete file */
	if (pos > 0)
	{
		chat->SendMessage(p, "Sending settings...");
		filetrans->SendFile(p, name, "server.set", TRUE);
	}
	else
	{
		chat->SendMessage(p, "No settings matched your query.");
		remove(name);
	}
}


local helptext_t quickfix_help =
"Module: quickfix\n"
"Targets: none\n"
"Args: <limiting text>\n"
"Lets you quickly change arena settings. This will display some list of\n"
"settings with their current values and allow you to change them. The\n"
"argument to this command can be used to limit the list of settings\n"
"displayed. (With no arguments, equivalent to ?getsettings in subgame.)\n";

local void Cquickfix(const char *tc, const char *params, Player *p, const Target *target)
{
	if (capman->HasCapability(p, CAP_CHANGESETTINGS))
		do_quickfix(p, params[0] ? params : NULL);
	else
		chat->SendMessage(p,
				"You are not authorized to view or change settings in this arena.");
}


local void p_settingchange(Player *p, byte *pkt, int len)
{
	Arena *arena;
	ConfigHandle ch;
	time_t t;
	struct tm _tm;
	const char *pos = (const char*)pkt + 1;
	char sec[MAXSECTIONLEN], key[MAXKEYLEN], info[128];
	int permanent = TRUE;

	arena = p->arena;
	if (!arena) return;
	ch = arena->cfg;

	if (!capman->HasCapability(p, CAP_CHANGESETTINGS))
	{
		chat->SendMessage(p,
				"You are not authorized to view or change settings in this arena.");
		return;
	}

#define CHECK(n) \
	if (!n) { \
		lm->LogP(L_MALICIOUS, "quickfix", p, \
				"Badly formatted setting change"); \
		return; \
	}

	time(&t);
	alocaltime_r(&t, &_tm);
	snprintf(info, 100, "set by %s with ?quickfix on ", p->name);
	strftime(info + strlen(info), sizeof(info) - strlen(info),
			"%a %b %d %H:%M:%S %Y", &_tm);

	while ((pos-(char*)pkt) < len)
	{
		pos = delimcpy(sec, pos, MAXSECTIONLEN, ':');
		CHECK(pos)
		pos = delimcpy(key, pos, MAXKEYLEN, ':');
		CHECK(pos)
		if (strcmp(sec, "__pragma") == 0)
		{
			if (strcmp(key, "perm") == 0)
			{
				/* send __pragma:perm:0 to make further settings changes
				 * temporary */
				permanent = atoi(pos);
			}
			else if (strcmp(key, "flush") == 0)
			{
				/* send __pragma:flush:1 to flush settings changes */
				cfg->FlushDirtyValues();
			}
		}
		else
		{
			lm->LogP(L_INFO, "quickfix", p, "setting %s:%s = %s",
					sec, key, pos);
			cfg->SetStr(ch, sec, key, pos, info, permanent);
		}
		pos = pos + strlen(pos) + 1;
		if (pos[0] == '\0') break;
	}
}

EXPORT const char info_quickfix[] = CORE_MOD_INFO("quickfix");

EXPORT int MM_quickfix(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfghelp = mm->GetInterface(I_CFGHELP, ALLARENAS);
		filetrans = mm->GetInterface(I_FILETRANS, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		if (!pd || !aman || !cfg || !chat || !cmd || !lm ||
				!cfghelp || !filetrans || !net || !capman)
			return MM_FAIL;

		net->AddPacket(C2S_SETTINGCHANGE, p_settingchange);
		cmd->AddCommand("quickfix", Cquickfix, ALLARENAS, quickfix_help);
		cmd->AddCommand("getsettings", Cquickfix, ALLARENAS, quickfix_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_SETTINGCHANGE, p_settingchange);
		cmd->RemoveCommand("quickfix", Cquickfix, ALLARENAS);
		cmd->RemoveCommand("getsettings", Cquickfix, ALLARENAS);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfghelp);
		mm->ReleaseInterface(filetrans);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}

