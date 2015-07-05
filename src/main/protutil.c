
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sched.h>
#endif


#include "defs.h"
#include "util.h"
#include "protutil.h"


typedef struct sp_buffer
{
	char *cur; /* points to within data */
	char data[1];
} sp_buffer;


#define LEFT(buf) (MAXMSGSIZE - ((buf)->cur - (buf)->data) - 1)
#define CR 0x0D
#define LF 0x0A

static sp_buffer *get_out_buffer(const char *str)
{
	sp_buffer *b = amalloc(sizeof(sp_buffer) + strlen(str) + 1);
	strcpy(b->data, str);
	strcat(b->data, "\n");
	b->cur = b->data;
	return b;
}


static sp_buffer *get_in_buffer(void)
{
	sp_buffer *b = amalloc(sizeof(sp_buffer) + MAXMSGSIZE);
	b->cur = b->data;
	return b;
}


int set_nonblock(int s)
{
#ifndef WIN32
	int opts = fcntl(s, F_GETFL);
	if (opts == -1)
		return -1;
	else if (fcntl(s, F_SETFL, opts | O_NONBLOCK) == -1)
		return -1;
	else
		return 0;
#else
	unsigned long nb = 1;
	if (ioctlsocket(s, FIONBIO, &nb) == SOCKET_ERROR)
		return -1;
	else
		return 0;
#endif
}


int init_listening_socket(int port, unsigned long int bindaddr)
{
	int s;
	struct sockaddr_in sin;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		return -1;
#endif

	sin.sin_family = AF_INET;
	memset(sin.sin_zero, 0, sizeof(sin.sin_zero));
	sin.sin_addr.s_addr = bindaddr;
	sin.sin_port = htons(port);

	s = socket(PF_INET, SOCK_STREAM, 0);

	if (s == -1)
	{
		perror("socket");
		return -1;
	}

#ifdef CFG_SET_REUSEADDR
	{
		int yes = 1;
		setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const void*)&yes, sizeof(yes));
	}
#endif

	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) == -1)
	{
		perror("bind");
		closesocket(s);
		return -1;
	}

	if (set_nonblock(s) == -1)
	{
		perror("set_nonblock");
		closesocket(s);
		return -1;
	}

	if (listen(s, 5) == -1)
	{
		perror("listen");
		closesocket(s);
		return -1;
	}

	return s;
}


int init_client_socket(void)
{
	int s;

#ifdef WIN32
	WSADATA wsad;
	if (WSAStartup(MAKEWORD(1,1),&wsad))
		return -1;
#endif

	s = socket(PF_INET, SOCK_STREAM, 0);

	if (s == -1)
	{
		perror("socket");
		return -1;
	}

	if (set_nonblock(s) == -1)
	{
		perror("set_nonblock");
		closesocket(s);
		return -1;
	}

	return s;
}


void clear_sp_conn(sp_conn *conn)
{
	afree(conn->inbuf);
	conn->inbuf = 0;
	LLEnum(&conn->outbufs, afree);
	LLEmpty(&conn->outbufs);
}


enum sp_read_result do_sp_read(sp_conn *conn)
{
	int n;
	sp_buffer *buf = conn->inbuf;

	if (!buf)
		buf = conn->inbuf = get_in_buffer();

	/* check if we have any room left */
	if (LEFT(buf) <= 0)
		return sp_read_nodata;

	n = recv(conn->socket, buf->cur, LEFT(buf), 0);

	if (n == 0)
	{
		/* client disconnected */
		afree(buf);
		conn->inbuf = NULL;
		return sp_read_died;
	}
	else if (n > 0)
	{
		buf->cur += n;
		conn->lastrecvtime = current_ticks();
		return sp_read_ok;
	}
	else
		return sp_read_nodata;
}


int do_sp_process(sp_conn *conn, void (*process)(const char *line, const char *rest, void *v), void *v)
{
	sp_buffer *buf = conn->inbuf;
	char *src = buf->data;

	/* if there is a complete message */
	if (memchr(src, CR, buf->cur - src) || memchr(src, LF, buf->cur - src))
	{
		char line[MAXMSGSIZE+1], *dst = line;
		sp_buffer *buf2 = NULL;

		/* copy it into line, minus terminators */
		while (*src != CR && *src != LF) *dst++ = *src++;
		/* close it off */
		*dst = 0;

		/* process it */
		{
			char *rest = strchr(line, ':');
			if (rest)
			{
				*rest = '\0';
				process(line, rest+1, v);
			}
			else
				process(line, NULL, v);
		}

		/* skip terminators in input */
		while (*src == CR || *src == LF) src++;
		/* if there's unprocessed data left... */
		if ((buf->cur - src) > 0)
		{
			/* put the unprocessed data in a new buffer */
			buf2 = get_in_buffer();
			memcpy(buf2->data, src, buf->cur - src);
			buf2->cur += buf->cur - src;
		}

		/* free the old one */
		afree(buf);
		/* put the new one in place */
		conn->inbuf = buf2;
		/* reset message time */
		conn->lastproctime = current_ticks();
	}
	else if (LEFT(buf) <= 0)
	{
		/* if we've found no newline and the buffer's full, this line is
		 * too long. discard. */
		buf->cur = buf->data;
	}

	return 0;
}


int do_sp_write(sp_conn *conn)
{
	Link *l = LLGetHead(&conn->outbufs);

	if (l && l->data)
	{
		sp_buffer *buf = l->data;
		int n, len;

		len = strlen(buf->data) - (buf->cur - buf->data);
		n = send(conn->socket, buf->cur, len, 0);

		if (n > 0)
			buf->cur += n;

		/* check if this buffer is done */
		if (buf->cur[0] == 0)
		{
			afree(buf);
			LLRemoveFirst(&conn->outbufs);
		}

		conn->lastsendtime = current_ticks();
	}

	return 0;
}


void sp_send(sp_conn *conn, const char *line)
{
	sp_buffer *buf = get_out_buffer(line);
	LLAdd(&conn->outbufs, buf);
}


