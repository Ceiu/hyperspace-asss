
/* dist: public */

#ifndef HSCORE_MYSQL_H
#define HSCORE_MYSQL_H


typedef struct db_res db_res;
typedef struct db_row db_row;

typedef void (*query_callback)(int status, db_res *res, void *clos);


#define I_HSCORE_MYSQL "hs-mysql-1"

typedef struct Ihscoremysql
{
	INTERFACE_HEAD_DECL

	/* 0 if not connected, 1 if connected */
	int (*GetStatus)();

	/* fmt may contain '?'s, which will be replaced by the corresponding
	 * argument as a properly escaped and quoted string, and '#'s, which
	 * will be replaced by the corresponding argument as an unsigned
	 * int. */
	int (*Query)(query_callback cb, void *clos, int notifyfail, const char *fmt, ...);

	int (*GetRowCount)(db_res *res);
	db_row * (*GetRow)(db_res *res);
	const char * (*GetField)(db_row *row, int fieldnum);
	int (*GetLastInsertId)(void);
} Ihscoremysql;



#endif
