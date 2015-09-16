
/* dist: public */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "asss.h"

#include "reldb.h"


#define TABLE_NAME "logins"


/* global data */

local Imodman *mm;
local Ilogman *lm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Icmdman *cmd;
local Ichat *chat;
local Inet *net;
local Ichatnet *chatnet;
local Ireldb *db;


#define CREATE_LOGIN_TABLE \
"CREATE TABLE IF NOT EXISTS " TABLE_NAME " (" \
"  name char(24) NOT NULL default ''," \
"  ip int(10) unsigned NOT NULL default '0'," \
"  macid int(10) unsigned NOT NULL default '0'," \
"  permid int(10) unsigned NOT NULL default '0'," \
"  lastseen timestamp NOT NULL," \
"  UNIQUE KEY unq_idx (name,ip,macid,permid)," \
"  KEY ip (ip)," \
"  KEY macid (macid)" \
")"


#define suffix(n) (((n) == 1) ? "" : "s")


local void init_db(void)
{
	/* make sure the logins table exists */
	db->Query(NULL, NULL, 0, CREATE_LOGIN_TABLE);
}


local void playera(Player *p, int action, Arena *arena)
{
	/* ignore fake players */
	if (!IS_HUMAN(p))
		return;

	if (action == PA_CONNECT)
	{
		struct net_client_stats ncs;
		struct chat_client_stats ccs;

		char *name = p->name;
		char *ip = "0";
		unsigned int macid;
		unsigned int permid;

		/* get ip and optionally macid/permid */
		if (IS_STANDARD(p) && net)
		{
			net->GetClientStats(p, &ncs);
			ip = ncs.ipaddr;
		}
		else if (IS_CHAT(p) && chatnet)
		{
			chatnet->GetClientStats(p, &ccs);
			ip = ccs.ipaddr;
		}

		macid = p->macid;
		permid = p->permid;

		/* the ip address will be in dotted decimal form, let mysql do
		 * the conversion to an integer. */
		db->Query(NULL, NULL, 0,
				"replace into " TABLE_NAME " (name, ip, macid, permid) "
				"values (?, inet_aton(?), #, #)",
				name, ip, macid, permid);
	}
}



local void dbcb_nameipmac(int status, db_res *res, void *clos)
{
	Player *p = (Player*)clos;
	int results;
	db_row *row;

	if (!p->flags.during_query)
	{
		if (lm)
			lm->LogP(L_WARN, "aliasdb", p, "recieved query result he didn't ask for");
		return;
	}

	p->flags.during_query = 0;

	if (status != 0 || res == NULL)
	{
		chat->SendMessage(p, "Unexpected database error.");
		return;
	}

	results = db->GetRowCount(res);

	if (results == 1)
		chat->SendMessage(p, "There was 1 match to your query.");
	else
		chat->SendMessage(p, "There were %d matches to your query.", results);

	if (results == 0) return;

	chat->SendMessage(p, "%-20.20s %-15.15s %-10.10s %s", "NAME", "IP", "MACID", "DAYS AGO");

	while ((row = db->GetRow(res)))
	{
		chat->SendMessage(p, "%-20.20s %-15.15s %-10.10s %3s",
				db->GetField(row, 0),
				db->GetField(row, 1),
				db->GetField(row, 2),
				db->GetField(row, 3));
	}
}

local void dbcb_alias(int status, db_res *res, void *clos)
{
	Player *p = (Player*)clos;
	int results;
	db_row *row;

	if (!p->flags.during_query)
	{
		if (lm)
			lm->LogP(L_WARN, "aliasdb", p, "recieved query result he didn't ask for");
		return;
	}

	p->flags.during_query = 0;

	if (status != 0 || res == NULL)
	{
		chat->SendMessage(p, "Unexpected database error.");
		return;
	}

	results = db->GetRowCount(res);

	if (results == 1)
		chat->SendMessage(p, "There was 1 match to your query.");
	else
		chat->SendMessage(p, "There were %d matches to your query.", results);

	if (results == 0) return;

	chat->SendMessage(p, "%-7.7s %-20.20s %-15.15s %-4.4s","MATCH", "NAME", "IP/MACID", "DAYS");

	while ((row = db->GetRow(res)))
	{
		chat->SendMessage(p, "%-7.7s %-20.20s %-15.15s %-4.4s",
				db->GetField(row, 0),
				db->GetField(row, 1),
				db->GetField(row, 2),
				db->GetField(row, 3));
	}
}


local helptext_t alias_help =
"Module: aliasdb\n"
"Targets: player or none\n"
"Args: [<name>]\n"
"Queries the alias database for players matching from the name, ip, or\n"
"macid of the target. Only works on MySQL 4 or later.\n";

local void Calias(const char *tc, const char *params, Player *p, const Target *target)
{
	const char *name = NULL;

	if (target->type == T_ARENA)
		name = params;
	else if (target->type == T_PLAYER)
		name = target->u.p->name;

	if (!name || !*name)
	{
		chat->SendMessage(p, "Invalid syntax. See help for ?alias.");
		return;
	}

	p->flags.during_query = 1;
	db->Query(dbcb_alias, p, 1,
			"(SELECT 'IPAddr' AS 'Match Type',Alias.name as Name, "
			"INET_NTOA(Alias.ip) AS 'IP/Mac', "
			"TO_DAYS(now()) - TO_DAYS(Alias.lastseen) as `daysago` "
			"FROM " TABLE_NAME " AS Source, " TABLE_NAME " AS Alias "
			"WHERE "
			"Source.name=? AND "
			"Source.ip=Alias.ip"
			") UNION ( "
			"SELECT 'MacId' AS 'Match Type',Alias.name as Name, "
			"Alias.macid AS 'IP/Mac', "
			"TO_DAYS(now()) - TO_DAYS(Alias.lastseen) as `daysago` "
			"FROM " TABLE_NAME " AS Source, " TABLE_NAME " AS Alias "
			"WHERE "
			"Source.name=? AND "
			"Source.macid > 400 AND "
			"Source.macid <> 305419896 AND " /* exclude 0x12345678, used by subchat */
			"Source.macid=Alias.macid ) "
			"ORDER BY `daysago` ASC LIMIT 150",
			name, name);
}


local helptext_t qip_help =
"Targets: none\n"
"Args: <ip address or pattern>\n"
"Queries the alias database for players connecting from that ip.\n"
"Queries can be an exact addreess, ?qip 216.34.65.%, or ?qip 216.34.65.0/24.\n";

local void Cqip(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_ARENA)
		return;

	p->flags.during_query = 1;

	if (strchr(params, '/'))
	{
		char baseip[16];
		const char *next;
		unsigned int bits;

		next = delimcpy(baseip, params, sizeof(baseip), '/');
		if (!next) return;
		bits = atoi(next);

		db->Query(dbcb_nameipmac, p, 1,
				"select name, inet_ntoa(ip), macid, to_days(now()) - to_days(lastseen) as daysago "
				"from " TABLE_NAME " "
				"where (ip & ((~0) << (32-#))) = (inet_aton(?) & ((~0) << (32-#))) "
				"order by daysago asc "
				"limit 50 ",
				bits, baseip, bits);
	}
	else if (strchr(params, '%'))
	{
		/* this is going to be a really really slow query... */
		db->Query(dbcb_nameipmac, p, 1,
				"select name, inet_ntoa(ip), macid, to_days(now()) - to_days(lastseen) as daysago "
				"from " TABLE_NAME " "
				"where inet_ntoa(ip) like ? "
				"order by daysago asc "
				"limit 50",
				params);
	}
	else /* try exact ip match */
	{
		db->Query(dbcb_nameipmac, p, 1,
				"select name, inet_ntoa(ip), macid, to_days(now()) - to_days(lastseen) as daysago "
				"from " TABLE_NAME " "
				"where ip = inet_aton(?) "
				"order by daysago asc "
				"limit 50 ",
				params);
	}
}


local helptext_t rawquery_help =
"Targets: none\n"
"Args: <sql code>\n"
"Performs a custom sql query on the alias data. The text you type\n"
"after ?rawquery will be used as the WHERE clause in the query.\n"
"Examples:  ?rawquery name like '%blah%'\n"
"           ?rawquery macid = 34127563 order by lastseen desc\n";

local void Crawquery(const char *tc, const char *params, Player *p, const Target *target)
{
	char qbuf[512];

	if (target->type != T_ARENA)
		return;

	p->flags.during_query = 1;

	snprintf(qbuf, sizeof(qbuf),
			"select name, inet_ntoa(ip), macid, to_days(now()) - to_days(lastseen) as daysago "
			"from " TABLE_NAME " "
			"where %s "
			"limit 50 ",
			params);
	db->Query(dbcb_nameipmac, p, 1, qbuf);
}



local void dbcb_last(int status, db_res *res, void *clos)
{
	Player *p = (Player*)clos;
	int results;
	db_row *row;

	if (!p->flags.during_query)
	{
		if (lm)
			lm->LogP(L_WARN, "aliasdb", p, "recieved query result he didn't ask for");
		return;
	}

	p->flags.during_query = 0;

	if (status != 0 || res == NULL)
	{
		chat->SendMessage(p, "Unexpected database error.");
		return;
	}

	results = db->GetRowCount(res);

	if (results == 0)
	{
		chat->SendMessage(p, "No one has logged in recently.");
		return;
	}

	while ((row = db->GetRow(res)))
	{
		const char *name = db->GetField(row, 0), *secss = db->GetField(row, 1);
		int days, hours, mins, secs = atoi(secss);

		mins = secs / 60;
		secs %= 60;
		hours = mins / 60;
		mins %= 60;
		days = hours / 24;
		hours %= 24;

		if (days == 0)
		{
			if (hours == 0)
			{
				if (mins == 0)
					chat->SendMessage(p, "%-20.20s  %d second%s ago", name, secs, suffix(secs));
				else
					chat->SendMessage(p, "%-20.20s  %d minute%s ago", name, mins, suffix(mins));
			}
			else
				chat->SendMessage(p, "%-20.20s  %d hour%s ago", name, hours, suffix(hours));
		}
		else
			chat->SendMessage(p, "%-20.20s  %d day%s ago", name, days, suffix(days));
	}
}



local helptext_t last_help =
"Targets: none\n"
"Args: none\n"
"Tells you the last 10 people to log in.\n";

local void Clast(const char *tc, const char *params, Player *p, const Target *target)
{
	if (target->type != T_ARENA)
		return;

	p->flags.during_query = 1;

	/* MYSQLISM: unix_timestamp */
	db->Query(dbcb_last, p, 1,
			"select name, unix_timestamp(now()) - unix_timestamp(lastseen) as secsago "
			"from " TABLE_NAME " order by secsago asc limit 10");
}

EXPORT const char info_aliasdb[] = CORE_MOD_INFO("aliasdb");

EXPORT int MM_aliasdb(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		db = mm->GetInterface(I_RELDB, ALLARENAS);

		if (!pd || !cfg || !cmd || !chat || !db)
			return MM_FAIL;

		/* make sure table exists */
		init_db();
		cmd->AddCommand("alias", Calias, ALLARENAS, alias_help);
		cmd->AddCommand("qip", Cqip, ALLARENAS, qip_help);
		cmd->AddCommand("rawquery", Crawquery, ALLARENAS, rawquery_help);
		cmd->AddCommand("last", Clast, ALLARENAS, last_help);

		mm->RegCallback(CB_PLAYERACTION, playera, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, playera, ALLARENAS);
		cmd->RemoveCommand("alias",Calias, ALLARENAS);
		cmd->RemoveCommand("qip", Cqip, ALLARENAS);
		cmd->RemoveCommand("rawquery", Crawquery, ALLARENAS);
		cmd->RemoveCommand("last", Clast, ALLARENAS);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(db);
		return MM_OK;
	}
	return MM_FAIL;
}

