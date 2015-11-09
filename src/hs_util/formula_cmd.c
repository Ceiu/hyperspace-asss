#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "formula.h"

typedef struct PData
{
	HashTable *vars;
} PData;

local Imodman *mm;
local Ilogman *lm;
local Icmdman *cmd;
local Iplayerdata *pd;
local Ichat *chat;
local Iformula *formula;
local Iconfig *cfg;

local int pdata_key;

local int count_vars_callback(const char *key, void *val, void *clos)
{
	int *count = clos;
	*count = *count + 1;
	return FALSE;
}

local int count_vars(HashTable *table)
{
	int count = 0;
	HashEnum(table, count_vars_callback, &count);
	return count;
}

local int allow_var_creation(Player *p)
{
	PData *pdata = PPDATA(p, pdata_key);
	int count = count_vars(pdata->vars);

	/* cfghelp: Formula:MaxVariables, global, int, def: 10, mod: formula_cmd
	 * How many variables a player can set with ?formula. */
	int max_count = cfg->GetInt(GLOBAL, "Fomula", "MaxVariables", 10);

	return count < max_count;
}

local helptext_t formula_help =
"Targets: none\n"
"Args: [<var>=]<formula>\n"
"Evaluates a formula. Variables can be set with var=<formula>."
"Variables can be unset with ?unset. If no variable is specified,"
"the result will be put into the variable 'ans'.";

local void Cformula(const char *cmd, const char *params, Player *p, const Target *target)
{
	if (params && *params)
	{
		char var_name_buf[MAX_VAR_NAME_LENGTH];
		char error_buf[200];
		error_buf[0] = '\0';

		// parse the formula
		Formula *f = formula->ParseFormula(params, error_buf, sizeof(error_buf));

		if (f)
		{
			if (allow_var_creation(p))
			{
				PData *pdata = PPDATA(p, pdata_key);
				double value = formula->EvaluateFormula(f, pdata->vars, var_name_buf, error_buf, sizeof(error_buf));
				formula->FreeFormula(f);

				if (error_buf[0] == '\0')
				{
					chat->SendMessage(p, "%s = %lf", var_name_buf, value);
					// chat->SendMessage(p, "You have too many variables. Free some with ?unset");
				}
				else
				{
					chat->SendMessage(p, "Evaluation Error: %s", error_buf);
				}
			}
			else
			{
				PData *pdata = PPDATA(p, pdata_key);
				double value = formula->EvaluateFormula(f, pdata->vars, NULL, error_buf, sizeof(error_buf));
				formula->FreeFormula(f);

				if (error_buf[0] == '\0')
				{
					chat->SendMessage(p, "You have too many variables. Free some with ?unset");
					chat->SendMessage(p, "%lf", value);
				}
				else
				{
					chat->SendMessage(p, "Evaluation Error: %s", error_buf);
				}
			}
		}
		else
		{
			chat->SendMessage(p, "Parse Error: %s", error_buf);
		}
	}
	else
	{
		chat->SendMessage(p, "You must enter a formula to evaluate.");
	}
}

local helptext_t unset_help =
"Targets: none\n"
"Args: <var>\n"
"Releases a variable set with ?formula";

local void Cunset(const char *cmd, const char *params, Player *p, const Target *target)
{
	if (params && *params)
	{
		PData *pdata = PPDATA(p, pdata_key);
		FormulaVariable *var;

		if (*params == '$')
		{
			// ignore a leading $
			params++;
		}

		var = HashGetOne(pdata->vars, params);

		if (var)
		{
			HashRemove(pdata->vars, params, var);
			afree(var);
			chat->SendMessage(p, "Unset %s", params);
		}
		else
		{
			chat->SendMessage(p, "No such variable '%s'", params);
		}
	}
	else
	{
		chat->SendMessage(p, "Usage: ?unset <var>");
	}
}

local int enum_vars(const char *key, void *val, void *clos)
{
	LinkedList *list = clos;

	LLAdd(list, key);

	return FALSE;
}

local helptext_t vars_help =
"Targets: none\n"
"Args: none\n"
"Lists variables set with ?formula";

local void Cvars(const char *cmd, const char *params, Player *p, const Target *target)
{
	PData *pdata = PPDATA(p, pdata_key);
	LinkedList list;

	LLInit(&list);

	HashEnum(pdata->vars, enum_vars, &list);

	if (LLGetHead(&list) != NULL)
	{
		Link *link;
		StringBuffer sb;
		SBInit(&sb);

		chat->SendMessage(p, "Variables:");

		for (link = LLGetHead(&list); link; link = link->next)
		{
			SBPrintf(&sb, ", %s", (const char*)link->data);
		}
		chat->SendWrappedText(p, SBText(&sb, 2));
		SBDestroy(&sb);

		LLEmpty(&list);
	}
	else
	{
		chat->SendMessage(p, "No Variables.");
	}
}

local void init_players()
{
	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		PData *pdata = PPDATA(p, pdata_key);
		pdata->vars = HashAlloc();
	}
	pd->Unlock();
}

local void free_players()
{
	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		PData *pdata = PPDATA(p, pdata_key);
		if (pdata->vars)
		{
			HashEnum(pdata->vars, hash_enum_afree, NULL);
			HashFree(pdata->vars);
		}
	}
	pd->Unlock();
}

local int var_free_enum(const char *key, void *val, void *clos)
{
	FormulaVariable *var = val;
	if (var->name)
		afree(var->name);
	afree(var);
	return TRUE;
}

local void player_action(Player *p, int action, Arena *arena)
{
	PData *pdata = PPDATA(p, pdata_key);

	if (action == PA_CONNECT)
	{
		FormulaVariable *var = amalloc(sizeof(FormulaVariable));
		var->name = astrdup("me");
		var->type = VAR_TYPE_PLAYER;
		var->player = p;
		pdata->vars = HashAlloc();
		HashAdd(pdata->vars, var->name, var);
	}
	else if (action == PA_DISCONNECT)
	{
		HashEnum(pdata->vars, var_free_enum, NULL);
		HashFree(pdata->vars);
	}
}

EXPORT const char info_formula_cmd[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_formula_cmd(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		formula = mm->GetInterface(I_FORMULA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!lm || !cmd || !pd || !chat || !formula || !cfg)
			return MM_FAIL;

		pdata_key = pd->AllocatePlayerData(sizeof(PData));
		if (pdata_key == -1)
			return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, player_action, ALLARENAS);

		init_players();

		cmd->AddCommand("formula", Cformula, ALLARENAS, formula_help);
		cmd->AddCommand("unset", Cunset, ALLARENAS, unset_help);
		cmd->AddCommand("vars", Cvars, ALLARENAS, vars_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("formula", Cformula, ALLARENAS);
		cmd->RemoveCommand("unset", Cunset, ALLARENAS);
		cmd->RemoveCommand("vars", Cvars, ALLARENAS);

		mm->UnregCallback(CB_PLAYERACTION, player_action, ALLARENAS);

		free_players();

		pd->FreePlayerData(pdata_key);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(formula);
		mm->ReleaseInterface(cfg);

		return MM_OK;
	}
	return MM_FAIL;
}

