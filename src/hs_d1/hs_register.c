#include <string.h>
#include <stdlib.h>
#include "asss.h"
#include "hscore.h"
#include "hscore_mysql.h"
#include "hscore_database.h"

local Ichat *chat;
local Icmdman *cmd;
local Ihscoremysql *sql;
local Ihscoredatabase *db;
local Ilogman *lm;

typedef struct Registration
{
	Player *p;
	char password[32];
	int exists;
} Registration;

#define getHSid(p) db->getPlayerWalletId(p)
local void C_register(const char *command, const char *params, Player *p, const Target *target);
local void dbcb_check(int status, db_res *res, void *clos);
local void dbcb_create(int status, db_res *res, void *clos);
local void dbcb_update(int status, db_res *res, void *clos);

local helptext_t register_help  =
"Targets: none\n"
"Args: <password>\n"
"Registers your account on the the website.\n"
"If you are already registered, changes your password.";

local void C_register(const char *tc, const char *params, Player *p, const Target *target)
{
	if(!strlen(params))
	{
		chat->SendMessage(p, "You need to specify a password.");
	}
	else
	{
		Registration *reg = amalloc(sizeof(Registration));
		reg->p = p;
		astrncpy(reg->password, params, sizeof(reg->password));
		sql->Query(dbcb_check, reg, 0, "SELECT user_id FROM e107_user WHERE user_name = ?",p->name, params);
	}
}

local void dbcb_check(int status, db_res *res, void *clos)
{
	Registration *reg = (Registration *)clos;
	if(status != 0)
	{
		chat->SendMessage(reg->p, "Unexpected database error.");
		afree(reg);
	}
	else
	{
		int results = sql->GetRowCount(res);
		if(results > 0)
		{
			reg->exists = 1;
			db_row *row = sql->GetRow(res);
			int user_id = atoi(sql->GetField(row, 0));
			sql->Query(dbcb_update, reg, 0, "UPDATE e107_user SET user_password = md5(?) WHERE user_id = #", reg->password, user_id);
			sql->Query(0, reg, 0, "UPDATE e107_user_extended SET user_hsid = # WHERE user_extended_id = #", getHSid(reg->p), user_id);
		}
		else
		{
			sql->Query(dbcb_create, reg, 0, "INSERT INTO e107_user (user_name, user_loginname, user_password, user_join) VALUES (?, ?, md5(?), UNIX_TIMESTAMP())", reg->p->name, reg->p->name, reg->password);
		}
	}
}

local void dbcb_create(int status, db_res *res, void *clos)
{
	Registration *reg = (Registration *)clos;
	if(status != 0)
	{
		chat->SendMessage(reg->p, "Unexpected database error.");
	}
	else
	{
		int user_id = sql->GetLastInsertId();
		sql->Query(0, reg, 0, "INSERT INTO e107_user_extended (user_extended_id, user_hsid) VALUES (#,#)", user_id, getHSid(reg->p));
		chat->SendMessage(reg->p, "Successfully registered. You may now log into the website.");
	}
	afree(reg);
}

local void dbcb_update(int status, db_res *res, void *clos)
{
	Registration *reg = (Registration *)clos;
	if(status != 0)
	{
		chat->SendMessage(reg->p, "Unexpected database error.");
	}
	else
	{
		chat->SendMessage(reg->p, "Your website password has been changed.");
	}
	afree(reg);
}

EXPORT int MM_hs_register(int action, Imodman *mm, Arena *arenas)
{
	if (action == MM_LOAD)
	{
		lm   = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd  = mm->GetInterface(I_CMDMAN, ALLARENAS);
		sql  = mm->GetInterface(I_HSCORE_MYSQL, ALLARENAS);
		db   = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!chat || !cmd || !sql || !db) return MM_FAIL;

		cmd->AddCommand("signup", C_register, ALLARENAS, register_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("signup", C_register, ALLARENAS);

		mm->ReleaseInterface(db);
		mm->ReleaseInterface(sql);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}
