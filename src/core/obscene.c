
/* dist: public */

#include <string.h>

#include "asss.h"
#include "obscene.h"

local Imodman *mm;
local Iconfig *cfg;
local Ilogman *lm;
local Icmdman *cmd;

local LinkedList obscene_words;
local int replace_count;
local pthread_mutex_t obscene_words_mtx = PTHREAD_MUTEX_INITIALIZER;


local void clear_obscene()
{
	LLEnum(&obscene_words, afree);
	LLEmpty(&obscene_words);
}

local void load_obscene()
{
	/* cfghelp: Chat:Obscene, global, string
	 * A space-separated list of obscene words to filter. Words starting
	 * with a question mark are encoded with rot-13. */
	const char *words = cfg->GetStr(GLOBAL, "Chat", "Obscene");

	pthread_mutex_lock(&obscene_words_mtx);

	clear_obscene();

	if (words && strlen(words) >= 1)
	{
		char word[128];
		const char *tmp = NULL;

		while (strsplit(words, " \t", word, sizeof(word), &tmp))
		{
			ToLowerStr(word);
			/* if the first character of a word is "?", the rest is
			 * rot-13 encoded. */
			if (word[0] == '?')
			{
				char *c;
				for (c = word + 1; *c; c++)
					if (*c >= 'a' && *c <= 'm')
						*c += 13;
					else if (*c >= 'n' && *c <= 'z')
						*c -= 13;
				LLAdd(&obscene_words, astrdup(word+1));
			}
			else
				LLAdd(&obscene_words, astrdup(word));
		}

		lm->Log(L_INFO, "<obscene> loaded %d obscene words",
				LLCount(&obscene_words));
	}

	pthread_mutex_unlock(&obscene_words_mtx);
}


local int Filter(char *line)
{
	static const char replace[] =
		"%@$&%*!#@&%!#&*$#?@!*%@&!%#&%!?$*#!*$&@#&%$!*%@#&%!@&#$!*@&$%*@?";
	int filtered = FALSE;
	Link *l;

	pthread_mutex_lock(&obscene_words_mtx);
	for (l = LLGetHead(&obscene_words); l; l = l->next)
	{
		const char *word = l->data;
		char *found = strcasestr(line, word);

		if (found)
		{
			int wlen = strlen(word);
			filtered = TRUE;
			while (found)
			{
				int pos = (5 * replace_count++) % strlen(replace);
				int leftover = pos + wlen - strlen(replace);
				if (leftover > 0)
				{
					int atend = strlen(replace) - pos;
					memcpy(found, replace + pos, atend);
					memcpy(found + atend, replace, leftover);
				}
				else
					memcpy(found, replace + pos, wlen);
				found = strcasestr(found + wlen, word);
			}
		}
	}
	pthread_mutex_unlock(&obscene_words_mtx);

	return filtered;
}


local helptext_t obscene_help =
"Targets: none\n"
"Args: none\n"
"Toggles the obscene word filter.\n";

local void Cobscene(const char *cmd, const char *params, Player *p, const Target *target)
{
	Ichat *chat = mm->GetInterface(I_CHAT, ALLARENAS);

	if (p->flags.obscenity_filter)
	{
		p->flags.obscenity_filter = 0;
		chat->SendMessage(p, "Obscene filter OFF");
	}
	else
	{
		p->flags.obscenity_filter = 1;
		chat->SendMessage(p, "Obscene filter ON");
	}

	mm->ReleaseInterface(chat);
}


local Iobscene obsceneint =
{
	INTERFACE_HEAD_INIT(I_OBSCENE, "obscene")
	Filter
};

EXPORT const char info_obscene[] = CORE_MOD_INFO("obscene");

EXPORT int MM_obscene(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!cfg || !lm) return MM_FAIL;

		mm->RegCallback(CB_GLOBALCONFIGCHANGED, load_obscene, ALLARENAS);

		if (cmd)
			cmd->AddCommand("obscene", Cobscene, ALLARENAS, obscene_help);

		LLInit(&obscene_words);
		replace_count = 0;
		load_obscene();

		mm->RegInterface(&obsceneint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&obsceneint, ALLARENAS))
			return MM_FAIL;

		if (cmd)
			cmd->RemoveCommand("obscene", Cobscene, ALLARENAS);

		pthread_mutex_lock(&obscene_words_mtx);
		clear_obscene();
		pthread_mutex_unlock(&obscene_words_mtx);

		mm->UnregCallback(CB_GLOBALCONFIGCHANGED, load_obscene, ALLARENAS);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);

		return MM_OK;
	}
	return MM_FAIL;
}

