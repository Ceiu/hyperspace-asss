
/* dist: public */

/* FIXME: KoTH game screws with the marks, so only one of the two may be used at a time */
/* FIXME: race conditions(?) */

#include <string.h>

#include "asss.h"

#include "packets/koth.h"

EXPORT const char info_mark[] = "v?.? by Catid <cat02e@fsu.edu>";


/* callbacks */
local void MyPA(Player *p, int action, Arena *arena);

/* local data */
local Imodman *mm;
local Iplayerdata *pd;
local Ichat *chat;
local Inet *net;
local Icmdman *cmd;

local int markey;

/* macros */

#define RETURN_IF(cond) if (cond) return;

/* constants */

#define NUMBER_OF_MARKS_PER_LINE 10
#define MARK_NAME_LENGTH_MAX 20
#define MARK_MAX_LISTED 15

/*
	MARK_SELF_MESSAGE

	If defined, the server will not allow players to ?mark themselves
*/
#define MARK_SELF_MESSAGE "Self-hatred is bad, mmkay?"

/*
	ACK_MARK_COMMANDS

	If defined, the server will acknowledge ?mark or ?unmark with a message.
*/
#define ACK_MARK_COMMANDS

/* types */

typedef struct marklistednode {
	char name[MARK_NAME_LENGTH_MAX];
} marklistednode;

typedef struct kothmark {
	LinkedList listed;
	int listCount;
} kothmark;

/* freebies */

local void listMarks(kothmark *md, Player *p)
{
	char _buf[256];
	int cnt;
	Link *link;

	cnt = 0;

	for (link = LLGetHead(&md->listed); link; link = link->next)
	{
		if (cnt % NUMBER_OF_MARKS_PER_LINE == 0)
			strcpy(_buf,"marks: ");
		else
			strcat(_buf,"  ");

		strcat(_buf,((marklistednode*)link->data)->name);

		if ((++cnt) % NUMBER_OF_MARKS_PER_LINE == 0)
			chat->SendMessage(p,"%s",_buf);
	}

	if (cnt == 0)
		chat->SendMessage(p,"You have not marked any players");
	else if (cnt % NUMBER_OF_MARKS_PER_LINE != 0)
		chat->SendMessage(p,"%s",_buf);
}

local int markExists(kothmark *md, const char *name)
{
	Link *link;

	for (link = LLGetHead(&md->listed); link; link = link->next)
		if (strcasecmp(name,((marklistednode*)link->data)->name) == 0)
			return 1;

	return 0;
}

local int unmarkPlayer(kothmark *md, const char *name)
{
	Link *link;

	for (link = LLGetHead(&md->listed); link; link = link->next)
		if (strcasecmp(name,((marklistednode*)link->data)->name) == 0)
		{
			afree(link->data);
			LLRemove(&md->listed, link->data);

			md->listCount--;
			return 1;
		}

	return 0;
}

local void adjustMark(Player *p, Player *pp, int option)
{
	struct S2CKoth pkt =
		{ S2C_KOTH, option, 0, pp->pid };
	net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
}

/* ?mark command */

local helptext_t mark_help =
	"Targets: Player, Arena\n"
	"Syntax: /?mark, ?mark <player>, ?mark (list only)\n"
	"Marks player in purple on your radar.";

local void Cmark(const char *tc, const char *params, Player *p, const Target *target)
{
	kothmark *md;

	md = (kothmark*)PPDATA(p, markey);

	if (*params || (target->type == T_PLAYER))
	{
		marklistednode *node;
		const char *name;
		Player *pp;

		if (*params)
		{
			pp = 0;
			name = params;
		}
		else
		{
			pp = target->u.p;
			name = pp->name;
		}

#ifdef MARK_SELF_MESSAGE
		if (strcasecmp(p->name, name) == 0) {
			chat->SendMessage(p,MARK_SELF_MESSAGE);
			return;
		}
#endif
		if (strlen(name) >= MARK_NAME_LENGTH_MAX) {
			chat->SendMessage(p,"Mark names must be under %i characters",MARK_NAME_LENGTH_MAX);
			return;
		}
		if (md->listCount >= MARK_MAX_LISTED) {
			chat->SendMessage(p,"You may not mark more than %i players",MARK_MAX_LISTED);
			return;
		}
		if (markExists(md,name)) {
#ifdef ACK_MARK_COMMANDS
			chat->SendMessage(p,"You have already marked %s", name);
#endif
			return;
		}

		node = amalloc(sizeof(marklistednode));

		RETURN_IF(!node)

		astrncpy(node->name, name, MARK_NAME_LENGTH_MAX);

		LLAdd(&md->listed,node);
		++md->listCount;

#ifdef ACK_MARK_COMMANDS
		chat->SendMessage(p,"Marked %s",name);
#endif

		if (!pp) {
			pp = pd->FindPlayer(name);
			RETURN_IF(!pp)
		}

		RETURN_IF(pp->arena != p->arena)

		adjustMark(p, pp, KOTH_ACTION_ADD_CROWN);
	}
	else
	{
		listMarks(md,p);
	}
}

/* ?unmark command */

local helptext_t unmark_help =
	"Targets: Player, Arena\n"
	"Syntax: /?unmark, ?unmark <player>\n"
	"Removes mark on a player.";

local void Cunmark(const char *tc, const char *params, Player *p, const Target *target)
{
	kothmark *md;

	md = (kothmark*)PPDATA(p, markey);

	if (*params || (target->type == T_PLAYER))
	{
		const char *name;
		Player *pp;

		if (*params)
		{
			pp = 0;
			name = params;
		}
		else
		{
			pp = target->u.p;
			name = pp->name;
		}

		if (strlen(name) >= MARK_NAME_LENGTH_MAX) {
			chat->SendMessage(p,"Mark names must be under %i characters",MARK_NAME_LENGTH_MAX);
			return;
		}

#ifdef ACK_MARK_COMMANDS
		chat->SendMessage(p,
						  unmarkPlayer(md,name) ? "Unmarked %s" : "%s was not marked",
						  name);
#else
		if (!unmarkPlayer(md,name))
			chat->SendMessage(p,"%s was not marked",name);
#endif

		if (!pp) {
			pp = pd->FindPlayer(name);
			RETURN_IF(!pp)
		}

		RETURN_IF(pp->arena != p->arena)

		adjustMark(p, pp, KOTH_ACTION_REMOVE_CROWN);
	}
#ifdef ACK_MARK_COMMANDS
	else
	{
		chat->SendMessage(p, "You must specify a player to unmark.  Send ?help unmark for the syntax");
	}
#endif
}



/* /?destroy command */

/* STOLEN: from game:game.c -- if the logic changes there, it needs to change here also */

/* End of stolen code */

local helptext_t destroy_help =
	"Targets: Player\n"
	"Syntax: /?destroy\n"
	"Simulates the player getting killed.";

local void Cdestroy(const char *tc, const char *params, Player *p, const Target *target)
{

	if (target->type == T_PLAYER)
	{
		
	}
	else
	{
		chat->SendMessage(p, "You must send this command privately to a single player");
	}
}


void MyPA(Player *p, int action, Arena *arena)
{
	kothmark *md;

	if (action == PA_CONNECT)
	{
		md = (kothmark*)PPDATA(p, markey);

		LLInit(&md->listed);
		md->listCount = 0;
	}
	else if (action == PA_DISCONNECT)
	{
		Link *link;

		md = (kothmark*)PPDATA(p, markey);

		for (link = LLGetHead(&md->listed); link; link = link->next)
			afree(link->data);

		LLEmpty(&md->listed);
	}
	else if (action == PA_ENTERARENA)
	{	/* FIXME: must be loaded after certain modules
				  because the Player Entering packet
				  needs to be sent prior to this KoTH
				  packet. */
		Link *link;
		Player *pp;
		kothmark *mdd;

		md = (kothmark*)PPDATA(p, markey);

		FOR_EACH_PLAYER_P(pp, mdd, markey)
			if (pp->arena == p->arena)
			{
				if (markExists(mdd, p->name))
					adjustMark(pp, p, KOTH_ACTION_ADD_CROWN);
				if (markExists(md, pp->name))
					adjustMark(p, pp, KOTH_ACTION_ADD_CROWN);
			}
	}
}

/* ?bounty command */

/*local helptext_t bounty_help =
	"Targets: Player\n"
	"Syntax: /?bounty <points>\n"
	"Set a bounty on another player.";

local void Cbounty(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_PLAYER || target->u.p == p)
	{
		chat->SendMessage(p,"Only valid target for ?bounty is another player.");
		return;
	}
}*/

EXPORT int MM_mark(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!pd || !chat || !net) return MM_FAIL;

		markey = pd->AllocatePlayerData(sizeof(kothmark));
		if (markey == -1) return MM_FAIL;

		cmd->AddCommand("mark", Cmark, ALLARENAS, mark_help);
		cmd->AddCommand("unmark", Cunmark, ALLARENAS, unmark_help);
		cmd->AddCommand("destroy", Cdestroy, ALLARENAS, destroy_help);

		mm->RegCallback(CB_PLAYERACTION, MyPA, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		kothmark *md;
		Link *link, *link2;
		Player *p;

		mm->UnregCallback(CB_PLAYERACTION, MyPA, ALLARENAS);

		cmd->RemoveCommand("mark", Cmark, ALLARENAS);
		cmd->RemoveCommand("unmark", Cunmark, ALLARENAS);
		cmd->RemoveCommand("destroy", Cdestroy, ALLARENAS);

		pd->Lock();
		FOR_EACH_PLAYER_P(p, md, markey)
		{
			for (link2 = LLGetHead(&md->listed); link2; link2 = link2->next)
				afree(link2->data);

			LLEmpty(&md->listed);
		}
		pd->Unlock();

		pd->FreePlayerData(markey);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cmd);
		return MM_OK;
	}
	return MM_FAIL;
}
