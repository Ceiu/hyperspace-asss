
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "asss.h"

#define KICKER_FIELD_LEN 24
#define REASON_FIELD_LEN 64

typedef struct
{
	TreapHead head;
	int count;
	time_t expire;
	char kicker[KICKER_FIELD_LEN];
	char reason[REASON_FIELD_LEN];
} ban_node_t;

local Imodman *mm;
local Iauth *oldauth;
local Icapman *capman;
local Ichat *chat;
local Icmdman *cmd;
local Iplayerdata *pd;
local Ilogman *lm;

local TreapHead *banroot = NULL;
local pthread_mutex_t banmtx = PTHREAD_MUTEX_INITIALIZER;


local void Authenticate(Player *p, struct LoginPacket *lp, int lplen,
		void (*Done)(Player *p, AuthData *data))
{
	ban_node_t *bn;

	pthread_mutex_lock(&banmtx);
	bn = (ban_node_t*)TrGet(banroot, (int)lp->macid);
	if (IS_STANDARD(p) && bn)
	{
		time_t now = time(NULL);
		if (now < bn->expire)
		{
			AuthData data = { 0, AUTH_CUSTOMTEXT, 0 };
			if (!bn->reason[0])
				snprintf(data.customtext, sizeof(data.customtext), "You have been temporarily kicked. You may log in again in %ldm%lds.", (bn->expire-now)/60, (bn->expire-now)%60);
			else
				snprintf(data.customtext, sizeof(data.customtext), "You have been temporarily kicked for %s. You may log in again in %ldm%lds.", bn->reason, (bn->expire-now)/60, (bn->expire-now)%60);
			bn->count++;
			if (lm) lm->Log(L_INFO, "<auth_ban> player [%s] tried to log in"
					" (try %d), banned for %ld more minutes",
					lp->name, bn->count, (bn->expire - now + 59) / 60);
			pthread_mutex_unlock(&banmtx);
			Done(p, &data);
			return;
		}
		else
			/* expired, remove and continue normally */
			TrRemove(&banroot, (int)lp->macid);
	}
	pthread_mutex_unlock(&banmtx);

	oldauth->Authenticate(p, lp, lplen, Done);
}


local helptext_t kick_help =
"Module: auth_ban\n"
"Targets: player\n"
"Args: [-s seconds | -t seconds | -m minutes | seconds] [message]\n"
"Kicks the player off of the server, with an optional timeout. (-s number, -t number, or number for seconds, -m number for minutes.)\n"
"For kicks with a timeout, you may provide a message to be displayed to the user.\n"
"Messages appear to users on timeout as \"You have been temporarily kicked for <MESSAGE>.\"\n";
local void Ckick(const char *tc, const char *params, Player *p, const Target *target)
{
	char *message = (char *)params;
	const char *parsedParams = params;
	int timeout = 0;
	Player *t = target->u.p;

	if (target->type != T_PLAYER)
	{
		chat->SendMessage(p, "This command only operates when targeting one specific player.");
		return;
	}

	if (t == p)
		return;

	if (!capman->HigherThan(p, t))
	{
		chat->SendMessage(p, "You don't have permission to use ?kick on that player.");
		chat->SendMessage(t, "%s tried to use ?kick on you.", p->name);
		return;
	}

	/* try timeout stuff while the player still exists */
	if (IS_STANDARD(t))
	{
		if (!strncmp(parsedParams, "-t", 2) || !strncmp(parsedParams, "-s", 2))
		{
			parsedParams += 2;
			while (isspace(*parsedParams)) ++parsedParams;
			timeout = strtol(parsedParams, &message, 0);
		}
		else if (!strncmp(parsedParams, "-m", 2))
		{
			parsedParams += 2;
			while (isspace(*parsedParams)) ++parsedParams;
			timeout = 60 * strtol(parsedParams, &message, 0);
		}
		else
		{
			timeout = strtol(parsedParams, &message, 0);
		}

		if (timeout > 0)
		{
			ban_node_t *bn = amalloc(sizeof(*bn));
			bn->head.key = (int)t->macid;
			bn->count = 0;
			bn->expire = time(NULL) + timeout;

			//remove leading spaces in the message
			while (isspace(*message)) ++message;

			astrncpy(bn->kicker, p->name, sizeof(bn->kicker));
			astrncpy(bn->reason, message, sizeof(bn->reason));

			pthread_mutex_lock(&banmtx);
			TrPut(&banroot, (TreapHead*)bn);
			pthread_mutex_unlock(&banmtx);
		}

		if (timeout > 119)
			chat->SendMessage(p, "Kicked '%s' for %i minutes and %i second(s).", t->name, timeout/60, timeout%60);
		else if (timeout > 59)
			chat->SendMessage(p, "Kicked '%s' for 1 minute and %i second(s).", t->name, timeout%60);
		else if (timeout > 1)
			chat->SendMessage(p, "Kicked '%s' for %i seconds.", t->name, timeout%60);
		else
			chat->SendMessage(p, "Kicked '%s'.", t->name);
	}
	else
	{
		chat->SendMessage(p, "Kicked '%s'.", t->name);
	}

	pd->KickPlayer(t);
}


local void add_mid_ban(TreapHead *node, void *clos)
{
	time_t ct = time(0);
	ban_node_t *ban = (ban_node_t*)node;
	StringBuffer *sb = clos;
	if (ban->expire)
	{
		int dif = ban->expire - ct;
		if (dif > 0)
		{
			SBPrintf(sb, ", %u by %s (%s) (%im%is left)",
					(unsigned)ban->head.key, ban->kicker, ban->reason, dif / 60, dif % 60);
		}
		else
		{
			SBPrintf(sb, ", %u by %s (%s) (kick has expired)",
					(unsigned)ban->head.key, ban->kicker, ban->reason);
		}
	}
	else
	{
		SBPrintf(sb, ", %u", (unsigned)ban->head.key);
	}
}

local helptext_t listmidbans_help =
"Module: auth_ban\n"
"Targets: none\n"
"Args: none\n"
"Lists the current kicks (machine-id bans) in effect.\n";
local void Clistmidbans(const char *tc, const char *params, Player *p, const Target *target)
{
	StringBuffer sb;
	SBInit(&sb);
	pthread_mutex_lock(&banmtx);
	TrEnum(banroot, add_mid_ban, &sb);
	pthread_mutex_unlock(&banmtx);
	chat->SendMessage(p, "Active machine id bans:");
	chat->SendWrappedText(p, SBText(&sb, 2));
	SBDestroy(&sb);
}


local helptext_t delmidban_help =
"Module: auth_ban\n"
"Targets: none\n"
"Args: <machine id>\n"
"Removes a machine id ban.\n";

local void Cdelmidban(const char *tc, const char *params, Player *p, const Target *target)
{
	int mid = (int)strtoul(params, NULL, 0);
	if (mid == 0)
		chat->SendMessage(p, "Invalid machine id.");
	else
	{
		pthread_mutex_lock(&banmtx);
		TrRemove(&banroot, mid);
		pthread_mutex_unlock(&banmtx);
	}
}


local Iauth myauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "auth-ban")
	Authenticate
};

EXPORT const char info_auth_ban[] = CORE_MOD_INFO("auth_ban");

EXPORT int MM_auth_ban(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!oldauth || !capman || !cmd || !chat || !pd)
			return MM_FAIL;

		cmd->AddCommand("kick", Ckick, ALLARENAS, kick_help);
		cmd->AddCommand("listmidbans", Clistmidbans, ALLARENAS, listmidbans_help);
		cmd->AddCommand("listkick", Clistmidbans, ALLARENAS, listmidbans_help);
		cmd->AddCommand("delmidban", Cdelmidban, ALLARENAS, delmidban_help);
		cmd->AddCommand("delkick", Cdelmidban, ALLARENAS, delmidban_help);
		cmd->AddCommand("liftkick", Cdelmidban, ALLARENAS, delmidban_help);

		mm->RegInterface(&myauth, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;
		cmd->RemoveCommand("kick", Ckick, ALLARENAS);
		cmd->RemoveCommand("listmidbans", Clistmidbans, ALLARENAS);
		cmd->RemoveCommand("listkick", Clistmidbans, ALLARENAS);
		cmd->RemoveCommand("delmidban", Cdelmidban, ALLARENAS);
		cmd->RemoveCommand("delkick", Cdelmidban, ALLARENAS);
		cmd->RemoveCommand("liftkick", Cdelmidban, ALLARENAS);
		TrEnum(banroot, tr_enum_afree, NULL);
		mm->ReleaseInterface(oldauth);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

