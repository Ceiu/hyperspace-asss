
/* dist: public */

#ifndef WIN32
#include <unistd.h>
/* #include <signal.h> */
#endif

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "asss.h"

#include "reldb.h"

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

#ifdef CFG_LOG_MYSQL_QUERIES
	if (lm) lm->Log(L_DRIVEL, "<mysql> query: %s", cmd->query);
#endif

	q = mysql_real_query(mydb, cmd->query, cmd->qlen);

	if (q)
	{
		if (lm)
			lm->Log(L_WARN, "<mysql> error in query: %s", mysql_error(mydb));
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
				lm->Log(L_WARN, "<mysql> error in store_result: %s", mysql_error(mydb));
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
	ticks_t tickcnt;

	mydb = mysql_init(NULL);

	if (mydb == NULL)
	{
		if (lm) lm->Log(L_WARN, "<mysql> init failed: %s", mysql_error(mydb));
		return NULL;
	}

	pthread_cleanup_push(close_db, mydb);

	connected = 0;

	/* try to connect */
	if (lm)
		lm->Log(L_INFO, "<mysql> connecting to mysql db on %s, user %s, db %s", host, user, dbname);
	while (mysql_real_connect(mydb, host, user, pw, dbname, 0, NULL, CLIENT_COMPRESS) == NULL)
	{
		if (lm) lm->Log(L_WARN, "<mysql> connect failed: %s", mysql_error(mydb));
		pthread_testcancel();
		sleep(60);
		pthread_testcancel();
	}

	connected = 1;
	tickcnt = current_millis();

	/* now serve requests */
	for (;;)
	{
		/* the pthread_cond_wait inside MPRemove is a cancellation point */
		cmd = MPRemove(&dbq);

		/* reconnect if necessary */
		if (mysql_ping(mydb))
		{
			if (mysql_real_connect(mydb, host, user, pw, dbname, 0, NULL, CLIENT_COMPRESS))
			{
				if (lm)
					lm->Log(L_INFO, "<mysql> Connection to database re-established.");
			}
			else
				if (lm) lm->Log(L_INFO, "<mysql> Attempt to re-establish database connection failed.");

			tickcnt = current_millis();
		}

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
			dummy = va_arg(ap, unsigned int);
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
			unsigned int arg = va_arg(ap, unsigned int);
			buf += sprintf(buf, "%u", arg);
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

local int GetFieldCount(db_res *res)
{
	return mysql_num_fields((MYSQL_RES*)res);
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

local int EscapeString(const char *str, char *buf, int buflen)
{
	int n = strlen(str);
	/* this is the formula for the maximal blowup of the escaped string.
	 * if the buffer doesn't have enough room, fail. */
	if (n * 2 + 1 > buflen)
		return FALSE;
	mysql_escape_string(buf, str, n);
	return TRUE;
}



local Ireldb my_int =
{
	INTERFACE_HEAD_INIT(I_RELDB, "mysql-db")
	GetStatus,
	Query,
	GetRowCount, GetFieldCount,
	GetRow, GetField,
	GetLastInsertId,
	EscapeString
};

EXPORT const char info_mysql[] = CORE_MOD_INFO("mysql");

EXPORT int MM_mysql(int action, Imodman *mm, Arena *arena)
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

		/* cfghelp: mysql:hostname, global, string, mod: mysql
		 * The name of the mysql server. */
		host = cfg->GetStr(GLOBAL, "mysql", "hostname");
		/* cfghelp: mysql:user, global, string, mod: mysql
		 * The mysql user to log in to the server as. */
		user = cfg->GetStr(GLOBAL, "mysql", "user");
		/* cfghelp: mysql:password, global, string, mod: mysql
		 * The password to log in to the mysql server as. */
		pw = cfg->GetStr(GLOBAL, "mysql", "password");
		/* cfghelp: mysql:database, global, string, mod: mysql
		 * The database on the mysql server to use. */
		dbname = cfg->GetStr(GLOBAL, "mysql", "database");

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


