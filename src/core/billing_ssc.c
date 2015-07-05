
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
#endif

#include "asss.h"
#include "billing.h"
#include "net-client.h"
#include "banners.h"
#include "persist.h"
#include "pwcache.h"
#include "protutil.h"
#include "packets/billing.h"
#include "packets/banners.h"


typedef struct pdata
{
	long billingid;
	unsigned long usage;
	int knowntobiller;
	int demographics_set;
	/* holds copy of name and password temporarily */
	struct
	{
		void (*done)(Player *p, AuthData *data);
		char name[24];
		char pw[24];
	} *logindata;
	char firstused[32];
	struct PlayerScore saved_score;
} pdata;


/* this holds the current status of billing server connection */
local enum
{
	s_no_socket,
	s_connecting,
	s_waitlogin,
	s_loggedin,
	s_retry,
	s_loginfailed,
	s_disabled
} state;

local time_t lastevent;
local int pdkey;
local pthread_mutex_t mtx;

local int cfg_retryseconds;
local int pending_auths;
local int interrupted_auths;
local time_t interrupted_damp_time;

local byte identity[256];
local int idlen;
local const byte zerobanner[BANNER_SIZE];

local ClientConnection *cc;

local Imodman *mm;
local Iplayerdata *pd;
local Ilogman *lm;
local Imainloop *ml;
local Ichat *chat;
local Icmdman *cmd;
local Iconfig *cfg;
local Ipwcache *pwcache;
local Inet *net;
local Inet_client *netcli;
local Istats *stats;
local Icapman *capman;


local void drop_connection(int newstate)
{
	Player *p;
	Link *link;
	pdata *data;
	byte disconn = S2B_SERVER_DISCONNECT;

	/* only announce if changing from loggedin */
	if (state == s_loggedin)
		chat->SendArenaMessage(ALLARENAS, "Notice: Connection to user database server lost");

	/* clear knowntobiller values */
	pd->Lock();
	FOR_EACH_PLAYER_P(p, data, pdkey)
		data->knowntobiller = 0;
	pd->Unlock();

	/* then close socket, if not done already */
	if (cc)
	{
		/* ideally we'd send this reliably, but reliable packets won't
		 * get sent after the DropConnection. */
		netcli->SendPacket(cc, &disconn, sizeof(disconn), NET_PRI_P5);
		netcli->DropConnection(cc);
		cc = NULL;
	}

	state = newstate;
	lastevent = time(NULL);
}

local void pwcache_done(void *clos, int result)
{
	AuthData ad;
	Player *p = clos;
	pdata *data = PPDATA(p, pdkey);

	if (!data->logindata)
	{
		lm->LogP(L_WARN, "billing_ssc", p, "unexpected pwcache response");
		return;
	}

	if (result == MATCH)
	{
		/* correct password, player is ok and authenticated */
		ad.code = AUTH_OK;
		ad.authenticated = TRUE;
		astrncpy(ad.name, data->logindata->name, sizeof(ad.name));
		astrncpy(ad.sendname, data->logindata->name, sizeof(ad.sendname));
		astrncpy(ad.squad, "", sizeof(ad.squad));
	}
	else if (result == NOT_FOUND)
	{
		/* add ^ in front of name and accept as unauthenticated */
		ad.code = AUTH_OK;
		ad.authenticated = FALSE;
		ad.name[0] = ad.sendname[0] = '^';
		astrncpy(ad.name + 1, data->logindata->name, sizeof(ad.name) - 1);
		astrncpy(ad.sendname + 1, data->logindata->name, sizeof(ad.sendname) - 1);
		astrncpy(ad.squad, "", sizeof(ad.squad));
		data->knowntobiller = FALSE;
	}
	else /* mismatch or anything else */
	{
		ad.code = AUTH_BADPASSWORD;
		ad.authenticated = FALSE;
	}

	data->logindata->done(p, &ad);
	afree(data->logindata);
	data->logindata = NULL;
}

/* the auth interface */

local void authenticate(Player *p, struct LoginPacket *lp, int lplen,
			void (*Done)(Player *p, AuthData *data))
{
	pdata *data = PPDATA(p, pdkey);

	pthread_mutex_lock(&mtx);

	/* default to false */
	data->knowntobiller = FALSE;

	/* set up temporary login data struct */
	data->logindata = amalloc(sizeof(*data->logindata));
	astrncpy(data->logindata->name, lp->name,
			sizeof(data->logindata->name));
	if (pwcache)
		astrncpy(data->logindata->pw, lp->password,
				sizeof(data->logindata->pw));
	data->logindata->done = Done;

	if (state == s_loggedin)
	{
		if (pending_auths < 15 && interrupted_auths < 20)
		{
			struct S2B_UserLogin pkt;
			int pktsize;

			pkt.Type = S2B_USER_LOGIN;
			pkt.MakeNew = lp->flags;
			pkt.IPAddress = inet_addr(p->ipaddr);
			astrncpy((char*)pkt.Name,lp->name,sizeof(pkt.Name));
			astrncpy((char*)pkt.Password,lp->password,sizeof(pkt.Password));
			pkt.ConnectionID = p->pid;
			pkt.MachineID = lp->macid;
			pkt.Timezone = (i16)lp->timezonebias;
			pkt.Unused0 = 0;
			pkt.Sysop = 0;
			pkt.ClientVersion = lp->cversion;

			pktsize = offsetof(struct S2B_UserLogin, ClientExtraData);
			if (lplen > offsetof(struct LoginPacket, contid))
			{
				int extlen = lplen-offsetof(struct LoginPacket, contid);
				if (extlen > sizeof(pkt.ClientExtraData))
					extlen = sizeof(pkt.ClientExtraData);
				memcpy(pkt.ClientExtraData,lp->contid,extlen);
				pktsize += extlen;
			}

			netcli->SendPacket(cc, (byte*)&pkt, pktsize, NET_RELIABLE);
			data->knowntobiller = TRUE;
			pending_auths++;
		}
		else
		{
			/* tell user to try again later */
			AuthData ad;
			ad.code = AUTH_SERVERBUSY;
			ad.authenticated = FALSE;
			Done(p, &ad);
			afree(data->logindata);
			data->logindata = NULL;
		}
	}
	else if (pwcache)
	{
		/* biller isn't connected, fall back to pw cache */
		pwcache->Check(lp->name, lp->password, pwcache_done, p);
	}
	else
	{
		/* act like not found in pwcache */
		pwcache_done(p, NOT_FOUND);
	}

	pthread_mutex_unlock(&mtx);
}

local struct Iauth myauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "billing-auth")
	authenticate
};

local int update_score(Player *p, struct PlayerScore *s)
{
	if (p->pkt.killpoints != s->Score ||
	    p->pkt.flagpoints != s->FlagScore ||
	    p->pkt.wins != s->Kills ||
	    p->pkt.losses != s->Deaths ||
	    p->pkt.flagscarried != s->Flags)
	{
		s->Score = p->pkt.killpoints;
		s->FlagScore = p->pkt.flagpoints;
		s->Kills = p->pkt.wins;
		s->Deaths = p->pkt.losses;
		s->Flags = p->pkt.flagscarried;
		return 1;
	}
	return 0;
}

/* catch players logging out */

local void paction(Player *p, int action, Arena *arena)
{
	pdata *data = PPDATA(p, pdkey);
	pthread_mutex_lock(&mtx);
	if (action == PA_DISCONNECT)
	{
		if (data->logindata)
		{
			/* disconnected while waiting for auth */
			if (data->knowntobiller)
			{
				pending_auths--;
				interrupted_auths++;
			}
			afree(data->logindata);
			data->logindata = NULL;
		}
		if (data->knowntobiller)
		{
			struct S2B_UserLogoff pkt;

			pkt.Type = S2B_USER_LOGOFF;
			pkt.ConnectionID = p->pid;
			pkt.DisconnectReason = 0;  //FIXME: put real reason here
			//FIXME: get real latency numbers
			pkt.Latency = 0;
			pkt.Ping = 0;
			pkt.PacketLossS2C = 0;
			pkt.PacketLossC2S = 0;

			if (update_score(p, &data->saved_score))
			{
				pkt.Score = data->saved_score;
				netcli->SendPacket(cc, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			}
			else
				netcli->SendPacket(cc, (byte*)&pkt,
						offsetof(struct S2B_UserLogoff, Score), NET_RELIABLE);
		}
	}
#if 0
	/* i don't want the biller messing with my stats */
	else if (action == PA_ENTERARENA && arena->ispublic)
	{
		if (data->setpublicscore)
		{
			data->setpublicscore = FALSE;
			stats->SetStat(p, STAT_KILL_POINTS, INTERVAL_RESET, data->saved_score.Score);
			stats->SetStat(p, STAT_FLAG_POINTS, INTERVAL_RESET, data->saved_score.FlagScore);
			stats->SetStat(p, STAT_KILLS,       INTERVAL_RESET, data->saved_score.Kills);
			stats->SetStat(p, STAT_DEATHS,      INTERVAL_RESET, data->saved_score.Deaths);
			stats->SetStat(p, STAT_FLAG_PICKUPS,INTERVAL_RESET, data->saved_score.Flags);
			stats->SendUpdates();
		}
	}
#endif
	else if (action == PA_LEAVEARENA && ARENA_IS_PUBLIC(arena))
	{
		data->saved_score.Score = stats->GetStat(p, STAT_KILL_POINTS, INTERVAL_RESET);
		data->saved_score.FlagScore = stats->GetStat(p, STAT_FLAG_POINTS, INTERVAL_RESET);
		data->saved_score.Kills = stats->GetStat(p, STAT_KILLS, INTERVAL_RESET);
		data->saved_score.Deaths = stats->GetStat(p, STAT_DEATHS, INTERVAL_RESET);
		data->saved_score.Flags = stats->GetStat(p, STAT_FLAG_PICKUPS,INTERVAL_RESET);
	}
	pthread_mutex_unlock(&mtx);
}


/* handle chat messages that have to go to the biller */

local void onchatmsg(Player *p, int type, int sound, Player *target, int freq, const char *text)
{
	pdata *data = PPDATA(p, pdkey);

	if (!data->knowntobiller)
		return;

	if (type == MSG_CHAT)
	{
		struct S2B_UserChannelChat pkt;
		const char *t;

		pkt.Type = S2B_USER_CHANNEL_CHAT;
		pkt.ConnectionID = p->pid;
		memset(pkt.ChannelNr,0,sizeof(pkt.ChannelNr));
		/* note that this supports a channel name in place of the usual
		 * channel number. e.g., ;foo;this is a message to the foo channel.
		 * most billers probably don't support this feature yet. */
		t = strchr(text, ';');
		if (t && (t-text) < 10)
			text = delimcpy((char*)pkt.ChannelNr, text, sizeof(pkt.ChannelNr), ';');
		else
			strcpy((char*)pkt.ChannelNr, "1");
		astrncpy((char*)pkt.Text, text, sizeof(pkt.Text));
		pthread_mutex_lock(&mtx);
		netcli->SendPacket(cc, (byte*)&pkt, strchr((char*)pkt.Text,0) + 1 - (char*)&pkt, NET_RELIABLE);
		pthread_mutex_unlock(&mtx);
	}
	else if (type == MSG_REMOTEPRIV && target == NULL)
	{
		/* only grab these if the server didn't handle them internally */
		struct S2B_UserPrivateChat pkt;
		char dest[32];
		const char *t;

		pkt.Type = S2B_USER_PRIVATE_CHAT;
		//for some odd reason ConnectionID>=0 indicates global broadcast message
		pkt.ConnectionID = -1;
		pkt.GroupID = 1;
		pkt.SubType = 2;
		pkt.Sound = sound;

		t = delimcpy(dest, text+1, sizeof(dest), ':');
		if (text[0] != ':' || !t)
			lm->LogP(L_MALICIOUS, "billing_ssc", p, "malformed remote private message");
		else
		{
			snprintf((char*)pkt.Text, sizeof(pkt.Text), ":%s:(%s)>%s", dest, p->name, t);
			pthread_mutex_lock(&mtx);
			netcli->SendPacket(cc, (byte*)&pkt, strchr((char*)pkt.Text,0) + 1 - (char*)&pkt, NET_RELIABLE);
			pthread_mutex_unlock(&mtx);
		}
	}
}

/* and commands that go to the biller */

local int findchat(const char *chat, const char *list)
{
	const char *tmp = NULL;
	char lchat[32];

	while (strsplit(list, "\t:;,", lchat, sizeof(lchat), &tmp))
		if (!strcasecmp(lchat,chat))
			return TRUE;
	return FALSE;
}

local void rewrite_chat_command(Player *p, const char *line, struct S2B_UserCommand *pkt)
{
	/* process local and staff chats */

	/* cfghelp: Billing:StaffChats, global, string
	 * Comma separated staff chat list. */
	const char *staffchats = cfg->GetStr(GLOBAL, "Billing", "StaffChats");
	/* cfghelp: Billing:StaffChatPrefix, global, string
	 * Secret prefix to prepend to staff chats */
	const char *staffprefix = cfg->GetStr(GLOBAL, "Billing", "StaffChatPrefix");
	/* cfghelp: Billing:StaffChats, global, string
	 * Comma separated staff zone local list. */
	const char *localchats = cfg->GetStr(GLOBAL, "Billing", "LocalChats");
	/* cfghelp: Billing:LocalChatPrefix, global, string
	 * Secret prefix to prepend to local chats */
	const char *localprefix = cfg->GetStr(GLOBAL, "Billing", "LocalChatPrefix");

	char *d = (char*)pkt->Text + 6;
	line += 5; /* skip chat= */

	strcpy((char*)pkt->Text, "?chat=");
	while (line && d < (char *)pkt->Text+sizeof(pkt->Text)-64)
	{
		char chatname[32],origchatname[32];
		char *e;

		line = delimcpy(chatname, line, sizeof(chatname), ',');
		strcpy(origchatname, chatname);

		e = strrchr(chatname, '/');
		if (e)
			*e=0;
		e = strchr(chatname, 0);
		while (e > chatname && e[-1] == ' ')
			*(--e) = 0;
		if (!chatname[0])
			continue;

		if (localchats && findchat(chatname, localchats))
		{
			int len = snprintf(d, 32, "$l$%s|", localprefix ? localprefix : "");
			if (len > 31)
				len = 31;
			d += len;
		}

		/* FIXME: is this broken when a staffchat == a localchat ? 
		 * (Even if it is, that shouldn't happen.) */
		if (staffchats && capman &&
			capman->HasCapability(p, CAP_SENDMODCHAT) &&
			findchat(chatname, staffchats))
		{
			int len = snprintf(d, 32, "$s$%s|", staffprefix ? staffprefix : "");
			if (len > 31)
				len = 31;
			d += len;
		}

		strcpy(d, origchatname);
		d = strchr(d, 0);
		if (line)
			*d++=',';
	}
	*d = 0;
}

local void Cdefault(const char *tc, const char *line, Player *p, const Target *target)
{
	pdata *data = PPDATA(p, pdkey);
	struct S2B_UserCommand pkt = { S2B_USER_COMMAND, p->pid };

	if (!data->knowntobiller)
		return;

	if (target->type != T_ARENA)
	{
		lm->LogP(L_DRIVEL, "billing_ssc", p, "unknown command with bad target: %s", line);
		return;
	}

	if (IS_RESTRICTED(chat->GetPlayerChatMask(p), MSG_BCOMMAND))
		return;

	if (!strncasecmp(line, "chat", 4) && (line[4] == ' ' || line[4] == '='))
		rewrite_chat_command(p, line, &pkt);
	else
	{
		pkt.Text[0] = '?';
		astrncpy((char*)(pkt.Text+1), line, sizeof(pkt.Text)-1);
	}

	pthread_mutex_lock(&mtx);
	netcli->SendPacket(cc, (byte*)&pkt, strchr((char*)pkt.Text,0) + 1 - (char*)&pkt, NET_RELIABLE);
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
}


local helptext_t userid_help =
"Targets: player or none\n"
"Args: none\n"
"Displays the user database id of the target player, or yours if no\n"
"target.\n";

local void Cuserid(const char *tc, const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : p;
	pdata *tdata = PPDATA(t, pdkey);
	if (tdata->knowntobiller)
		chat->SendMessage(p, "%s has user id %ld",
				t->name, tdata->billingid);
	else
		chat->SendMessage(p, "user id unknown for %s", t->name);
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
		drop_connection(s_disabled);
		chat->SendMessage(p, "user database connection disabled");
	}
	else if (!strcmp(params, "connect"))
	{
		if (state == s_loginfailed || state == s_disabled || state == s_retry)
		{
			state = s_no_socket;
			chat->SendMessage(p, "user database server connection reactivated");
		}
		else
			chat->SendMessage(p, "user database server connection already active");
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
		chat->SendMessage(p, "user database status: %s  pending auths: %d", t, pending_auths);
	}

	pthread_mutex_unlock(&mtx);
}


/* handlers for all messages from the biller */

local void process_user_login(const char *data,int len)
{
	Player *p;
	struct B2S_UserLogin *pkt = (struct B2S_UserLogin *)data;
	AuthData ad;
	pdata *bdata;

	if (len < offsetof(struct B2S_UserLogin, Score))
	{
		lm->Log(L_WARN, "<billing_ssc> invalid login packet len %d", len);
		return;
	}

	p = pd->PidToPlayer(pkt->ConnectionID);
	if (!p)
	{
		lm->Log(L_WARN, "<billing_ssc> biller sent player auth response for unknown pid %d", pkt->ConnectionID);
		return;
	}

	bdata = PPDATA(p, pdkey);
	if (!bdata->logindata)
	{
		lm->LogP(L_WARN, "billing_ssc", p, "unexpected player auth response");
		return;
	}

	if (pkt->Result == B2S_LOGIN_OK ||
	    pkt->Result == B2S_LOGIN_DEMOVERSION ||
	    pkt->Result == B2S_LOGIN_ASKDEMOGRAPHICS)
	{
		snprintf(bdata->firstused, sizeof(bdata->firstused), "%02d-%04d-%02d %02d:%02d:%02d",
					pkt->FirstLogin.Month, pkt->FirstLogin.Year, pkt->FirstLogin.Day,
					pkt->FirstLogin.Hour, pkt->FirstLogin.Min,pkt->FirstLogin.Sec);
		bdata->usage = pkt->SecondsPlayed;
		bdata->billingid = pkt->UserID;
#if 0
		if (len == sizeof(*pkt))
		{
			bdata->saved_score = pkt->Score;
			bdata->setpublicscore = TRUE;
		}
		else
#endif
			memset(&bdata->saved_score, 0, sizeof(bdata->saved_score));

		ad.demodata = (pkt->Result == B2S_LOGIN_ASKDEMOGRAPHICS || pkt->Result == B2S_LOGIN_DEMOVERSION);
		ad.code = (pkt->Result == B2S_LOGIN_ASKDEMOGRAPHICS || pkt->Result == B2S_LOGIN_DEMOVERSION) ? AUTH_ASKDEMOGRAPHICS : AUTH_OK;
		ad.authenticated = TRUE;
		astrncpy(ad.name, (char*)pkt->Name, sizeof(ad.name));
		astrncpy(ad.sendname, (char*)pkt->Name, sizeof(ad.sendname));
		astrncpy(ad.squad, (char*)pkt->Squad, sizeof(ad.squad));
		/* pass banner to banners module only if it exists */
		if (memcmp(pkt->Banner, zerobanner, BANNER_SIZE))
		{
			Ibanners *bnr = mm->GetInterface(I_BANNERS, ALLARENAS);
			if (bnr)
				bnr->SetBanner(p, (Banner*)pkt->Banner);
			mm->ReleaseInterface(bnr);
		}

		if (pwcache)
			pwcache->Set(bdata->logindata->name, bdata->logindata->pw);
	}
	else
	{
		ad.demodata = 0;
		if (pkt->Result == B2S_LOGIN_NEWUSER)
			ad.code = AUTH_NEWNAME;
		else if (pkt->Result == B2S_LOGIN_INVALIDPW)
			ad.code = AUTH_BADPASSWORD;
		else if (pkt->Result == B2S_LOGIN_BANNED)
			ad.code = AUTH_LOCKEDOUT;
		else if (pkt->Result == B2S_LOGIN_NONEWCONNS)
			ad.code = AUTH_NONEWCONN;
		else if (pkt->Result == B2S_LOGIN_BADUSERNAME)
			ad.code = AUTH_BADNAME;
		else if (pkt->Result == B2S_LOGIN_SERVERBUSY)
			ad.code = AUTH_SERVERBUSY;
		else
			ad.code = AUTH_NOPERMISSION;
		ad.authenticated = FALSE;
	}
	bdata->logindata->done(p, &ad);
	afree(bdata->logindata);
	bdata->logindata = NULL;
	pending_auths--;
}

local void process_rmt(const char *data,int len)
{
	struct B2S_UserPrivateChat *pkt = (struct B2S_UserPrivateChat *)data;

	if (len < offsetof(struct B2S_UserPrivateChat, Text[1]) || pkt->SubType!=2 ||
			!memchr(pkt->Text, 0, len-offsetof(struct B2S_UserPrivateChat,Text)))
	{
		lm->Log(L_WARN, "<billing_ssc> invalid chat packet len %d", len);
		return;
	}

	if (*pkt->Text==':')  /* private message */
	{
		Player *p;
		char recipient[32], sender[32];
		const char *t;
		const char *text = delimcpy(recipient, (char*)(pkt->Text+1), sizeof(recipient), ':');

		if (!text || *text != '(')
			return;

		/* this is a horrible feature of the protocol: how do you parse
		 * :recpient:(funny)>name)>message
		 * ? we take the first ')>' as the delimiter. */
		t = strstr(text + 1, ")>");
		if (!t || (t - text) > 30)
			return;
		memset(sender, 0, sizeof(sender));
		memcpy(sender, text + 1, t - text - 1);
		text = t + 2;

		if (recipient[0] == '#')
		{
			/* squad msg. check for the null squad name */
			if (recipient[1])
			{
				Link *link;
				LinkedList set = LL_INITIALIZER;
				pd->Lock();
				FOR_EACH_PLAYER(p)
					if (strcasecmp(recipient+1, p->squad) == 0)
						LLAdd(&set, p);
				pd->Unlock();
				chat->SendRemotePrivMessage(&set, pkt->Sound, recipient+1,
						sender, text);
#ifdef CFG_LOG_PRIVATE
				lm->Log(L_DRIVEL, "<chat> (%d rcpts) incoming remote squad msg: %s:%s> %s",
						LLCount(&set), recipient+1, sender, text);
#endif
				LLEmpty(&set);
			}
		}
		else if ((p = pd->FindPlayer(recipient)))
		{
			Link link = { NULL, p };
			LinkedList list = { &link, &link };
			/* format of text is "(sender)>msg" */
			chat->SendRemotePrivMessage(&list, pkt->Sound, NULL, sender, text);
#ifdef CFG_LOG_PRIVATE
			/* this is sort of wrong, but i think it makes more sense to
			 * use the module name chat here for ease of filtering. */
			lm->Log(L_DRIVEL, "<chat> [%s] incoming remote priv: %s> %s",
					p->name, sender, text);
#endif
		}
		else
			lm->Log(L_DRIVEL, "<billing_ssc> unknown destination for incoming remote priv: %s",
					recipient);
	}
	else  /* broadcast message */
	{
		chat->SendArenaSoundMessage(ALLARENAS, pkt->Sound, "%s", pkt->Text);
	}
}

local void process_kickout(const char *data,int len)
{
	struct B2S_UserKickout *pkt = (struct B2S_UserKickout *)data;
	Player *p;

	if (len < sizeof(*pkt))
	{
		lm->Log(L_WARN, "<billing_ssc> invalid kickout packet len %d", len);
		return;
	}

	p = pd->PidToPlayer(pkt->ConnectionID);
	if (p)
	{
		lm->LogP(L_INFO, "billing_ssc", p, "player kicked out by user database server (%d)",
				pkt->Reason);
		pd->KickPlayer(p);
	}
}

local void process_chanchat(const char *data,int len)
{
	struct B2S_UserChannelChat *pkt = (struct B2S_UserChannelChat *)data;
	Player *p;

	if (len < offsetof(struct B2S_UserChannelChat, Text[1]) ||
			!memchr(pkt->Text, 0, len-offsetof(struct B2S_UserChannelChat,Text)))
	{
		lm->Log(L_WARN, "<billing_ssc> invalid chat channel packet len %d", len);
		return;
	}

	p = pd->PidToPlayer(pkt->ConnectionID);
	if (p)
	{
		Link link = { NULL, p };
		LinkedList list = { &link, &link };

		chat->SendAnyMessage(&list, MSG_CHAT, 0, NULL,
				"%d:%s", pkt->ChannelNr, pkt->Text);
#ifdef CFG_LOG_PRIVATE
		lm->Log(L_DRIVEL, "<chat> [%s] incoming chat msg: %d:%s",
				p->name, pkt->ChannelNr, pkt->Text);
#endif
	}
}

local void process_mchanchat(const char *data, int len)
{
	struct B2S_UserMChannelChat *pkt = (struct B2S_UserMChannelChat *)data;
	const char *txt;
	int i;

	if (len < offsetof(struct B2S_UserMChannelChat, Recipient[1]) ||
	    ((txt=(const char *)&pkt->Recipient[pkt->Count]) - data) > len ||
	    !memchr(txt,0,data+len-txt))
	{
		lm->Log(L_WARN, "<billing_ssc> invalid mchannel chat packet len %d", len);
		return;
	}

	for(i = 0; i < pkt->Count; i++)
	{
		Player *p = pd->PidToPlayer(pkt->Recipient[i].ConnectionID);
		if (p)
		{
			Link link = { NULL, p };
			LinkedList list = { &link, &link };
			chat->SendAnyMessage(&list, MSG_CHAT, 0, NULL,
					"%d:%s", pkt->Recipient[i].ChanNr, txt);
		}
	}
#ifdef CFG_LOG_PRIVATE
	lm->Log(L_DRIVEL, "<chat> (%d rcpts) incoming chat msg: %s",
			pkt->Count, txt);
#endif
}

local void process_cmdchat(const char *data,int len)
{
	struct B2S_UserCommandChat *pkt = (struct B2S_UserCommandChat *)data;
	Player *p;

	if (len < offsetof(struct B2S_UserCommandChat, Text[1]) ||
			!memchr(pkt->Text, 0, len-offsetof(struct B2S_UserCommandChat,Text)))
	{
		lm->Log(L_WARN, "<billing_ssc> invalid command chat packet len %d", len);
		return;
	}

	p = pd->PidToPlayer(pkt->ConnectionID);
	if (p)
	{
		const char *t=strchr((char*)pkt->Text,'|');
		if (t && !strncmp((char*)pkt->Text, "$l$", 3))
			chat->SendMessage(p, "(local) %s", t+1);
		else if (t && !strncmp((char*)pkt->Text, "$s$", 3))
			chat->SendMessage(p, "(staff) %s", t+1);
		else
			chat->SendMessage(p, "%s", pkt->Text);
#ifdef CFG_LOG_PRIVATE
		lm->Log(L_DRIVEL, "<chat> [%s] incoming command chat msg: %s",
				p->name, pkt->Text);
#endif
	}
}

local void process_userpkt(const char *data, int len)
{
	struct B2S_UserPacket *pkt = (struct B2S_UserPacket *)data;
	int datalen = len - offsetof(struct B2S_UserPacket, Data[1]);

	if (datalen <= 0)
	{
		lm->Log(L_WARN, "<billing_ssc> invalid user packet len %d", len);
		return;
	}

	if (pkt->ConnectionID == 0xffffffffU)
	{
		/* send to all players not allowed */
		lm->Log(L_WARN, "<billing_ssc> b2s user packet filtered (target all)");
		/* unlikely to get this, maybe during score reset?
		net->SendToArena(ALLARENAS, NULL, pkt->Data, datalen, NET_RELIABLE);
		*/
	}
	else
	{
		Player *p = pd->PidToPlayer(pkt->ConnectionID);

		if (p)
		{
			/* only allow S2C_LOGINTEXT for banned players to get the ban text. */
			if (*pkt->Data == S2C_LOGINTEXT)
			{
				net->SendToOne(p, pkt->Data, datalen, NET_RELIABLE);
			}
			else
			{
				lm->Log(L_WARN, "<billing_ssc> b2s user packet "
					"filtered (target [%s])", p->name);
			}
		}
		else
		{
			lm->Log(L_WARN, "<billing_ssc> b2s user packet "
				"unknown pid (%d)", pkt->ConnectionID);
		}
	}
}

local void process_scorereset(const char *data,int len)
{
	struct B2S_Scorereset *pkt = (struct B2S_Scorereset *)data;

	if (len < sizeof(*pkt) || (i32)pkt->ScoreID != -(i32)pkt->ScoreIDNeg)
	{
		lm->Log(L_WARN, "<billing_ssc> invalid scorereset packet len %d", len);
		return;
	}

	/* cfghelp: Billing:HonorScoreResetRequests, global, bool, def: 1
	 * Whether to reset scores when the billing server says it is time to */
	if (cfg->GetInt(GLOBAL, "Billing", "HonorScoreResetRequests", 1))
	{
		Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);

		/* reset scores in public arenas */
		if (persist)
		{
			persist->EndInterval(AG_PUBLIC, NULL, INTERVAL_RESET);
			lm->Log(L_INFO, "<billing_ssc> billing server requested score reset, resetting scores");
		}
		else
		{
			lm->Log(L_WARN, "<billing_ssc> billing server requested score reset, persist interface unavailable!");
		}
		mm->ReleaseInterface(persist);
	}
	else
	{
		lm->Log(L_INFO, "<billing_ssc> billing server requested score reset, but honoring such requests is disabled");
	}
}

local void process_identity(const char *data, int len)
{
	struct B2S_BillingIdentity *pkt = (struct B2S_BillingIdentity *)data;
	len--;
	if (len > sizeof(identity))
		len = sizeof(identity);
	memcpy(identity, pkt->IDData, len);
	idlen = len;
}

/* the dispatcher */

local void logged_in(void)
{
	struct S2B_ServerCapabilities cpkt = {S2B_SERVER_CAPABILITIES,1,1,0};

	chat->SendArenaMessage(ALLARENAS,
			"Notice: Connection to user database server restored. "
			"Log in again for full functionality");

	netcli->SendPacket(cc, (byte*)&cpkt, sizeof(cpkt), NET_RELIABLE);
	state = s_loggedin;
	idlen = -1;

	lm->Log(L_INFO, "<billing_ssc> logged in to user database server");
}


local void process_packet(byte *pkt, int len)
{
	pthread_mutex_lock(&mtx);

	/* move past waitlogin on any packet (other than 0007, which won't
	 * get here). */
	if (state == s_waitlogin)
		logged_in();

	switch (*pkt)
	{
		case B2S_USER_LOGIN:
			process_user_login((char*)pkt,len);
			break;
		case B2S_USER_PRIVATE_CHAT:
			process_rmt((char*)pkt,len);
			break;
		case B2S_USER_KICKOUT:
			process_kickout((char*)pkt,len);
			break;
		case B2S_USER_COMMAND_CHAT:
			process_cmdchat((char*)pkt,len);
			break;
		case B2S_USER_CHANNEL_CHAT:
			process_chanchat((char*)pkt,len);
			break;
		case B2S_SCORERESET:
			process_scorereset((char*)pkt,len);
			break;
		case B2S_USER_PACKET:
			process_userpkt((char*)pkt,len);
			break;
		case B2S_BILLING_IDENTITY:
			process_identity((char*)pkt,len);
			break;
		case B2S_USER_MCHANNEL_CHAT:
			process_mchanchat((char*)pkt,len);
			break;
		default:
			lm->Log(L_WARN, "<billing_ssc> unsupported packet type %d", *pkt);
	}

	pthread_mutex_unlock(&mtx);
}

local void got_connection(void)
{
	int port;
	struct S2B_ServerConnect pkt;
	/* cfghelp: Billing:ServerName, global, string
	 * The server name to send to the user database server. */
	const char *servername = cfg->GetStr(GLOBAL, "Billing", "ServerName");
	/* cfghelp: Billing:Password, global, string
	 * The password to log in to the user database server with. */
	const char *password = cfg->GetStr(GLOBAL, "Billing", "Password");

	pkt.Type = S2B_SERVER_CONNECT;
	/* cfghelp: Billing:ServerID, global, int, def: 0
	 * ServerID identifying zone to user database server. */
	pkt.ServerID = cfg->GetInt(GLOBAL, "Billing", "ServerID", 0);
	/* cfghelp: Billing:GroupID, global, int, def: 1
	 * GroupID identifying zone to user database server. */
	pkt.GroupID = cfg->GetInt(GLOBAL, "Billing", "GroupID", 1);
	/* cfghelp: Billing:ScoreID, global, int, def: 0
	 * Score realm. */
	pkt.ScoreID = cfg->GetInt(GLOBAL, "Billing", "ScoreID", 0);

	astrncpy((char*)pkt.ServerName, servername?servername:"", sizeof(pkt.ServerName));
	if (net->GetListenData(0, &port, NULL, 0))
		pkt.Port = port;
	else
		pkt.Port = 0;
	astrncpy((char*)pkt.Password, password?password:"", sizeof(pkt.Password));

	pthread_mutex_lock(&mtx);
	netcli->SendPacket(cc, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	state = s_waitlogin;
	lastevent = time(NULL);
	pthread_mutex_unlock(&mtx);

	lm->Log(L_INFO, "<billing_ssc> connected to user database server, logging in");
}


local void got_disconnection(void)
{
	cc = NULL;
	lm->Log(L_INFO, "<billing_ssc> lost connection to user database server "
			"(auto-retry in %d seconds)", cfg_retryseconds);
	drop_connection(s_retry);
}


local struct Iclientconn ccint =
	{ got_connection, process_packet, got_disconnection };

/* runs often */

local int do_one_iter(void *v)
{
	pthread_mutex_lock(&mtx);

	if (state == s_no_socket)
	{
		/* cfghelp: Billing:IP, global, string
		 * The ip address of the user database server (no dns hostnames allowed). */
		const char *ipaddr = cfg->GetStr(GLOBAL, "Billing", "IP");
		/* cfghelp: Billing:Port, global, int, def: 1850
		 * The port to connect to on the user database server. */
		int port = cfg->GetInt(GLOBAL, "Billing", "Port", 1850);

		if (!ipaddr)
		{
			lm->Log(L_WARN, "<billing_ssc> no Billing:IP set. user database disabled");
			drop_connection(s_disabled);
		}
		else
		{
			Iclientencrypt *clienc = mm->GetInterfaceByName("enc-vie-client");
			cc = netcli->MakeClientConnection(ipaddr, port, &ccint, clienc);
			if (cc)
			{
				state = s_connecting;
				lm->Log(L_INFO, "<billing_ssc> connecting to user database server at %s:%d",
						ipaddr, port);
			}
			else
				state = s_retry;
			lastevent = time(NULL);
		}
	}
	else if (state == s_connecting)
	{
		/* just wait */
	}
	else if (state == s_waitlogin)
	{
		/* this billing protocol doesn't respond to the login packet,
		 * but process_packet will set the next state when it gets any
		 * packet. */
		/* um, but only ssc proactively sends us a packet after a good
		 * login. for others, assume good after a few seconds without
		 * getting kicked off. */
		if (time(NULL) - lastevent >= 5)
			logged_in();
	}
	else if (state == s_loggedin)
	{
		time_t now = time(NULL);
		if (now - lastevent >= 60)
		{
			u8 pkt=S2B_PING;
			netcli->SendPacket(cc, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			lastevent = now;
		}

		if (now - interrupted_damp_time >= 10)
		{
			interrupted_auths /= 2;
			interrupted_damp_time = now;
		}
	}
	else if (state == s_retry)
		if ( (time(NULL) - lastevent) > cfg_retryseconds)
			state = s_no_socket;
	pthread_mutex_unlock(&mtx);

	return TRUE;
}


local void PDemographics(Player *p, byte *opkt, int l)
{
	pdata *data = PPDATA(p, pdkey);
	struct S2B_UserDemographics pkt;

	if(l - 1 > sizeof(pkt.Data))
	{
		lm->LogP(L_MALICIOUS, "billing_ssc", p, "invalid demographics packet len %d.", l);
		return;
	}

	pthread_mutex_lock(&mtx);

	if (data->demographics_set)
	{
		lm->LogP(L_MALICIOUS, "billing_ssc", p, "duplicate demographics packet.");
	}
	else if (data->knowntobiller)
	{
		pkt.Type = S2B_USER_DEMOGRAPHICS;
		pkt.ConnectionID = p->pid;
		memcpy(pkt.Data, opkt+1, l-1);
		netcli->SendPacket(cc, (byte*)&pkt, offsetof(struct S2B_UserDemographics, Data) + l - 1, NET_RELIABLE);
		data->demographics_set = TRUE;
	}

	pthread_mutex_unlock(&mtx);
}


local void setbanner(Player *p, Banner *banner, int from_player)
{
	if (from_player)
	{
		static ticks_t lastbsend = 0;

		pdata *data = PPDATA(p, pdkey);
		ticks_t now;

		pthread_mutex_lock(&mtx);

		now = current_ticks();
		if (lastbsend == 0)
			lastbsend = TICK_MAKE(now - 1000);

		/* only allow 1 banner every .2 seconds to get to biller. any more
		 * get dropped. */
		if (data->knowntobiller && TICK_DIFF(now, lastbsend) > 20)
		{
			struct S2B_UserBanner bpkt;

			bpkt.Type = S2B_USER_BANNER;
			bpkt.ConnectionID = p->pid;
			memcpy(bpkt.Data, banner, sizeof(bpkt.Data));
			netcli->SendPacket(cc, (byte*)&bpkt, sizeof(bpkt), NET_RELIABLE);

			lastbsend = now;
		}

		pthread_mutex_unlock(&mtx);
	}
}


local billing_state_t GetStatus(void)
{
	billing_state_t ret;
	pthread_mutex_lock(&mtx);
	switch (state)
	{
		case s_no_socket: ret = BILLING_DOWN; break;
		case s_connecting: ret = BILLING_DOWN; break;
		case s_waitlogin: ret = BILLING_DOWN; break;
		case s_loggedin: ret = BILLING_UP; break;
		case s_retry: ret = BILLING_DOWN; break;
		case s_loginfailed: ret = BILLING_DISABLED; break;
		case s_disabled: ret = BILLING_DISABLED; break;
		default: ret = BILLING_DISABLED; break;
	};
	pthread_mutex_unlock(&mtx);
	return ret;
}

local int GetIdentity(byte *buf, int len)
{
	if (len > idlen)
		len = idlen;
	if (len > 0)
		memcpy(buf, identity, len);
	return len;
}

local Ibilling billingint =
{
	INTERFACE_HEAD_INIT(I_BILLING, "billing_ssc")
	GetStatus, GetIdentity
};

EXPORT const char info_biling_ssc[] = CORE_MOD_INFO("billing_ssc");

EXPORT int MM_billing_ssc(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		netcli = mm->GetInterface(I_NET_CLIENT, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pwcache = mm->GetInterface(I_PWCACHE, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);

		if (!net || !netcli || !pd || !lm || !ml || !chat || !cmd ||
				!cfg || !stats)
			return MM_FAIL;
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		net->AddPacket(C2S_REGDATA, PDemographics);

		drop_connection(s_no_socket);

		/* cfghelp: Billing:RetryInterval, global, int, def: 180
		 * How many seconds to wait between tries to connect to the
		 * user database server. */
		cfg_retryseconds = cfg->GetInt(GLOBAL, "Billing", "RetryInterval", 180);
		pending_auths = interrupted_auths = 0;

		pthread_mutex_init(&mtx, NULL);

		ml->SetTimer(do_one_iter, 10, 10, NULL, NULL);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_CHATMSG, onchatmsg, ALLARENAS);
		mm->RegCallback(CB_SET_BANNER, setbanner, ALLARENAS);

		cmd->AddCommand("usage", Cusage, ALLARENAS, usage_help);
		cmd->AddCommand("userid", Cuserid, ALLARENAS, userid_help);
		cmd->AddCommand("userdbadm", Cuserdbadm, ALLARENAS, userdbadm_help);
		cmd->AddCommand(NULL, Cdefault, ALLARENAS, NULL);

		mm->RegInterface(&myauth, ALLARENAS);
		mm->RegInterface(&billingint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;

		if (mm->UnregInterface(&billingint, ALLARENAS))
			return MM_FAIL;

		/* make sure net won't be calling back into us anymore */
		drop_connection(s_disabled);

		cmd->RemoveCommand("usage", Cusage, ALLARENAS);
		cmd->RemoveCommand("userid", Cuserid, ALLARENAS);
		cmd->RemoveCommand("userdbadm", Cuserdbadm, ALLARENAS);
		cmd->RemoveCommand(NULL, Cdefault, ALLARENAS);

		net->RemovePacket(C2S_REGDATA, PDemographics);

		mm->UnregCallback(CB_CHATMSG, onchatmsg, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->UnregCallback(CB_SET_BANNER, setbanner, ALLARENAS);
		ml->ClearTimer(do_one_iter, NULL);

		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(net);
		mm->ReleaseInterface(netcli);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pwcache);
		mm->ReleaseInterface(stats);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	else
		return MM_FAIL;
}
