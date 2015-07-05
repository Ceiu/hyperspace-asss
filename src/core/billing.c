
/* dist: public */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <errno.h>

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
#include "banners.h"
#include "persist.h"

#define PINGINTERVAL 180

typedef struct pdata
{
	long userdbid;
	unsigned long usage;
	char firstused[32];
	void (*Done)(Player *p, AuthData *data);
	byte knowntobiller, sentpae, wantreg, demographics_set;
} pdata;


/* this holds the current status of our connection */
local enum
{
	s_no_socket,
	s_connecting,
	s_connected,
	s_waitlogin,
	s_loggedin,
	s_retry,
	s_loginfailed,
	s_disabled
} state;

local time_t lastretry; /* doubles as lastping */
local int pdkey;
local sp_conn conn;
local pthread_mutex_t mtx;

local int cfg_retryseconds;

local Imodman *mm;
local Iplayerdata *pd;
local Ilogman *lm;
local Imainloop *ml;
local Ichat *chat;
local Icmdman *cmd;
local Iconfig *cfg;
local Inet *net;
local Iauth *oldauth;


/* utility thingies */

local void memtohex(char *dest, byte *mem, int bytes)
{
	static const char tab[16] = "0123456789abcdef";
	int i;
	for (i = 0; i < bytes; i++)
	{
		*dest++ = tab[(mem[i]>>4) & 0x0f];
		*dest++ = tab[(mem[i]>>0) & 0x0f];
	}
	*dest = 0;
}

local int hextomem(byte *dest, const char *text, int bytes)
{
	int i;
	for (i = 0; i < bytes; i++)
	{
		byte d = 0;
		const char c1 = *text++;
		const char c2 = *text++;

		if (c1 >= '0' && c1 <= '9')
			d = c1 - '0';
		else if (c1 >= 'a' && c1 <= 'f')
			d = c1 - 'a' + 10;
		else if (c1 >= 'A' && c1 <= 'F')
			d = c1 - 'A' + 10;
		else return FALSE;

		d <<= 4;

		if (c2 >= '0' && c2 <= '9')
			d |= c2 - '0';
		else if (c2 >= 'a' && c2 <= 'f')
			d |= c2 - 'a' + 10;
		else if (c2 >= 'A' && c2 <= 'F')
			d |= c2 - 'A' + 10;
		else return FALSE;

		*dest++ = d;
	}
	return TRUE;
}


local void drop_connection(int newstate)
{
	Player *p;
	Link *link;
	pdata *data;

	/* only announce if changing from loggedin */
	if (state == s_loggedin)
		chat->SendArenaMessage(ALLARENAS, "Notice: Connection to user database server lost");

	/* clear knowntobiller values */
	pd->Lock();
	FOR_EACH_PLAYER_P(p, data, pdkey)
		data->knowntobiller = 0;
	pd->Unlock();

	/* then close socket */
	if (conn.socket > 0)
		closesocket(conn.socket);
	conn.socket = -1;

	state = newstate;
	lastretry = time(NULL);
}

/* the auth interface */

local void authenticate(Player *p, struct LoginPacket *lp, int lplen,
			void (*Done)(Player *p, AuthData *data))
{
	char buf[MAXMSGSIZE];
	pdata *data = PPDATA(p, pdkey);

	pthread_mutex_lock(&mtx);

	if (state == s_loggedin)
	{
		data->Done = Done;

		snprintf(buf, MAXMSGSIZE - 128, "PLOGIN:%d:%d:%s:%s:%s:%d:",
				p->pid, lp->flags, lp->name, lp->password, p->ipaddr, lp->macid);
		if (lplen == LEN_LOGINPACKET_CONT)
			memtohex(buf + strlen(buf), lp->contid, 64);

		sp_send(&conn, buf);

		data->knowntobiller = TRUE;
		data->sentpae = FALSE;
		data->wantreg = FALSE;
		data->demographics_set = FALSE;
	}
	else
	{
		/* biller isn't connected, fall back to next highest priority */
		lm->Log(L_DRIVEL,
				"<billing> user db server not connected; falling back to '%s'",
				oldauth->head.name);
		oldauth->Authenticate(p, lp, lplen, Done);
		data->knowntobiller = FALSE;
	}

	pthread_mutex_unlock(&mtx);
}

local struct Iauth myauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "billing-auth")
	authenticate
};


/* catch players logging out */

local void paction(Player *p, int action, Arena *arena)
{
	pdata *data = PPDATA(p, pdkey);

	pthread_mutex_lock(&mtx);
	if (!data->knowntobiller)
		;
	else if (action == PA_DISCONNECT)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "PLEAVE:%d", p->pid);
		sp_send(&conn, buf);
		data->knowntobiller = FALSE;
	}
	else if (action == PA_ENTERARENA && !data->sentpae)
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "PENTERARENA:%d", p->pid);
		sp_send(&conn, buf);
		data->sentpae = TRUE;
	}
	pthread_mutex_unlock(&mtx);
}


/* handle chat messages that have to go to the biller */

local void onchatmsg(Player *p, int type, int sound, Player *target, int freq, const char *text)
{
	pdata *data = PPDATA(p, pdkey);

	pthread_mutex_lock(&mtx);
	if (!data->knowntobiller)
		;
	else if (type == MSG_CHAT)
	{
		char buf[MAXMSGSIZE], chan[16];
		const char *t;

		/* note that this supports a channel name in place of the usual
		 * channel number. e.g., ;foo;this is a message to the foo channel. */
		t = strchr(text, ';');
		if (t && (t-text) < 10)
			text = delimcpy(chan, text, sizeof(chan), ';');
		else
			strcpy(chan, "1");

		snprintf(buf, sizeof(buf), "CHAT:%d:%s:%d:%s", p->pid, chan, sound, text);
		sp_send(&conn, buf);
	}
	else if (type == MSG_REMOTEPRIV && target == NULL)
	{
		/* only grab these if the server didn't handle them internally */
		const char *t;
		char dest[32];

		t = delimcpy(dest, text+1, sizeof(dest), ':');
		if (text[0] != ':' || !t)
			lm->LogP(L_MALICIOUS, "billing", p, "malformed remote private message");
		else if (dest[0] == '#')
		{
			char buf[MAXMSGSIZE];
			snprintf(buf, sizeof(buf), "RMTSQD:%d:%s:%d:%s", p->pid, dest+1, sound, t);
			sp_send(&conn, buf);
		}
		else
		{
			char buf[MAXMSGSIZE];
			snprintf(buf, sizeof(buf), "RMT:%d:%s:%d:%s", p->pid, dest, sound, t);
			sp_send(&conn, buf);
		}
	}
	pthread_mutex_unlock(&mtx);
}

local void setbanner(Player *p, Banner *banner, int from_player)
{
	if (from_player)
	{
		pdata *data = PPDATA(p, pdkey);

		pthread_mutex_lock(&mtx);
		if (data->knowntobiller)
		{
			char buf[24 + sizeof(banner->data) * 2], *t;
			snprintf(buf, 24, "BNR:%d:", p->pid);
			t = buf + strlen(buf);
			memtohex(t, banner->data, sizeof(banner->data));
			t[sizeof(banner->data) * 2] = '\0';
			sp_send(&conn, buf);
		}
		pthread_mutex_unlock(&mtx);
	}
}

local void pdemographics(Player *p, byte *pkt, int len)
{
	pdata *data = PPDATA(p, pdkey);

	pthread_mutex_lock(&mtx);
	if (data->demographics_set)
		lm->LogP(L_MALICIOUS, "billing", p, "duplicate demographics packet");
	else if (data->knowntobiller && len < 800)
	{
		char buf[2048], *t;
		snprintf(buf, 32, "REGDATA:%d:", p->pid);
		t = buf + strlen(buf);
		memtohex(t, pkt, len);
		t[len*2] = '\0';
		sp_send(&conn, buf);
		data->demographics_set = TRUE;
	}
	pthread_mutex_unlock(&mtx);
}


/* and command that go to the biller */

local void Cdefault(const char *tc, const char *line, Player *p, const Target *target)
{
	pdata *data = PPDATA(p, pdkey);
	char buf[MAXMSGSIZE];

	pthread_mutex_lock(&mtx);
	if (!data->knowntobiller)
		;
	else if (target->type != T_ARENA)
		lm->LogP(L_DRIVEL, "billing", p, "unknown command with bad target: %s", line);
	else if (IS_RESTRICTED(chat->GetPlayerChatMask(p), MSG_BCOMMAND))
		;
	else
	{
		const char *params = line + strlen(tc);
		while (*params && (*params == ' ' || *params == '='))
			params++;
		snprintf(buf, sizeof(buf), "CMD:%d:%s:%s", p->pid, tc, params);
		sp_send(&conn, buf);
	}
	pthread_mutex_unlock(&mtx);
}


/* other useful commands */

local helptext_t usage_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the usage information (current hours and minutes logged in, and\n"
"total hours and minutes logged in), as well as the first login time, of\n"
"the target player, or you if no target.\n";

local void Cusage(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	pdata *tdata = PPDATA(t, pdkey);
	unsigned int mins, secs;

	pthread_mutex_lock(&mtx);
	if (tdata->knowntobiller)
	{
		secs = TICK_DIFF(current_ticks(), t->connecttime) / 100;
		mins = secs / 60;

		if (t != p) chat->SendMessage(p, "usage: %s:", t->name);
		chat->SendMessage(p, "session: %5d:%02d:%02d",
				mins / 60, mins % 60, secs % 60);
		secs += tdata->usage;
		mins = secs / 60;
		chat->SendMessage(p, "  total: %5d:%02d:%02d",
				mins / 60, mins % 60, secs % 60);
		chat->SendMessage(p, "first played: %s", tdata->firstused);
	}
	else
		chat->SendMessage(p, "usage unknown for %s", t->name);
	pthread_mutex_unlock(&mtx);
}


local helptext_t userdbid_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the user database server id of the target player,\n"
"or yours if no target.\n";

local void Cuserdbid(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	pdata *tdata = PPDATA(t, pdkey);
	pthread_mutex_lock(&mtx);
	if (tdata->knowntobiller)
		chat->SendMessage(p, "%s has user database id %ld",
				t->name, tdata->userdbid);
	else
		chat->SendMessage(p, "user database id unknown for %s", t->name);
	pthread_mutex_unlock(&mtx);
}


local helptext_t userdbadm_help =
"Targets: none\n"
"Args: status|drop|connect\n"
"The subcommand 'status' reports the status of the user database server\n"
"connection. 'drop' disconnects the connection if it's up, and 'connect'\n"
"reconnects after dropping or failed login.\n";

local void Cuserdbadm(const char *tc, const char *params, Player *p, const Target *target)
{
	pthread_mutex_lock(&mtx);
	if (!strcmp(params, "drop"))
	{
		/* if we're up, drop the socket */
		if (conn.socket > 0)
			drop_connection(s_disabled);
		else
			state = s_disabled;
		state = s_disabled;
		chat->SendMessage(p, "user db connection disabled");
	}
	else if (!strcmp(params, "connect"))
	{
		if (state == s_loginfailed || state == s_disabled)
		{
			state = s_no_socket;
			chat->SendMessage(p, "user db server connection reactivated");
		}
		else
			chat->SendMessage(p, "user db server connection already active");
	}
	else
	{
		const char *t = NULL;
		switch (state)
		{
			case s_no_socket:
				t = "not connected yet";  break;
			case s_connecting:
				t = "connecting";  break;
			case s_connected:
				t = "connected";  break;
			case s_waitlogin:
				t = "waiting for login response";  break;
			case s_loggedin:
				t = "logged in";  break;
			case s_retry:
				t = "waiting to retry";  break;
			case s_loginfailed:
				t = "disabled (login failed)";  break;
			case s_disabled:
				t = "disabled (by user)";  break;
		}
		chat->SendMessage(p, "user db status: %s", t);
	}
	pthread_mutex_unlock(&mtx);
}


/* handlers for all messages from the biller */

local void process_connectok(const char *line)
{
	char swname[128];
	const char *billername;

	billername = delimcpy(swname, line, sizeof(swname), ':');
	if (billername)
	{
		lm->Log(L_INFO, "<billing> logged into user db server (%s; %s)",
				billername, swname);
	}
	else
	{
		lm->Log(L_INFO, "<billing> logged into user db server with "
				"malformed response: %s", line);
	}

	state = s_loggedin;
}

local void process_connectbad(const char *line)
{
	char swname[128];
	char billername[128];
	const char *reason;

	reason = delimcpy(swname, line, sizeof(swname), ':');
	if (reason)
	{
		reason = delimcpy(billername, reason, sizeof(billername), ':');
		lm->Log(L_INFO, "<billing> user db server (%s; %s) rejected login: %s",
				billername, swname, reason);
	}
	else
	{
		lm->Log(L_INFO, "<billing> user db server rejected login with "
				"malformed response: %s", line);
	}

	/* now close it and don't try again */
	drop_connection(s_loginfailed);
}

local void process_wantreg(const char *line)
{
	/* b->g: "WANTREG:pid" */
	int pid = atoi(line);
	Player *p = pd->PidToPlayer(pid);
	pdata *data = PPDATA(p, pdkey);

	if (p) data->wantreg = TRUE;
}

local void process_pok(const char *line)
{
	/* b->g: "POK:pid:rtext:name:squad:billingid:usage:firstused" */
	AuthData ad;
	char pidstr[16], rtext[256], bidstr[16], usagestr[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;
	t = delimcpy(rtext, t, sizeof(rtext), ':');
	if (!t) return;
	t = delimcpy(ad.name, t, 21, ':');
	if (!t) return;
	strncpy(ad.sendname, ad.name, 20);
	t = delimcpy(ad.squad, t, sizeof(ad.squad), ':');
	if (!t) return;
	t = delimcpy(bidstr, t, sizeof(bidstr), ':');
	if (!t) return;
	t = delimcpy(usagestr, t, sizeof(usagestr), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		pdata *data = PPDATA(p, pdkey);
		astrncpy(data->firstused, t, sizeof(data->firstused));
		data->usage = atol(usagestr);
		data->userdbid = atol(bidstr);
		ad.demodata = data->wantreg;
		ad.code = AUTH_OK;
		ad.authenticated = TRUE;
		data->Done(p, &ad);
	}
	else
		lm->Log(L_WARN, "<billing> biller sent player auth response for unknown pid %s", pidstr);
}

local void process_pbad(const char *line)
{
	/* b->g: "PBAD:pid:newname:rtext" */
	AuthData ad;
	char pidstr[16], newname[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;
	t = delimcpy(newname, t, sizeof(newname), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		pdata *data = PPDATA(p, pdkey);
		memset(&ad, 0, sizeof(ad));
		/* ew.. i really wish i didn't have to do this */
		if (!strncmp(t, "CODE", 4))
			ad.code = atoi(t+4);
		else if (atoi(newname) == 1)
			ad.code = AUTH_NEWNAME;
		else
		{
			ad.code = AUTH_CUSTOMTEXT;
			astrncpy(ad.customtext, t, sizeof(ad.customtext));
		}
		data->Done(p, &ad);
	}
	else
		lm->Log(L_WARN, "<billing> biller sent player auth response for unknown pid %s", pidstr);
}

local void process_pkick(const char *line)
{
	/* b->g: "PKICK:pid:reason" */
	char pidstr[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		lm->LogP(L_INFO, "billing", p, "kicked off by biller: %s", t);
		chat->SendMessage(p, "You were disconnected by the user database server: %s", t);
		pd->KickPlayer(p);
	}
	else
		lm->Log(L_WARN, "<billing> biller sent player kick request for "
			"unknown pid %s, reason: %s", pidstr, t);
}

local void process_bnr(const char *line)
{
	/* b->g: "BNR:pid:banner" */
	Banner banner;
	char pidstr[16];
	const char *t = line;
	Player *p;
	Ibanners *bnr = mm->GetInterface(I_BANNERS, ALLARENAS);

	if (!bnr) return;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;
	
	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		if (hextomem(banner.data, t, sizeof(banner.data)))
			bnr->SetBanner(p, &banner);
		else
			lm->Log(L_WARN, "<billing> biller sent bad banner string "
					"for pid %s", pidstr);
	}
	else
		lm->Log(L_WARN, "<billing> biller sent player banner for "
			"unknown pid %s", pidstr);

	mm->ReleaseInterface(bnr);
}

static struct
{
	char channel[32];
	char sender[32];
	int sound;
	char text[256];
} chatdata;

local void process_chattxt(const char *line)
{
	/* b->g: "CHATTXT:channel:sender:sound:text" */
	char soundstr[16];
	const char *t = line;

	t = delimcpy(chatdata.channel, t, sizeof(chatdata.channel), ':');
	if (!t) return;
	t = delimcpy(chatdata.sender, t, sizeof(chatdata.sender), ':');
	if (!t) return;
	t = delimcpy(soundstr, t, sizeof(soundstr), ':');
	if (!t) return;
	chatdata.sound = atoi(soundstr);
	astrncpy(chatdata.text, t, sizeof(chatdata.text));
}

local void process_chat(const char *line)
{
	/* b->g: "CHAT:pid:number" */
	char pidstr[16];
	const char *t = line;
	Player *p;

	t = delimcpy(pidstr, t, sizeof(pidstr), ':');
	if (!t) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		Link link = { NULL, p };
		LinkedList list = { &link, &link };

		chat->SendAnyMessage(&list, MSG_CHAT, chatdata.sound, NULL,
				"%s:%s> %s", t, chatdata.sender, chatdata.text);
	}
}

local void process_rmt(const char *line)
{
	/* b->g: "RMT:pid:sender:sound:text" */
	char pidstr[16], sender[32], soundstr[16];
	const char *text = line;
	Player *p;

	text = delimcpy(pidstr, text, sizeof(pidstr), ':');
	if (!text) return;
	text = delimcpy(sender, text, sizeof(sender), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
	{
		Link link = { NULL, p };
		LinkedList list = { &link, &link };

		chat->SendRemotePrivMessage(&list, atoi(soundstr), NULL, sender, text);
	}
}

local void process_rmtsqd(const char *line)
{
	/* b->g: "RMTSQD:destsquad:sender:sound:text" */
	char destsq[32], sender[32], soundstr[16];
	const char *t = line;

	LinkedList list = LL_INITIALIZER;
	Link *link;
	Player *p;

	t = delimcpy(destsq, t, sizeof(destsq), ':');
	if (!t || !destsq[0]) return;
	t = delimcpy(sender, t, sizeof(sender), ':');
	if (!t) return;
	t = delimcpy(soundstr, t, sizeof(soundstr), ':');
	if (!t) return;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (strcasecmp(destsq, p->squad) == 0)
			LLAdd(&list, p);
	pd->Unlock();

	chat->SendRemotePrivMessage(&list, atoi(soundstr), destsq, sender, t);
}

local void process_msg(const char *line)
{
	/* b->g: "MSG:pid:sound:text" */
	char pidstr[16], soundstr[16];
	const char *text = line;
	Player *p;

	text = delimcpy(pidstr, text, sizeof(pidstr), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	p = pd->PidToPlayer(atoi(pidstr));
	if (p)
		chat->SendSoundMessage(p, atoi(soundstr), "%s", text);
}

local void process_staffmsg(const char *line)
{
	/* b->g: "STAFFMSG:sender:sound:text" */
	char sender[32], soundstr[16];
	const char *text = line;

	text = delimcpy(sender, text, sizeof(sender), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	chat->SendModMessage("Network-wide broadcast: %s", text);
}

local void process_broadcast(const char *line)
{
	/* b->g: "BROADCAST:sender:sound:text" */
	char sender[32], soundstr[16];
	const char *text = line;

	text = delimcpy(sender, text, sizeof(sender), ':');
	if (!text) return;
	text = delimcpy(soundstr, text, sizeof(soundstr), ':');
	if (!text) return;

	chat->SendArenaSoundMessage(ALLARENAS, atoi(soundstr),
			"Network-wide broadcast: %s", text);
}

local void process_scorereset(const char *line)
{
	/* b->g: "SCORERESET" */
	Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);

	lm->Log(L_INFO, "<billing> billing server requested score reset");
	if (persist)
		persist->EndInterval(AG_PUBLIC, NULL, INTERVAL_RESET);
	mm->ReleaseInterface(persist);
}


/* the dispatcher */

local HashTable *dispatch;

local void init_dispatch(void)
{
	dispatch = HashAlloc();
	HashAdd(dispatch, "CONNECTOK",       process_connectok);
	HashAdd(dispatch, "CONNECTBAD",      process_connectbad);
	HashAdd(dispatch, "WANTREG",         process_wantreg);
	HashAdd(dispatch, "POK",             process_pok);
	HashAdd(dispatch, "PBAD",            process_pbad);
	HashAdd(dispatch, "PKICK",           process_pkick);
	HashAdd(dispatch, "BNR",             process_bnr);
	HashAdd(dispatch, "CHATTXT",         process_chattxt);
	HashAdd(dispatch, "CHAT",            process_chat);
	HashAdd(dispatch, "RMT",             process_rmt);
	HashAdd(dispatch, "RMTSQD",          process_rmtsqd);
	HashAdd(dispatch, "MSG",             process_msg);
	HashAdd(dispatch, "STAFFMSG",        process_staffmsg);
	HashAdd(dispatch, "BROADCAST",       process_broadcast);
	HashAdd(dispatch, "SCORERESET",      process_scorereset);
}

local void deinit_dispatch(void)
{
	HashFree(dispatch);
	dispatch = NULL;
}

local void process_line(const char *cmd, const char *rest, void *dummy)
{
	void (*func)(const char *) = HashGetOne(dispatch, cmd);
	if (func) func(rest);
}


/* stuff to handle connecting */

#ifndef WIN32
local void setup_proxy(const char *proxy, const char *ipaddr, int port)
{
	/* this means we're using an external proxy to connect to the
	 * biller. set up some sockets and fork it off */
	int sockets[2], r;
	pid_t chld;

	r = socketpair(PF_UNIX, SOCK_STREAM, 0, sockets);
	if (r < 0)
	{
		lm->Log(L_ERROR, "<billing> socketpair failed: %s", strerror(errno));
		state = s_disabled;
		return;
	}

	/* no need to bother with pthread_atfork handlers because we're just
	 * going to exec the proxy immediately */
	chld = fork();

	if (chld < 0)
	{
		lm->Log(L_ERROR, "<billing> fork failed: %s", strerror(errno));
		state = s_disabled;
		return;
	}
	else if (chld == 0)
	{
		/* in child */
		char portstr[16];

		/* set up fds, but leave stderr connected to stderr of the server */
		closesocket(sockets[1]);
		dup2(sockets[0], STDIN_FILENO);
		dup2(sockets[0], STDOUT_FILENO);
		closesocket(sockets[0]);

		snprintf(portstr, sizeof(portstr), "%d", port);
		execlp(proxy, proxy, ipaddr, portstr, NULL);

		/* uh oh */
		fprintf(stderr, "E <billing> can't exec user db proxy (%s): %s\n",
				proxy, strerror(errno));
		_exit(123);
	}
	else
	{
		/* in parent */
		closesocket(sockets[0]);
		set_nonblock(sockets[1]);
		conn.socket = sockets[1];
		/* skip right over s_connecting */
		state = s_connected;
	}
}
#endif


local void remote_connect(const char *ipaddr, int port)
{
	int r;
	struct sockaddr_in sin;

	conn.socket = init_client_socket();

	if (conn.socket == -1)
	{
		drop_connection(s_retry);
		return;
	}

	lm->Log(L_DRIVEL, "<billing> trying to connect to user db server at %s:%d",
			ipaddr, port);

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = inet_addr(ipaddr);
	r = connect(conn.socket, (struct sockaddr*)&sin, sizeof(sin));

	if (r == 0)
	{
		/* successful connect. this is pretty unlikely since the socket
		 * is nonblocking. */
		lm->Log(L_INFO, "<billing> connected to user db server");
		state = s_connected;
	}
#ifndef WIN32
	else if (errno == EINPROGRESS)
#else
	else if (WSAGetLastError() == WSAEWOULDBLOCK)
#endif
	{
		/* this is the most likely result */
		state = s_connecting;
	}
	else
	{
		lm->Log(L_WARN, "<billing> unexpected error from connect: %s",
				strerror(errno));
		/* retry again in a while */
		drop_connection(s_retry);
	}
}


local void get_socket(void)
{
	/* cfghelp: Billing:Proxy, global, string
	 * This setting allows you to specify an external program that will
	 * handle the billing server connection. The program should be
	 * prepared to speak the asss billing protocol over its standard
	 * input and output. It will get two command line arguments, which
	 * are the ip and port of the billing server, as specified in the
	 * Billing:IP and Billing:Port settings. The program name should
	 * either be an absolute pathname or be located on your $PATH. */
	const char *proxy = cfg->GetStr(GLOBAL, "Billing", "Proxy");
	/* cfghelp: Billing:IP, global, string
	 * The ip address of the billing server (no dns hostnames allowed). */
	const char *ipaddr = cfg->GetStr(GLOBAL, "Billing", "IP");
	/* cfghelp: Billing:Port, global, int, def: 1850
	 * The port to connect to on the billing server. */
	int port = cfg->GetInt(GLOBAL, "Billing", "Port", 1850);

	lastretry = time(NULL);

	if (proxy)
#ifndef WIN32
		setup_proxy(proxy, ipaddr, port);
#else
	{
		lm->Log(L_ERROR, "<billing> proxy not supported on windows");
		state = s_disabled;
	}
#endif
	else
		remote_connect(ipaddr, port);
}


local void check_connected(void)
{
	/* we have an connect in progress. check it. */

	fd_set fds;
	struct timeval tv = { 0, 0 };
	int r;

	FD_ZERO(&fds);
	FD_SET(conn.socket, &fds);
	r = select(conn.socket + 1, NULL, &fds, NULL, &tv);

	if (r > 0)
	{
		/* we've got a result */
		int opt;
		socklen_t optlen = sizeof(opt);

		r = getsockopt(conn.socket, SOL_SOCKET, SO_ERROR, &opt, &optlen);

		if (r < 0)
			lm->Log(L_WARN, "<billing> unexpected error from getsockopt: %s",
					strerror(errno));
		else
		{
			if (opt == 0)
			{
				/* successful connection */
				lm->Log(L_DRIVEL, "<billing> connected to billing server");
				state = s_connected;
			}
			else
			{
				lm->Log(L_WARN, "<billing> can't connect to billing server: %s",
						strerror(opt));
				drop_connection(s_retry);
			}
		}
	}
	else if (r < 0)
	{
		/* error from select */
		lm->Log(L_WARN, "<billing> unexpected error from select: %s",
				strerror(errno));
		/* abort and retry in a while */
		drop_connection(s_retry);
	}
}


local void try_login(void)
{
	char buf[MAXMSGSIZE];

	/* cfghelp: Billing:ServerName, global, string
	 * The server name to send to the billing server. */
	const char *zonename = cfg->GetStr(GLOBAL, "Billing", "ServerName");
	/* cfghelp: Billing:ServerNetwork, global, string
	 * The network name to send to the billing server. A network name
	 * should identify a group of servers (e.g., SSCX). */
	const char *net = cfg->GetStr(GLOBAL, "Billing", "ServerNetwork");
	/* cfghelp: Billing:Password, global, string
	 * The password to log in to the billing server with. */
	const char *pwd = cfg->GetStr(GLOBAL, "Billing", "Password");

	if (!zonename) zonename = "";
	if (!net) net = "";
	if (!pwd) pwd = "";

	snprintf(buf, sizeof(buf), "CONNECT:1.3.1:asss "ASSSVERSION":%s:%s:%s",
			zonename, net, pwd);
	sp_send(&conn, buf);

	state = s_waitlogin;
}


/* runs often */

local void try_send_recv(void)
{
	time_t now;
	fd_set rfds, wfds;
	struct timeval tv = { 0, 0 };

	if (conn.socket < 0) return;

	FD_ZERO(&rfds);
	FD_SET(conn.socket, &rfds);
	FD_ZERO(&wfds);
	if (!LLIsEmpty(&conn.outbufs))
		FD_SET(conn.socket, &wfds);

	select(conn.socket+1, &rfds, &wfds, NULL, &tv);

	if (FD_ISSET(conn.socket, &rfds))
		if (do_sp_read(&conn) == sp_read_died)
		{
			/* lost connection */
			lm->Log(L_INFO, "<billing> lost connection to billing server (read eof)");
			drop_connection(s_retry);
			return;
		}

	if (FD_ISSET(conn.socket, &wfds))
		do_sp_write(&conn);

	if (conn.inbuf)
		do_sp_process(&conn, process_line, NULL);

	now = time(NULL);
	if ((now - lastretry) > PINGINTERVAL)
	{
		sp_send(&conn, "PING");
		lastretry = now;
	}
}


local int do_one_iter(void *v)
{
	pthread_mutex_lock(&mtx);
	if (state == s_no_socket)
		get_socket();
	else if (state == s_connecting)
		check_connected();
	else if (state == s_connected)
		try_login();
	else if (state == s_waitlogin || state == s_loggedin)
		try_send_recv();
	else if (state == s_retry)
		if ( (time(NULL) - lastretry) > cfg_retryseconds)
			state = s_no_socket;
	pthread_mutex_unlock(&mtx);

	return TRUE;
}

EXPORT const char info_billing[] = CORE_MOD_INFO("billing");

EXPORT int MM_billing(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		if (!pd || !lm || !ml || !chat || !cmd || !cfg || !oldauth)
			return MM_FAIL;
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		/* cfghelp: Billing:RetryInterval, global, int, def: 30
		 * How many seconds to wait between tries to connect to the
		 * billing server. */
		cfg_retryseconds = cfg->GetInt(GLOBAL, "Billing", "RetryInterval", 30);

		init_dispatch();

		conn.socket = -1;
		drop_connection(s_no_socket);

		pthread_mutex_init(&mtx, NULL);

		ml->SetTimer(do_one_iter, 10, 10, NULL, NULL);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_CHATMSG, onchatmsg, ALLARENAS);
		mm->RegCallback(CB_SET_BANNER, setbanner, ALLARENAS);

		cmd->AddCommand("usage", Cusage, ALLARENAS, usage_help);
		cmd->AddCommand("userdbid", Cuserdbid, ALLARENAS, userdbid_help);
		cmd->AddCommand("userdbadm", Cuserdbadm, ALLARENAS, userdbadm_help);
		cmd->AddCommand(NULL, Cdefault, ALLARENAS, NULL);

		if (net) net->AddPacket(C2S_REGDATA, pdemographics);

		mm->RegInterface(&myauth, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;

		/* clean up sockets */
		drop_connection(s_disabled);

		if (net) net->RemovePacket(C2S_REGDATA, pdemographics);
		cmd->RemoveCommand("usage", Cusage, ALLARENAS);
		cmd->RemoveCommand("userdbid", Cuserdbid, ALLARENAS);
		cmd->RemoveCommand("userdbadm", Cuserdbadm, ALLARENAS);
		cmd->RemoveCommand(NULL, Cdefault, ALLARENAS);
		mm->UnregCallback(CB_SET_BANNER, setbanner, ALLARENAS);
		mm->UnregCallback(CB_CHATMSG, onchatmsg, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		ml->ClearTimer(do_one_iter, NULL);
		deinit_dispatch();
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(oldauth);
		return MM_OK;
	}
	else
		return MM_FAIL;
}


