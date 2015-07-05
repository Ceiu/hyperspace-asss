
/* dist: public */

#ifndef __RELDB_H
#define __RELDB_H


typedef struct db_res db_res;
/* pytype: opaque, db_res *, db_res */
typedef struct db_row db_row;
/* pytype: opaque, db_row *, db_row */

typedef void (*query_callback)(int status, db_res *res, void *clos);


#define I_RELDB "reldb-3"

typedef struct Ireldb
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/* 0 if not connected, 1 if connected */
	int (*GetStatus)();
	/* pyint: void -> int */

	/* fmt may contain '?'s, which will be replaced by the corresponding
	 * argument as a properly escaped and quoted string, and '#'s, which
	 * will be replaced by the corresponding argument as an unsigned
	 * int. python users have to escape stuff themselves, using
	 * EscapeString. */
	int (*Query)(query_callback cb, void *clos, int notifyfail, const char *fmt, ...);
	/* pyint: (int, db_res, clos -> void) dynamic failval 0, clos, int, string -> int */

	int (*GetRowCount)(db_res *res);
	/* pyint: db_res -> int */
	int (*GetFieldCount)(db_res *res);
	/* pyint: db_res -> int */
	db_row * (*GetRow)(db_res *res);
	/* pyint: db_res -> db_row */
	const char * (*GetField)(db_row *row, int fieldnum);
	/* pyint: db_row, int -> string */

	/* returns the value generated for an AUTO_INCREMENT column for the
	 * previous INSERT or UPDATE. you must call this immediately after
	 * the query that you want the id from. if this returns -1, it means
	 * this database implementation doesn't support this functionality. */
	int (*GetLastInsertId)(void);
	/* pyint: void -> int */

	/* only useful to python. C users should use formatting characters
	 * in Query. */
	int (*EscapeString)(const char *str, char *buf, int buflen);
	/* pyint: string, string out, int buflen -> int */
} Ireldb;



#endif

