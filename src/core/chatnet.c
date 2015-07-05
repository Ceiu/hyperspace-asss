
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

#include "asss.h"

#include "protutil.h"


/* locking notes: we only acquire the big chatnet lock in many places
 * when checking player type information because although new players
 * may be created and destroyed without that mutex, chatnet players may
 * not be, and those are the only ones we're interested in here. */


/* global data */

local Imodman *mm;
local Iplayerdata *pd;
local Ilogman *lm;
local Imainloop *ml;
local Iconfig *cfg;

local HashTable *handlers;
local int mysock;
local int cfg_msgdelay;
local int cdkey;
local pthread_mutex_t bigmtx;
#define LOCK() pthread_mutex_lock(&bigmtx)
#define UNLOCK() pthread_mutex_unlock(&bigmtx)


local int get_socket(void)
{
	const char *spec;
	int port;
	struct in_addr bindaddr;

	/* cfghelp: Net:ChatListen, global, string, mod: chatnet
	 * Where to listen for chat protocol connections. Either 'port' or
	 * 'ip:port'. Net:Listen will be used if this is missing. */
	spec = cfg->GetStr(GLOBAL, "Net", "ChatListen");
	if (spec == NULL)
	{
		Inet *net = mm->GetInterface(I_NET, ALLARENAS);
		if (!net) return -1;
		net->GetListenData(0, &port, NULL, 0);
		mm->ReleaseInterface(net);
		bindaddr.s_addr = INADDR_ANY;
	}
	else
	{
		char field1[32];
		const char *n = delimcpy(field1, spec, sizeof(field1), ':');
		if (!n)
		{
			/* just port */
			port = strtol(field1, NULL, 0);
			bindaddr.s_addr = INADDR_ANY;
		}
		else
		{
			/* got ip:port */
			port = strtol(n, NULL, 0);
			inet_aton(field1, &bindaddr);
		}
	}

	return init_listening_socket(port, bindaddr.s_addr);
}


/* call with big lock */
local Player * try_accept(int s)
{
	Player *p;
	sp_conn *cli;
	char ipbuf[INET_ADDRSTRLEN];

	int a;
	socklen_t sinsize;
	struct sockaddr_in sin;

	sinsize = sizeof(sin);
	a = accept(s, (struct sockaddr*)&sin, &sinsize);

	if (a == -1)
	{
		if (lm) lm->Log(L_WARN, "<chatnet> accept() failed");
		return NULL;
	}

	if (set_nonblock(a) == -1)
	{
		if (lm) lm->Log(L_WARN, "<chatnet> set_nonblock() failed");
		closesocket(a);
		return NULL;
	}

	p = pd->NewPlayer(T_CHAT);

	cli = PPDATA(p, cdkey);
	cli->socket = a;
	cli->sin = sin;
	cli->lastproctime = TICK_MAKE(current_ticks() - 1000U);
	cli->lastsendtime = TICK_MAKE(current_ticks() + 1000U);
	cli->lastrecvtime = TICK_MAKE(current_ticks() + 1000U);
	cli->inbuf = NULL;
	LLInit(&cli->outbufs);

	inet_ntop(AF_INET, &sin.sin_addr, ipbuf, INET_ADDRSTRLEN);
	astrncpy(p->ipaddr, ipbuf, sizeof(p->ipaddr));
	astrncpy(p->clientname, "<unknown chat client>", sizeof(p->clientname));

	pd->WriteLock();
	p->status = S_CONNECTED;
	pd->WriteUnlock();

	lm->Log(L_DRIVEL, "<chatnet> [pid=%d] new connection from %s:%i",
			p->pid, ipbuf, ntohs(sin.sin_port));

	return p;
}


local void process_line(const char *cmd, const char *rest, void *v)
{
	Player *p = v;
	Link *l;
	LinkedList lst = LL_INITIALIZER;

	if (!rest) rest = "";

	HashGetAppend(handlers, cmd, &lst);

	for (l = LLGetHead(&lst); l; l = l->next)
		((MessageFunc)(l->data))(p, rest);

	LLEmpty(&lst);
}


/* call with lock held */
local void clear_bufs(sp_conn *cli)
{
	clear_sp_conn(cli);
}


local int do_one_iter(void *dummy)
{
	Player *p;
	sp_conn *cli;
	Link *link;
	int max, ret;
	ticks_t gtc = current_ticks();
	fd_set readset, writeset;
	struct timeval tv = { 0, 0 };
	LinkedList toremove = LL_INITIALIZER;
	LinkedList tokill = LL_INITIALIZER;
	LinkedList toproc = LL_INITIALIZER;

	FD_ZERO(&readset);
	FD_ZERO(&writeset);

	/* always listen for accepts on listening socket */
	FD_SET(mysock, &readset);
	max = mysock;

	pd->WriteLock();
	LOCK();

	FOR_EACH_PLAYER_P(p, cli, cdkey)
		if (IS_CHAT(p) &&
		    p->status >= S_CONNECTED &&
		    cli->socket > 2)
		{
			if (p->status < S_TIMEWAIT)
			{
				/* always check for incoming data */
				FD_SET(cli->socket, &readset);
				/* maybe for writing too */
				if (! LLIsEmpty(&cli->outbufs))
					FD_SET(cli->socket, &writeset);
				/* update max */
				if (cli->socket > max)
					max = cli->socket;
			}
			else
			{
				/* handle disconnects */
				lm->LogP(L_INFO, "chatnet", p, "disconnected");
				clear_bufs(cli);
				closesocket(cli->socket);
				cli->socket = -1;
				/* we can't remove players while we're iterating through
				 * the list, so add them and do them later. */
				LLAdd(&toremove, p);
			}
		}

	/* remove players that disconnected above */
	for (link = LLGetHead(&toremove); link; link = link->next)
		pd->FreePlayer(link->data);
	LLEmpty(&toremove);

	ret = select(max + 1, &readset, &writeset, NULL, &tv);

	/* new connections? */
	if (FD_ISSET(mysock, &readset))
		try_accept(mysock);

	FOR_EACH_PLAYER_P(p, cli, cdkey)
		if (IS_CHAT(p) &&
		    p->status < S_TIMEWAIT &&
		    cli->socket > 2)
		{
			/* data to read? */
			if (FD_ISSET(cli->socket, &readset))
			{
				if (do_sp_read(cli) == sp_read_died)
					/* we can't call KickPlayer in here because we have the
					 * player status mutex for reading instead of writing. so add to
					 * list and do it later. */
					LLAdd(&tokill, p);
			}
			/* or write? */
			if (FD_ISSET(cli->socket, &writeset))
				do_sp_write(cli);
			/* or process? */
			if (cli->inbuf &&
			    TICK_DIFF(gtc, cli->lastproctime) > cfg_msgdelay)
				LLAdd(&toproc, p);
			/* send noop if we haven't sent anything to this client, and
			 * they haven't sent anything to us, for 3 minutes. */
			if (TICK_DIFF(gtc, cli->lastsendtime) > 18000 &&
			    TICK_DIFF(gtc, cli->lastrecvtime) > 18000)
				sp_send(cli, "NOOP");
		}
	
	/* kill players where we read eof above */
	for (link = LLGetHead(&tokill); link; link = link->next)
		pd->KickPlayer(link->data);
	LLEmpty(&tokill);

	/* process clients who had info above */
	for (link = LLGetHead(&toproc); link; link = link->next)
	{
		p = link->data;
		do_sp_process(PPDATA(p, cdkey), process_line, p);
	}
	LLEmpty(&toproc);

	UNLOCK();
	pd->WriteUnlock();

	return TRUE;
}


local void AddHandler(const char *type, MessageFunc f)
{
	LOCK();
	HashAdd(handlers, type, f);
	UNLOCK();
}

local void RemoveHandler(const char *type, MessageFunc f)
{
	LOCK();
	HashRemove(handlers, type, f);
	UNLOCK();
}



local void real_send(LinkedList *lst, const char *line, va_list ap)
{
	Link *l;
	char buf[MAXMSGSIZE+1];

	vsnprintf(buf, MAXMSGSIZE - 2, line, ap);

	LOCK();
	for (l = LLGetHead(lst); l; l = l->next)
	{
		Player *p = l->data;
		sp_conn *cli = PPDATA(p, cdkey);
		if (IS_CHAT(p))
			sp_send(cli, buf);
	}
	UNLOCK();
}


local void SendToSet(LinkedList *set, const char *line, ...)
{
	va_list args;
	va_start(args, line);
	real_send(set, line, args);
	va_end(args);
}


local void SendToOne(Player *p, const char *line, ...)
{
	va_list args;
	Link l = { NULL, p };
	LinkedList lst = { &l, &l };

	va_start(args, line);
	real_send(&lst, line, args);
	va_end(args);
}


local void SendToArena(Arena *arena, Player *except, const char *line, ...)
{
	va_list args;
	LinkedList lst = LL_INITIALIZER;
	Link *link;
	Player *p;

	if (!arena) return;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING && p->arena == arena && p != except && IS_CHAT(p))
			LLAdd(&lst, p);
	pd->Unlock();

	va_start(args, line);
	real_send(&lst, line, args);
	va_end(args);

	LLEmpty(&lst);
}


local void GetClientStats(Player *p, struct chat_client_stats *stats)
{
	char ipbuf[INET_ADDRSTRLEN];
	sp_conn *cli = PPDATA(p, cdkey);
	if (!stats || !p) return;
	inet_ntop(AF_INET, &(cli->sin.sin_addr), ipbuf, INET_ADDRSTRLEN);
	astrncpy(stats->ipaddr, ipbuf, sizeof(stats->ipaddr));
	stats->port = cli->sin.sin_port;
}


local void do_final_shutdown(void)
{
	Link *link;
	Player *p;
	sp_conn *cli;

	pd->Lock();
	LOCK();
	FOR_EACH_PLAYER_P(p, cli, cdkey)
		if (IS_CHAT(p))
		{
			/* try to clean up as much memory as possible */
			clear_bufs(cli);
			/* close all the connections also */
			if (cli->socket > 2)
				closesocket(cli->socket);
		}
	UNLOCK();
	pd->Unlock();
}


local Ichatnet _int =
{
	INTERFACE_HEAD_INIT(I_CHATNET, "net-chat")
	AddHandler, RemoveHandler,
	SendToOne, SendToArena, SendToSet,
	GetClientStats
};

EXPORT const char info_chatnet[] = CORE_MOD_INFO("chatnet");

EXPORT int MM_chatnet(int action, Imodman *mm_, Arena *a)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		if (!pd || !cfg || !lm || !ml) return MM_FAIL;

		cdkey = pd->AllocatePlayerData(sizeof(sp_conn));
		if (cdkey == -1) return MM_FAIL;

		/* get the sockets */
		mysock = get_socket();
		if (mysock == -1) return MM_FAIL;

		/* cfghelp: Net:ChatMessageDelay, global, int, def: 20 \
		 * mod: chatnet
		 * The delay between sending messages to clients using the
		 * text-based chat protocol. (To limit bandwidth used by
		 * non-playing cilents.) */
		cfg_msgdelay = cfg->GetInt(GLOBAL, "Net", "ChatMessageDelay", 20);

		/* init mutex */
		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&bigmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		handlers = HashAlloc();

		/* install timer */
		ml->SetTimer(do_one_iter, 10, 10, NULL, NULL);

		/* install ourself */
		mm->RegInterface(&_int, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		/* uninstall ourself */
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(do_one_iter, NULL);

		/* clean up */
		do_final_shutdown();
		HashFree(handlers);
		pthread_mutex_destroy(&bigmtx);
		closesocket(mysock);
		pd->FreePlayerData(cdkey);

		/* release these */
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);

		return MM_OK;
	}
	return MM_FAIL;
}

