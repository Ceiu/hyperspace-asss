
/* dist: public */

#ifndef WIN32
#include <unistd.h>
/* #include <signal.h> */
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "asss.h"

#include "hscore_mysql.h"

#include "mysql.h"


struct db_cmd
{
	enum
	{
		CMD_NULL,
		CMD_QUERY,
	} type;
	query_callback cb;
	void *clos;
	int qlen, flags;
#define FLAG_NOTIFYFAIL 0x01
	char query[1];
};


local Iconfig *cfg;
local Ilogman *lm;

local const char *host, *user, *pw, *dbname;

local MPQueue dbq;
local pthread_t wthd;
local volatile int connected;

local MYSQL *mydb;


local void do_query(struct db_cmd *cmd)
{
	int q;

	if (lm) lm->Log(L_DRIVEL, "<hscore_mysql> query: %s", cmd->query);

	q = mysql_real_query(mydb, cmd->query, cmd->qlen);

	if (q)
	{
		if (lm)
			lm->Log(L_WARN, "<hscore_mysql> error in query: %s", mysql_error(mydb));
		if (cmd->cb && (cmd->flags & FLAG_NOTIFYFAIL))
			cmd->cb(mysql_errno(mydb), NULL, cmd->clos);
		return;
	}

	if (mysql_field_count(mydb) == 0)
	{
		/* this wasn't a select, we have no data to report */
		if (cmd->cb)
			cmd->cb(0, NULL, cmd->clos);
	}
	else
	{
		MYSQL_RES *res = mysql_store_result(mydb);

		if (res == NULL)
		{
			if (lm)
				lm->Log(L_WARN, "<hscore_mysql> error in store_result: %s", mysql_error(mydb));
			if (cmd->cb && (cmd->flags & FLAG_NOTIFYFAIL))
				cmd->cb(mysql_errno(mydb), NULL, cmd->clos);
			return;
		}

		if (cmd->cb)
			cmd->cb(0, (db_res*)res, cmd->clos);

		if (res)
			mysql_free_result(res);
	}
}


local void close_db(void *v)
{
	connected = 0;
	mysql_close((MYSQL*)v);
}

local void * work_thread(void *dummy)
{
	struct db_cmd *cmd;

	mydb = mysql_init(NULL);

	if (mydb == NULL)
	{
		if (lm) lm->Log(L_WARN, "<hscore_mysql> init failed: %s", mysql_error(mydb));
		return NULL;
	}

	int reconnect = 1;
	mysql_options(mydb, MYSQL_OPT_RECONNECT, &reconnect);

	pthread_cleanup_push(close_db, mydb);

	connected = 0;

	/* try to connect */
	if (lm)
		lm->Log(L_INFO, "<hscore_mysql> connecting to mysql db on %s, user %s, db %s", host, user, dbname);
	while (mysql_real_connect(mydb, host, user, pw, dbname, 0, NULL, 0) == NULL)
	{
		if (lm) lm->Log(L_WARN, "<hscore_mysql> connect failed: %s", mysql_error(mydb));
		pthread_testcancel();
		sleep(10);
		pthread_testcancel();
	}

	connected = 1;

	/* now serve requests */
	for (;;)
	{
		/* the pthread_cond_wait inside MPRemove is a cancellation point */
		cmd = MPRemove(&dbq);
		if (!cmd) break;

		switch (cmd->type)
		{
			case CMD_NULL:
				if (cmd->cb)
					cmd->cb(0, NULL, cmd->clos);
				break;

			case CMD_QUERY:
				do_query(cmd);
				break;
		}

		afree(cmd);
	}

	pthread_cleanup_pop(1);

	return NULL;
}


local int GetStatus()
{
	return connected;
}


local int Query(query_callback cb, void *clos, int notifyfail, const char *fmt, ...)
{
	va_list ap;
	const char *c;
	char *buf;
	int space = 0, dummy;
	struct db_cmd *cmd;

	va_start(ap, fmt);
	for (c = fmt; *c; c++)
		if (*c == '?')
			space += strlen(va_arg(ap, const char *)) * 2 + 3;
		else if (*c == '#')
		{
			dummy = va_arg(ap, int);
			space += 10;
		}
	va_end(ap);

	space += strlen(fmt);

	cmd = amalloc(sizeof(struct db_cmd) + space);
	cmd->type = CMD_QUERY;
	cmd->cb = cb;
	cmd->clos = clos;
	if (notifyfail)
		cmd->flags |= FLAG_NOTIFYFAIL;

	buf = cmd->query;

	va_start(ap, fmt);
	for (c = fmt; *c; c++)
	{
		if (*c == '?')
		{
			const char *str = va_arg(ap, const char *);
			*buf++ = '\'';
			/* don't use mysql_real_escape_string because the db might
			 * not be connected yet, and mysql crashes on that. */
			buf += mysql_escape_string(buf, str, strlen(str));
			*buf++ = '\'';
		}
		else if (*c == '#')
		{
			int arg = va_arg(ap, int);
			buf += sprintf(buf, "%i", arg);
		}
		else
			*buf++ = *c;
	}
	va_end(ap);

	*buf = 0;
	cmd->qlen = buf - cmd->query;

	MPAdd(&dbq, cmd);

	return 1;
}


local int GetRowCount(db_res *res)
{
	return mysql_num_rows((MYSQL_RES*)res);
}

local db_row * GetRow(db_res *res)
{
	return (db_row*)mysql_fetch_row((MYSQL_RES*)res);
}

local const char * GetField(db_row *row, int fieldnum)
{
	return ((MYSQL_ROW)row)[fieldnum];
}

local int GetLastInsertId(void)
{
	return mysql_insert_id(mydb);
}

local Ihscoremysql my_int =
{
	INTERFACE_HEAD_INIT(I_HSCORE_MYSQL, "hscore_mysql")
	GetStatus,
	Query,
	GetRowCount, GetRow, GetField,
	GetLastInsertId
};

EXPORT const char info_hscore_mysql[] = "v1.0 Grelminar, modified by Dr Brain";

EXPORT int MM_hscore_mysql(int action, Imodman *mm, Arena *arena)
{
	/* static sighandler_t oldh; */

	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!cfg)
			return MM_FAIL;

		connected = 0;
		MPInit(&dbq);

		/* cfghelp: Hyperspace:Hostname, global, string, mod: hscore_mysql
		 * The name of the mysql server. */
		host = cfg->GetStr(GLOBAL, "hyperspace", "hostname");
		/* cfghelp: Hyperspace:User, global, string, mod: hscore_mysql
		 * The mysql user to log in to the server as. */
		user = cfg->GetStr(GLOBAL, "hyperspace", "user");
		/* cfghelp: Hyperspace:Password, global, string, mod: hscore_mysql
		 * The password to log in to the mysql server as. */
		pw = cfg->GetStr(GLOBAL, "hyperspace", "password");
		/* cfghelp: Hyperspace:Database, global, string, mod: hscore_mysql
		 * The database on the mysql server to use. */
		dbname = cfg->GetStr(GLOBAL, "hyperspace", "database");

		if (!host || !user || !pw || !dbname)
			return MM_FAIL;

		host = astrdup(host);
		user = astrdup(user);
		pw = astrdup(pw);
		dbname = astrdup(dbname);

		/* oldh = signal(SIGPIPE, SIG_IGN); */

		pthread_create(&wthd, NULL, work_thread, NULL);

		mm->RegInterface(&my_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&my_int, ALLARENAS))
			return MM_FAIL;

		/* kill worker thread */
		MPAdd(&dbq, NULL);
		pthread_cancel(wthd);
		pthread_join(wthd, NULL);

		MPDestroy(&dbq);
		afree(host); afree(user); afree(pw); afree(dbname);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);

		/* signal(SIGPIPE, oldh); */

		return MM_OK;
	}
	return MM_FAIL;
}
