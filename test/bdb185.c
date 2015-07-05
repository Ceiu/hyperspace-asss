
/* 2>/dev/null
gcc -o test bdb185.c -ldb1
exit # */

#include <stdio.h>
#include <fcntl.h>
#include <db1/db.h>

#define DBNAME "test.db"

DB *db;


void do_add(int c, char *v[])
{
	DBT key, val;
	int res;

	if (c < 2) return;

	key.data = v[0];
	key.size = strlen(v[0]) + 1;

	val.data = v[1];
	val.size = strlen(v[1]) + 1;

	res = db->put(db, &key, &val, 0);

	if (res == 0)
		printf("Added successfully,\n");
	else
		printf("Error.\n");
}

void do_get(int c, char *v[])
{
	DBT key, val;
	int res;

	if (c < 1) return;

	key.data = v[0];
	key.size = strlen(v[0]) + 1;

	res = db->get(db, &key, &val, 0);

	if (res == 1)
		printf("Not found.\n");
	else if (res == 0)
		printf("Value: %s\n", val.data);
	else
		printf("Error.\n");
}

void do_list()
{
	DBT key, val;
	int res;

	while ((res = db->seq(db, &key, &val, R_NEXT)) == 0)
		printf("Key: %s  Value: %s\n", key.data, val.data);
	if (res == 1)
		printf("End.\n");
	else
		printf("Error.\n");
}


int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("usage: %s {add|get|list} [key] [data]\n", argv[0]);
		exit(1);
	}

	db = dbopen(DBNAME, O_CREAT | O_RDWR, 0666, DB_HASH, NULL);

	if (!strcasecmp(argv[1], "add"))
		do_add(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "get"))
		do_get(argc - 2, argv + 2);
	else if (!strcasecmp(argv[1], "list"))
		do_list();

	db->close(db);
}

