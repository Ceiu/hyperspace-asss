
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "asss.h"
#include "filetrans.h"


struct transfer_data
{
	Player *from, *to;
	char clientpath[256];
	char fname[16];
};

local LinkedList offers;

local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)

local Ifiletrans *ft;
local Icmdman *cmd;
local Ichat *chat;
local Iplayerdata *pd;
local Ilogman *lm;


local int is_sending(Player *p)
{
	Link *l;
	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->from == p)
			{ UNLOCK(); return 1; }
	UNLOCK();
	return 0;
}

local int is_recving(Player *p)
{
	Link *l;
	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
		if (((struct transfer_data*)(l->data))->to == p)
			{ UNLOCK(); return 1; }
	UNLOCK();
	return 0;
}

local int cancel_files(Player *p)
{
	int cancelled = FALSE;
	Link *l, *n;
	LOCK();
	for (l = LLGetHead(&offers); l; l = n)
	{
		struct transfer_data *td = l->data;
		n = l->next;
		if (td->from == p || td->to == p)
		{
			afree(td);
			LLRemove(&offers, td);
			cancelled = TRUE;
		}
	}
	UNLOCK();

	return cancelled;
}


local void uploaded(const char *path, void *clos)
{
	struct transfer_data *td = clos;
	const char *t1, *t2;

	LOCK();
	if (path == NULL)
	{
		lm->Log(L_WARN, "<sendfile> upload failed");
	}
	else if (td->to->status != S_PLAYING || !IS_STANDARD(td->to))
	{
		lm->Log(L_WARN,
				"<sendfile> bad state or client type for recipient of received file");
		remove(path);
	}
	else
	{
		/* try to get basename of the client path */
		t1 = strrchr(td->clientpath, '/');
		t2 = strrchr(td->clientpath, '\\');
		if (t2 > t1) t1 = t2;
		t1 = t1 ? t1 + 1 : td->clientpath;

		if (ft->SendFile(td->to, path, t1, 1) != MM_OK)
			remove(path);
		else
			chat->SendMessage(td->from, "File sent.");
	}
	UNLOCK();
	afree(td);
}

local helptext_t sendfile_help =
"Module: sendfile\n"
"Targets: player\n"
"Args: none\n"
"Offer someone a file from your client's directory.\n"
"Only one file can be offered at once.\n";

local void Csendfile(const char *tc, const char *params, Player *p, const Target *target)
{
	struct transfer_data *td;
	Player *t = target->u.p;

	if (target->type != T_PLAYER) return;

	if (!*params) return;

	if (!IS_STANDARD(p))
	{
		chat->SendMessage(p, "You must be in a game client to offer files.");
		return;
	}

	if (!IS_STANDARD(t))
	{
		chat->SendMessage(p, "You can only offer files to players in game clients.");
		return;
	}

	if (t == p)
	{
		chat->SendMessage(p, "You cannot send files to yourself.");
		return;
	}

	if (is_sending(p))
	{
		chat->SendMessage(p, "You are currently sending a file. "
			"Use ?cancelfile to cancel the current file transfer.");
		return;
	}

	if (is_recving(t))
	{
		chat->SendMessage(p, "That player is currently receiving a file.");
		return;
	}

	if (p->p_ship != SHIP_SPEC)
	{
		chat->SendMessage(p, "You must be in spectator mode to offer files.");
		return;
	}

	if (t->p_ship != SHIP_SPEC)
	{
		chat->SendMessage(p, "You must offer files to another player in spectator mode.");
		return;
	}

	td = amalloc(sizeof(*td));
	td->from = p;
	td->to = t;
	astrncpy(td->clientpath, params, sizeof(td->clientpath));

	chat->SendMessage(p, "Waiting for %s to accept your file.", t->name);
	chat->SendMessage(t, "%s wants to send you the file \"%s\". To accept it, type ?acceptfile.",
			p->name, td->clientpath);
	LOCK();
	LLAdd(&offers, td);
	UNLOCK();
}

local helptext_t cancelfile_help =
"Module: sendfile\n"
"Targets: none\n"
"Args: none\n"
"Withdraw your previously offered files.\n";

local void Ccancelfile(const char *tc, const char *params, Player *p, const Target *target)
{
	if (cancel_files(p))
		chat->SendMessage(p, "Your file offers have been canceled.");
}

local helptext_t acceptfile_help =
"Module: sendfile\n"
"Targets: none\n"
"Args: none\n"
"Accept a file that has been offered to you.\n";

local void Cacceptfile(const char *tc, const char *params, Player *p, const Target *t)
{
	Link *l;

	LOCK();
	for (l = LLGetHead(&offers); l; l = l->next)
	{
		struct transfer_data *td = l->data;
		if (td->to == p)
		{
			chat->SendMessage(td->from, "%s is accepting your file.",
				td->to->name);
			chat->SendMessage(p, "File accepted. Transferring...");
			ft->RequestFile(td->from, td->clientpath, uploaded, td);
			LLRemove(&offers, td);
			goto done;
		}
	}
	chat->SendMessage(p, "Nobody has offered any files to you.");
done:
	UNLOCK();
}


local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_CONNECT || action == PA_DISCONNECT)
		cancel_files(p);
}

EXPORT const char info_sendfile[] = CORE_MOD_INFO("sendfile");

EXPORT int MM_sendfile(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		ft = mm->GetInterface(I_FILETRANS, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!ft || !cmd || !chat || !pd || !lm)
			return MM_FAIL;

		cmd->AddCommand("sendfile", Csendfile, ALLARENAS, sendfile_help);
		cmd->AddCommand("acceptfile", Cacceptfile, ALLARENAS, acceptfile_help);
		cmd->AddCommand("cancelfile", Ccancelfile, ALLARENAS, cancelfile_help);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		LLInit(&offers);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		cmd->RemoveCommand("sendfile", Csendfile, ALLARENAS);
		cmd->RemoveCommand("acceptfile", Cacceptfile, ALLARENAS);
		cmd->RemoveCommand("cancelfile", Ccancelfile, ALLARENAS);
		LLEnum(&offers, afree);
		LLEmpty(&offers);
		mm->ReleaseInterface(ft);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

