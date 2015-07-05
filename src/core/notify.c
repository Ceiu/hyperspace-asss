
/* dist: public */

#include "asss.h"

local Ichat *chat;
local Icmdman *cmd;
local Iconfig *cfg;

local LinkedList notify_commands;

local helptext_t notify_help =
"Targets: none\n"
"Args: <message>\n"
"Sends the message to all online staff members.\n";

local void Cnotify(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (IS_ALLOWED(chat->GetPlayerChatMask(p), MSG_MODCHAT))
	{
		if (params && *params)
		{
			chat->SendModMessage("%s {%s} %s: %s", tc,
					arena->name, p->name, params);
			chat->SendMessage(p, "Message has been sent to online staff.");
		}
		else
		{
			const char *empty_reply = cfg->GetStr(GLOBAL, "Notify", "EmptyReply");

			if (!empty_reply)
				empty_reply = "Please include a message to send to online staff.";

			chat->SendMessage(p, "%s", empty_reply);
		}
	}
}

local void register_commands()
{
	char word[256];
	const char *tmp = NULL;
	const char *cmds;

	cmds = cfg->GetStr(GLOBAL, "Notify", "AlertCommand");
	/* default to ?cheater */
	if (!cmds)
		cmds = "cheater";

	LLInit(&notify_commands);
	while (strsplit(cmds, " ,:;", word, sizeof(word), &tmp))
	{
		ToLowerStr(word);
		LLAdd(&notify_commands, astrdup(word));
		cmd->AddCommand(word, Cnotify, ALLARENAS, notify_help);
	}
}

local void unregister_commands()
{
	Link *l;
	for (l = LLGetHead(&notify_commands); l; l = l->next)
		cmd->RemoveCommand(l->data, Cnotify, ALLARENAS);
	LLEnum(&notify_commands, afree);
	LLEmpty(&notify_commands);
}

EXPORT const char info_notify[] = CORE_MOD_INFO("notify");

EXPORT int MM_notify(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!chat || !cmd || !cfg) return MM_FAIL;

		register_commands();

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		unregister_commands();

		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		return MM_OK;
	}
	return MM_FAIL;
}

