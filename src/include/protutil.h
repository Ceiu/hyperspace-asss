
/* dist: public */

#include "util.h"

#define MAXMSGSIZE 2035

struct sp_buffer;

typedef struct sp_conn
{
	int socket;
	struct sockaddr_in sin;
	/* these three are in ticks */
	ticks_t lastproctime, lastsendtime, lastrecvtime;
	struct sp_buffer *inbuf;
	LinkedList outbufs;
} sp_conn;


int set_nonblock(int s);
int init_listening_socket(int port, unsigned long int bindaddr);
int init_client_socket(void);

void clear_sp_conn(sp_conn *conn);

enum sp_read_result
{
	sp_read_nodata,
	sp_read_ok,
	sp_read_died
};

enum sp_read_result do_sp_read(sp_conn *conn);

int do_sp_process(sp_conn *conn,
		void (*process)(const char *cmd, const char *rest, void *v),
		void *v);

int do_sp_write(sp_conn *conn);

void sp_send(sp_conn *conn, const char *line);

