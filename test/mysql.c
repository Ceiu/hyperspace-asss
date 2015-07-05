
/* 2>/dev/null
gcc -L/opt/mysql/lib -I/opt/mysql/include -o test mysql.c -lmysqlclient -lz
exit # */

/*

/opt/mysql/bin/mysql -u asss -p <<EOF
use asss;
create table t1 (k varchar(200) primary key, v varchar(200));
EOF

*/

#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <mysql.h>

static MYSQL *db;


int do_query(MYSQL *m, const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
	int r;

	va_start(ap, fmt);
	r = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (r > sizeof(buf))
		return -1; /* query too big */

	return mysql_real_query(m, buf, r);
}



void do_add(int c, char *v[])
{
	int res;

	if (c < 2) return;

	res = do_query(db, "insert into t1 values ('%s', '%s')",
			v[0], v[1]);

	if (res == 0)
		printf("Added successfully,\n");
	else
		printf("Error.\n");
}

void do_get(int c, char *v[])
{
	int r;

	if (c < 1) return;

	r= do_query(db, "select v from t1 where k = '%s'",
			v[0]);

	if (r)
		printf("Error.\n");
	else
	{
		MYSQL_ROW row;
		MYSQL_RES *res = mysql_store_result(db);

		while ((row = mysql_fetch_row(res)))
			printf("Result: %s\n", row[0]);

		mysql_free_result(res);
	}
}

void do_list()
{
	int r;

	r = do_query(db, "select k, v from t1");

	if (r)
		printf("Error: %s.\n", mysql_error(db));
	else
	{
		MYSQL_ROW row;
		MYSQL_RES *res = mysql_store_result(db);

		while ((row = mysql_fetch_row(res)))
			printf("Key: '%s'  Val: '%s'\n", row[0], row[1]);

		mysql_free_result(res);
	}

}


int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("usage: %s {add|get|list} [key] [data]\n", argv[0]);
		exit(1);
	}

	db = mysql_init(NULL);

	if (!mysql_real_connect(db, "localhost", "asss", "asss", "asss", 0, NULL, 0))
	{
		printf("Error in connecting: %s\n", mysql_error(db));
		return 1;
	}

	if (!strcasecmp(argv[1], "add"))
		do_add(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "get"))
		do_get(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "list"))
		do_list();

	mysql_close(db);
}

