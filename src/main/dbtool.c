
/* dist: public, vim: set fdm=marker: */

/* includes and defines {{{ */

#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>

#ifndef WIN32
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#else
#include <direct.h>
#endif

#include "db.h"

#include "defs.h"

#ifdef DB_VERSION_MAJOR
#if DB_VERSION_MAJOR < 4
#error "This version of bdb is too old."
#elif DB_VERSION_MAJOR > 5
#warning "Your version of bdb is too new. Things might not work right."
#endif
#else
#error "This version of bdb is too old."
#endif

#include "db_layout.h"
#include "statcodes.h"

/* stolen from various .c files */
#define KEY_STATS 1 /* player */
#define KEY_ENDING_TIME 2 /* arena */
#define KEY_TURF_OWNERS 19 /* arena */
#define KEY_JACKPOT 12 /* arena */
#define KEY_CHAT 47 /* player */

#define LATEST_SERIAL ((unsigned)(-1))

/* }}} */


local DB_ENV *dbenv;
local DB *db;

local struct
{
	enum interval_t iv;
	unsigned int serial;
	int serialoffset;
	int (*subcmd)(int argc, char *argv[]);
	int needwrite;
	int numheader;
	const char *dbfile;
} args;


/* db init and shutdown {{{ */

local int init_db(int write)
{
	int err;
	if ((err = db_env_create(&dbenv, 0)))
	{
		fprintf(stderr, "db_env_create: %s\n", db_strerror(err));
		return -1;
	}
	if ((err = dbenv->open(
				dbenv,
				ASSS_DB_HOME,
				DB_INIT_CDB | DB_INIT_MPOOL,
				0644)))
	{
		fprintf(stderr, "dbenv->open: %s\n", db_strerror(err));
		goto close_env;
	}
	if ((err = db_create(&db, dbenv, 0)))
	{
		fprintf(stderr, "db_create: %s\n", db_strerror(err));
		goto close_env;
	}
	if ((err = db->open(
				db,
#if DB_VERSION_MAJOR >= 4 && DB_VERSION_MINOR >= 1
				/* they added a transaction parameter to the db->open
				 * call in 4.1.0. */
				NULL,
#endif
				ASSS_DB_FILENAME,
				NULL,
				DB_BTREE,
				(args.needwrite ? 0 : DB_RDONLY),
				0644)))
	{
		fprintf(stderr, "db->open: %s\n", db_strerror(err));
		goto close_db;
	}

	return 0;

close_db:
	db->close(db, 0);
close_env:
	dbenv->close(dbenv, 0);
	return -1;
}


local int init_named_db(const char *name)
{
	int err;
	if ((err = db_create(&db, NULL, 0)))
	{
		fprintf(stderr, "db_create: %s\n", db_strerror(err));
		return -1;
	}
	if ((err = db->open(
				db,
#if DB_VERSION_MAJOR >= 4 && DB_VERSION_MINOR >= 1
				/* they added a transaction parameter to the db->open
				 * call in 4.1.0. */
				NULL,
#endif
				name,
				NULL,
				DB_BTREE,
				DB_RDONLY,
				0644)))
	{
		fprintf(stderr, "db->open: %s\n", db_strerror(err));
		goto close_db;
	}

	return 0;

close_db:
	db->close(db, 0);
	return -1;
}


local void close_db(void)
{
	if (db)
		db->close(db, 0);
	if (dbenv)
		dbenv->close(dbenv, 0);
}

/* }}} */


/* utility stuff {{{ */

local int is_ascii7_str(const unsigned char *c)
{
	for (;; c++)
		if (*c == '\0')
			return TRUE;
		else if (*c < ' ')
			return FALSE;
		else if (*c > '~')
			return FALSE;
}

/* there's no standard for quoting things in a .csv file, so I'm
 * creating my own. */
local void print_quoted(const unsigned char *c)
{
	if (is_ascii7_str(c) && strchr((const char *)c, ',') == NULL)
		fputs((const char *)c, stdout);
	else
	{
		putchar('"');
		for (; *c; c++)
			if (*c == '"')
				putchar('\\'), putchar('"');
			else if (*c < ' ' || *c > '~')
				printf("\\x%2x", *c);
			else
				putchar(*c);
		putchar('"');
	}
}


local int get_latest_serial(unsigned int *dest, const char *ag, int interval)
{
	DBT key, val;
	struct current_serial_record_key curr;
	int err;

	memset(&curr, 0, sizeof(curr));
	strncpy(curr.arenagrp, ag, sizeof(curr.arenagrp));
	curr.interval = interval;

	/* prepare for query */
	memset(&key, 0, sizeof(key));
	key.data = &curr;
	key.size = sizeof(curr);
	memset(&val, 0, sizeof(val));
	val.data = dest;
	val.ulen = sizeof(*dest);
	val.flags = DB_DBT_USERMEM;

	/* query current serial number for this sg/interval */
	err = db->get(db, NULL, &key, &val, 0);
	if (err == DB_NOTFOUND)
	{
		fprintf(stderr, "serial number not found for arenagrp %s, interval %s\n",
				ag, get_interval_name(interval));
		return FALSE;
	}
	else if (err)
	{
		fprintf(stderr, "error getting serial number for arenagrp %s, interval %s: %s\n",
				ag, get_interval_name(interval), db_strerror(err));
		return FALSE;
	}
	else
		return TRUE;
}


local int get_ending_time(time_t *result, const char *ag, int interval, unsigned int serial)
{
	DBT key, val;
	struct arena_record_key keydata;
	int err;

	memset(&keydata, 0, sizeof(keydata));
	strncpy(keydata.arena, ag, sizeof(keydata.arena));
	keydata.interval = interval;
	keydata.serialno = serial;
	keydata.key = KEY_ENDING_TIME;

	memset(&key, 0, sizeof(key));
	key.data = &keydata;
	key.size = sizeof(keydata);
	memset(&val, 0, sizeof(val));
	val.data = result;
	val.ulen = sizeof(*result);
	val.flags = DB_DBT_USERMEM;

	err = db->get(db, NULL, &key, &val, 0);
	if (err == DB_NOTFOUND)
		return FALSE;
	else if (err)
	{
		fprintf(stderr, "error getting ending time arenagrp %s, interval %s, serial %d: %s\n",
				ag, get_interval_name(interval), serial, db_strerror(err));
		return FALSE;
	}
	else
		return TRUE;
}


local void walk_db(int (*func)(DBT *key, DBT *val), int write)
{
	DBC *cursor;
	DBT key, val;

	if (db->cursor(db, NULL, &cursor, write ? DB_WRITECURSOR : 0))
	{
		if (write)
			fputs("couldn't get db cursor for writing. is the db read-only?\n", stderr);
		else
			fputs("couldn't get db cursor.\n", stderr);
		return;
	}

	memset(&key, 0, sizeof(key));
	memset(&val, 0, sizeof(val));
	while (cursor->c_get(cursor, &key, &val, DB_NEXT) == 0)
		if (func(&key, &val))
			cursor->c_del(cursor, 0);

	cursor->c_close(cursor);
}

/* }}} */


/* debug stuff, debug subcommand {{{ */

local void print_player_data(int key, DBT *val)
{
	if (key == KEY_STATS)
	{
		struct
		{
			unsigned short stat;
			int val;
		} *v = val->data;
		int c = val->size / sizeof(*v);
	
		printf("\tstats data:\n");
		for (; c--; v++)
			printf("\t\t%s: %d\n", get_stat_name(v->stat), v->val);
	}
	else if (key == KEY_CHAT)
	{
		struct
		{
			unsigned short mask;
			time_t expires;
			int msgs;
			ticks_t lastcheck;
		} *v = val->data;

		printf("\tchat data: mask=%hu expires=%ld msgs=%d lastcheck=%d\n",
				v->mask, (long)v->expires, v->msgs, v->lastcheck);
	}
	else
	{
		printf("\tunknown key type %d\n", key);
	}
}

local void print_arena_data(int key, DBT *val)
{
	if (key == KEY_ENDING_TIME)
	{
		printf("\tending time: %s", ctime(val->data));
	}
	else if (key == KEY_TURF_OWNERS)
	{
		printf("\tturf flag owner data, %d flags\n", val->size / 2);
	}
	else if (key == KEY_JACKPOT)
	{
		printf("\tjackpot: %d\n", *(int*)val->data);
	}
	else
	{
		printf("\tunknown key type %d\n", key);
	}
}

local int print_entry(DBT *key, DBT *val)
{
	if (key->size == sizeof(struct player_record_key))
	{
		struct player_record_key *k = key->data;
		printf("player record key:\n\tname: %s\n\tarenagrp: %s\n\tinterval: %s\n"
				"\tserialno: %u\n",
			k->name, k->arenagrp, get_interval_name(k->interval), k->serialno);
		print_player_data(k->key, val);
	}
	else if (key->size == sizeof(struct arena_record_key))
	{
		struct arena_record_key *k = key->data;
		printf("arena record key:\n\tarenagrp: %s\n\tinterval: %s\n\tserialno: %u\n",
			k->arena, get_interval_name(k->interval), k->serialno);
		print_arena_data(k->key, val);
	}
	else if (key->size == sizeof(struct current_serial_record_key))
	{
		struct current_serial_record_key *k = key->data;
		printf("serial record key:\n\tarenagrp: %s\n\tinterval: %s\n\tvalue: %u\n",
			k->arenagrp, get_interval_name(k->interval), *(int*)val->data);
	}
	else
	{
		printf("unknown key type: length %d bytes\n", key->size);
	}

	return FALSE;
}


local int cmd_debug(int argc, char *argv[])
{
	walk_db(print_entry, FALSE);
	return 0;
}


/* }}} */


/* stats subcommand {{{ */

#define MAX_STAT 1000

struct
{
	const char *arena;
	byte statmap[MAX_STAT+1];
} stats_args;

local int stats_match(DBT *key, DBT *val)
{
	struct player_record_key *k = key->data;
	if (key->size != sizeof(struct player_record_key))
		return FALSE;
	if (k->key != KEY_STATS)
		return FALSE;
	if (args.iv != k->interval)
		return FALSE;
	if (args.serial != k->serialno)
		return FALSE;
	if (strncmp(k->arenagrp, stats_args.arena, sizeof(k->arenagrp)))
		return FALSE;
	return TRUE;
}

local int stats_get_statmap(DBT *key, DBT *val)
{
	struct
	{
		unsigned short stat;
		int val;
	} *v = val->data;
	int c = val->size / sizeof(*v);

	if (stats_match(key, val))
		for (; c--; v++)
			stats_args.statmap[v->stat] = 1;

	return FALSE;
}

local int stats_print_stats(DBT *key, DBT *val)
{
	struct player_record_key *k = key->data;
	struct
	{
		unsigned short stat;
		int val;
	} *v = val->data;
	int c = val->size / sizeof(*v), cur;

	if (!stats_match(key, val))
		return FALSE;

	print_quoted((unsigned char *)k->name);
	cur = 0;
	for (; c--; v++)
	{
		int pos = stats_args.statmap[v->stat];
		/* if we didn't see it before, just skip it */
		if (pos == 0)
			continue;
		/* fill in commas until the right spot */
		for (; cur < pos; cur++)
			putchar(',');
		printf("%d", v->val);
	}
	while (cur < stats_args.statmap[MAX_STAT])
		putchar(','), cur++;
	putchar('\n');

	return FALSE;
}

local int cmd_stats(int argc, char *argv[])
{
	int i, c;
	time_t tm = time(NULL), ended;

	stats_args.arena = argv[1];
	if (!stats_args.arena)
	{
		puts("you must specify an arena name");
		return 1;
	}

	if (args.serial == LATEST_SERIAL)
		if (!get_latest_serial(&args.serial, stats_args.arena, args.iv))
			return 2;

	args.serial += args.serialoffset;

	/* print some meta-information */
	printf("### asss db tool: stats\n");
	printf("### generated at %s", ctime(&tm));
	printf("### arena: %s\n", stats_args.arena);
	printf("### interval: %s\n", get_interval_name(args.iv));
	printf("### serial: %d\n", args.serial);
	if (get_ending_time(&ended, stats_args.arena, args.iv, args.serial))
		printf("### last modified time: %s\n", ctime(&ended));
	else
		printf("### last modified time unknown\n");

	/* note: it's possible to see stats during the print_stats that we
	 * didn't see during the get_statmap because of concurrent access! */
	/* also note we're taking advantage of the fact that stats are
	 * always stored in sorted order to speed thing up a bit. */

	walk_db(stats_get_statmap, FALSE);

	/* figure out column positions */
	for (i = c = 0; i < MAX_STAT; i++)
		if (stats_args.statmap[i])
			stats_args.statmap[i] = c++;

	/* print header line */
	stats_args.statmap[MAX_STAT] = c;
	printf("player name");
	for (i = 0; i < MAX_STAT; i++)
		if (stats_args.statmap[i])
		{
			if (args.numheader)
				printf(",%d", i);
			else
				printf(",%s", get_stat_name(i));
		}
	putchar('\n');

	/* does the real work */
	walk_db(stats_print_stats, FALSE);

	return 0;
}

/* }}} */


/* arenas subcommand {{{ */

typedef struct arenas_info
{
	unsigned int precs, arecs, srecs;
	unsigned int pbytes, abytes, sbytes;
} arenas_info;

local HashTable *arenas_hash;

local int arenas_get_names(DBT *key, DBT *val)
{
	arenas_info *i;
	if (key->size == sizeof(struct player_record_key))
	{
		struct player_record_key *k = key->data;
		if (!(i = HashGetOne(arenas_hash, k->arenagrp)))
			HashAdd(arenas_hash, k->arenagrp, i = amalloc(sizeof(*i)));
		i->precs++;
		i->pbytes += key->size + val->size;
	}
	else if (key->size == sizeof(struct arena_record_key))
	{
		struct arena_record_key *k = key->data;
		if (!(i = HashGetOne(arenas_hash, k->arena)))
			HashAdd(arenas_hash, k->arena, i = amalloc(sizeof(*i)));
		i->arecs++;
		i->abytes += key->size + val->size;
	}
	else if (key->size == sizeof(struct current_serial_record_key))
	{
		struct current_serial_record_key *k = key->data;
		if (!(i = HashGetOne(arenas_hash, k->arenagrp)))
			HashAdd(arenas_hash, k->arenagrp, i = amalloc(sizeof(*i)));
		i->srecs++;
		i->sbytes += key->size + val->size;
	}
	else
		printf("unknown key type: length %d bytes\n", key->size);

	return FALSE;
}

local int arenas_print_names(const char *key, void *val, void *data)
{
	arenas_info *i = val;
	printf("%-16.16s  %6d /%9d  %6d /%9d  %6d /%9d\n",
			key,
			i->precs, i->pbytes,
			i->arecs, i->abytes,
			i->precs + i->arecs + i->srecs, i->pbytes + i->abytes + i->sbytes);
	afree(val);
	return FALSE;
}

local int cmd_arenas(int argc, char *argv[])
{
	arenas_hash = HashAlloc();

	walk_db(arenas_get_names, FALSE);

	printf("    this database contains information about these arenas:\n");
	printf("  %-14.14s  %-17s  %-17s  %-17s\n",
		"arena name",
		"player recs/bytes",
		" arena recs/bytes",
		" total recs/bytes");

	HashEnum(arenas_hash, arenas_print_names, NULL);
	HashFree(arenas_hash);

	return 0;
}

/* }}} */


/* players subcommand {{{ */

typedef struct players_info
{
	unsigned recs, bytes;
} players_info;

local HashTable *players_hash;

local int players_get_names(DBT *key, DBT *val)
{
	players_info *i;
	if (key->size == sizeof(struct player_record_key))
	{
		struct player_record_key *k = key->data;
		if (!(i = HashGetOne(players_hash, k->name)))
			HashAdd(players_hash, k->name, i = amalloc(sizeof(*i)));
		i->recs++;
		i->bytes += key->size + val->size;
	}

	return FALSE;
}

local int players_print_names(const char *key, void *val, void *data)
{
	players_info *i = val;
	printf("%-20.20s  %6d /%9d\n",
			key, i->recs, i->bytes);
	afree(val);
	return FALSE;
}

local int cmd_players(int argc, char *argv[])
{
	players_hash = HashAlloc();

	walk_db(players_get_names, FALSE);

	printf("    this database contains information about these players:\n");
	printf("  %-18.18s  %-17s\n",
		"player name",
		"records / bytes");

	HashEnum(players_hash, players_print_names, NULL);
	HashFree(players_hash);

	return 0;
}

/* }}} */


/* serials subcommand {{{ */

local const char *serials_ag;
local HashTable *serials_hash;

local int serials_get_names(DBT *key, DBT *val)
{
	struct player_record_key *k = key->data;
	char n[16];

	if (key->size != sizeof(struct player_record_key))
		return FALSE;
	if (strncmp(k->arenagrp, serials_ag, sizeof(k->arenagrp)))
		return FALSE;
	if (k->interval != args.iv)
		return FALSE;

	snprintf(n, sizeof(n), "%u", k->serialno);
	HashReplace(serials_hash, n, NULL);

	return FALSE;
}

local int serials_print_nums(const char *key, void *val, void *data)
{
	puts(key);
	return FALSE;
}

local int cmd_serials(int argc, char *argv[])
{
	serials_ag = argv[1];

	if (!serials_ag)
	{
		puts("you must specify an arena name");
		return 1;
	}

	serials_hash = HashAlloc();

	walk_db(serials_get_names, FALSE);

	printf("  for arena %s, interval %s, these serials are defined:\n",
			serials_ag, get_interval_name(args.iv));

	HashEnum(serials_hash, serials_print_nums, NULL);
	HashFree(serials_hash);

	return 0;
}

/* }}} */


/* erase subcommand {{{ */

local struct
{
	const char *ag;
	unsigned player_rm, arena_rm, serial_rm;
} erase_args;

local int erase_walk(DBT *key, DBT *val)
{
	if (key->size == sizeof(struct player_record_key))
	{
		struct player_record_key *k = key->data;
		if (!strncmp(k->arenagrp, erase_args.ag, sizeof(k->arenagrp)))
			return ++erase_args.player_rm;
	}
	else if (key->size == sizeof(struct arena_record_key))
	{
		struct arena_record_key *k = key->data;
		if (!strncmp(k->arena, erase_args.ag, sizeof(k->arena)))
			return ++erase_args.arena_rm;
	}
	else if (key->size == sizeof(struct current_serial_record_key))
	{
		struct current_serial_record_key *k = key->data;
		if (!strncmp(k->arenagrp, erase_args.ag, sizeof(k->arenagrp)))
			return ++erase_args.serial_rm;
	}
	else
		printf("unknown key type: length %d bytes\n", key->size);

	return FALSE;
}

local int cmd_erase(int argc, char *argv[])
{
	erase_args.ag = argv[1];

	if (!erase_args.ag || !erase_args.ag[0])
	{
		puts("you must specify an arena or arena group name");
		return 1;
	}

	walk_db(erase_walk, TRUE);

	printf("removed %d records (",
			erase_args.player_rm + erase_args.arena_rm + erase_args.serial_rm);
	printf("%d player, %d arena, %d serial).\n",
			erase_args.player_rm , erase_args.arena_rm , erase_args.serial_rm);

	return 0;
}

/* }}} */


/* perase subcommand {{{ */

local struct
{
	const char *player;
	unsigned rm;
} perase_args;

local int perase_walk(DBT *key, DBT *val)
{
	if (key->size == sizeof(struct player_record_key))
	{
		struct player_record_key *k = key->data;
		if (!strncmp(k->name, perase_args.player, sizeof(k->name)))
			return ++perase_args.rm;
	}

	return FALSE;
}

local int cmd_perase(int argc, char *argv[])
{
	perase_args.player = argv[1];

	if (!perase_args.player || !perase_args.player[0])
	{
		puts("you must specify a player name.");
		return 1;
	}

	walk_db(perase_walk, TRUE);

	printf("removed %d records.\n", perase_args.rm);

	return 0;
}

/* }}} */


/* erasehist subcommand {{{ */

local struct
{
	unsigned player_rm, arena_rm;
} erasehist_args;

local int erasehist_walk(DBT *key, DBT *val)
{
	unsigned int latest;
	if (key->size == sizeof(struct player_record_key))
	{
		struct player_record_key *k = key->data;
		if (get_latest_serial(&latest, k->arenagrp, k->interval))
			if (k->serialno != latest)
				return ++erasehist_args.player_rm;
	}
	else if (key->size == sizeof(struct arena_record_key))
	{
		struct arena_record_key *k = key->data;
		if (get_latest_serial(&latest, k->arena, k->interval))
			if (k->serialno != latest)
				return ++erasehist_args.arena_rm;
	}

	return FALSE;
}

local int cmd_erasehist(int argc, char *argv[])
{
	walk_db(erasehist_walk, TRUE);

	printf("removed %d records (",
			erasehist_args.player_rm + erasehist_args.arena_rm);
	printf("%d player, %d arena).\n",
			erasehist_args.player_rm , erasehist_args.arena_rm);

	return 0;
}

/* }}} */


/* usage info, command line processing {{{ */

local void do_usage(void)
{
	fputs(
"asss db tool\n"
"\n"
"usage: dbtool <general options> <subcommand>\n"
"\n"
"general options:\n"
"\t-d <file>  use a specific database file\n"
"\n"
"\t-g   use per-game interval\n"
"\t-r   use per-reset interval (default)\n"
"\t-m   use per-map-rotation interval\n"
"\t-f   use forever interval\n"
"\n"
"\t-c   use current stats (default)\n"
"\t-l   use last-completed period of stats\n"
"\t-s <number>  use the given period of stats\n"
"\n"
"\t-n   print field names numerically instead of descriptively\n"
"\n"
"subcommands:\n"
"\tarenas\n"
"\t\tlists the known arenas\n"
"\tplayers\n"
"\t\tlists the known players\n"
"\tserials <arena name>\n"
"\t\tprint out the available stat periods for an arena\n"
"\tstats <arena name>\n"
"\t\tprint out stats for an arena\n"
"\terase <arena name>\n"
"\t\terase arena records (all intervals and serials)\n"
"\tperase <player name>\n"
"\t\terase player records (all intervals and serials)\n"
"\terasehist\n"
"\t\terase historical records (all intervals and arenas)\n"
"\tdebug\n"
"\t\tdump data for debugging purposes\n"
"\n"
"notes:\n"
"use arena name '<public>' for public arena shared stats.\n"
"files specified with -d will be opened read-only.\n"
"\n"
	, stdout);
	exit(0);
}


local int proc_args(int argc, char *argv[])
{
	int i, o;

	args.iv = INTERVAL_RESET;
	args.serial = LATEST_SERIAL;
	args.serialoffset = 0;

	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] != '-')
			break;
		o = argv[i][1];
		if (o == 'g')
			args.iv = INTERVAL_GAME;
		else if (o == 'r')
			args.iv = INTERVAL_RESET;
		else if (o == 'm')
			args.iv = INTERVAL_MAPROTATION;
		else if (o == 'f')
			args.iv = INTERVAL_FOREVER;
		else if (o == 'c')
			args.serial = LATEST_SERIAL;
		else if (o == 'l')
			args.serial = LATEST_SERIAL, args.serialoffset = -1;
		else if (o == 's')
			args.serial = atoi(argv[++i]);
		else if (o == 'n')
			args.numheader = 1;
		else if (o == 'd')
			args.dbfile = argv[++i];
		else
		{
			printf("unknown option: %s\n\n", argv[i]);
			do_usage();
		}
	}

	if (i != argc)
	{
		const char *subcmd = argv[i];
		     if (!strcasecmp(subcmd, "arenas"))
			args.subcmd = cmd_arenas;
		else if (!strcasecmp(subcmd, "players"))
			args.subcmd = cmd_players;
		else if (!strcasecmp(subcmd, "serials"))
			args.subcmd = cmd_serials;
		else if (!strcasecmp(subcmd, "stats"))
			args.subcmd = cmd_stats;
		else if (!strcasecmp(subcmd, "erase"))
			args.subcmd = cmd_erase, args.needwrite = 1;
		else if (!strcasecmp(subcmd, "perase"))
			args.subcmd = cmd_perase, args.needwrite = 1;
		else if (!strcasecmp(subcmd, "erasehist"))
			args.subcmd = cmd_erasehist, args.needwrite = 1;
		else if (!strcasecmp(subcmd, "debug"))
			args.subcmd = cmd_debug;
	}

	return i-1;
}

/* }}} */


int main(int argc, char *argv[])
{
	int i, r;

	i = proc_args(argc, argv);

	if (!args.subcmd)
		do_usage();

	if (args.dbfile)
	{
		if (args.needwrite)
		{
			fprintf(stderr, "you can't write to a database specified with -d.\n");
			return 1;
		}
		else
			r = init_named_db(args.dbfile);
	}
	else
		r = init_db(args.needwrite);

	if (r)
	{
		fprintf(stderr, "can't open database.\n");
		return 1;
	}

	i++; /* eat argv[0] */
	i = args.subcmd(argc - i, argv + i);

	close_db();

	return i;
}

