
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>

#ifndef WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "zlib.h"

#include "asss.h"
#include "persist.h"


/* STRUCTS */

#include "packets/login.h"

#include "packets/loginresp.h"

typedef struct
{
	AuthData *authdata;
	struct LoginPacket *loginpkt;
	int lplen;
	Player *replaced_by;
	unsigned hasdonegsync : 1;
	unsigned hasdoneasync : 1;
	unsigned hasdonegcallbacks : 1;
	unsigned padding : 29;
} pdata;


/* PROTOTYPES */

/* packet funcs */
local void PLogin(Player *, byte *, int);
local void MLogin(Player *, const char *);

local void AuthDone(Player *, AuthData *);
local void player_sync_done(Player *);

local int SendKeepalive(void *);
local int process_player_states(void *);
local void SendLoginResponse(Player *);

/* default auth, can be replaced */
local void DefaultAuth(Player *, struct LoginPacket *, int, void (*)(Player *, AuthData *));


/* GLOBALS */

local Imodman *mm;
local Iplayerdata *pd;
local Imainloop *ml;
local Iconfig *cfg;
local Inet *net;
local Ichatnet *chatnet;
local Ilogman *lm;
local Imapnewsdl *map;
local Iarenaman *aman;
local Icapman *capman;
local Ipersist *persist;
local Istats *stats;

local int pdkey;

#define CVERSION_CONT 40
#define CVERSION_VIE 134

local u32 contchecksum, codechecksum;

#define CONT_EXE_FILE "clients/continuum.exe"
#define CONT_CSUM_FILE "scrty"

local Iauth _iauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "auth-default")
	DefaultAuth
};


/* FUNCTIONS */


local u32 get_checksum(const char *file)
{
	FILE *f;
	char buf[8192];
	uLong crc = crc32(0, Z_NULL, 0);
	if (!(f = fopen(file, "rb")))
		return (u32)-1;
	while (!feof(f))
	{
		int b = fread(buf, 1, sizeof(buf), f);
		crc = crc32(crc, (unsigned char *)buf, b);
	}
	fclose(f);
	return crc;
}

local u32 get_u32(const char *file, int offset)
{
	FILE *f;
	u32 buf = (u32)-1;
	if ((f = fopen(file, "rb")))
	{
		if (fseek(f, offset, SEEK_SET) == 0)
			fread(&buf, sizeof(u32), 1, f);
		fclose(f);
	}
	return buf;
}

EXPORT const char info_core[] = CORE_MOD_INFO("core");

EXPORT int MM_core(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		map = mm->GetInterface(I_MAPNEWSDL, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		if (!pd || !lm || !cfg || !map || !aman || !ml) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		/* set up callbacks */
		if (net)
		{
			net->AddPacket(C2S_LOGIN, PLogin);
			net->AddPacket(C2S_CONTLOGIN, PLogin);
		}
		if (chatnet)
			chatnet->AddHandler("LOGIN", MLogin);

		ml->SetTimer(process_player_states, 10, 10, NULL, NULL);

		/* register default interfaces which may be replaced later */
		mm->RegInterface(&_iauth, ALLARENAS);

		/* set up periodic events */
		ml->SetTimer(SendKeepalive, 500, 500, NULL, NULL);

		contchecksum = get_checksum(CONT_EXE_FILE);
		codechecksum = get_u32(CONT_CSUM_FILE, 4);

		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		stats = mm->GetInterface(I_STATS, ALLARENAS);
	}
	else if (action == MM_PREUNLOAD)
	{
		/* FIXME: if this module attempts to unload,
		 * it should check if it can before releasing these */
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(stats);
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_iauth, ALLARENAS))
			return MM_FAIL;
		ml->ClearTimer(SendKeepalive, NULL);
		ml->ClearTimer(process_player_states, NULL);
		if (net)
		{
			net->RemovePacket(C2S_LOGIN, PLogin);
			net->RemovePacket(C2S_CONTLOGIN, PLogin);
		}
		if (chatnet)
			chatnet->RemoveHandler("LOGIN", MLogin);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(map);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(capman);
		return MM_OK;
	}
	return MM_FAIL;
}


int process_player_states(void *v)
{
	int ns, oldstatus, requested_ship;
	Player *player;
	Link *link;

	/* put pending actions here while processing the player list */
	struct action_t
	{
		Player *player;
		int oldstatus;
	};
	LinkedList actions = LL_INITIALIZER;

	pd->WriteLock();
	FOR_EACH_PLAYER(player)
	{
		struct action_t * action;

		oldstatus = player->status;

		switch (oldstatus)
		{
			/* for all of these states, there's nothing to do in this
			 * loop */
			case S_UNINITIALIZED:
			case S_WAIT_AUTH:
			case S_WAIT_GLOBAL_SYNC1:
			case S_WAIT_GLOBAL_SYNC2:
			case S_WAIT_ARENA_SYNC1:
			case S_WAIT_ARENA_SYNC2:
			case S_PLAYING:
			case S_TIMEWAIT:
				continue;

			/* this is an interesting state: this function is
			 * responsible for some transitions away from loggedin. we
			 * also do the whenloggedin transition if the player is just
			 * connected and not logged in yet. */
			case S_CONNECTED:
			case S_LOGGEDIN:
				/* at this point, the player can't have an arena */
				player->arena = NULL;

				/* check if the player's arena is ready.
				 * LOCK: we don't grab the arena status lock because it
				 * doesn't matter if we miss it this time around */
				if (player->newarena && player->newarena->status == ARENA_RUNNING)
				{
					player->arena = player->newarena;
					player->newarena = NULL;
					player->status = S_DO_FREQ_AND_ARENA_SYNC;
				}

				/* check whenloggedin. this is used to move players to
				 * the leaving_zone status once various things are
				 * completed. */
				if (player->whenloggedin)
				{
					player->status = player->whenloggedin;
					player->whenloggedin = 0;
				}

				continue;

			/* these states automatically transition to another one. set
			 * the new status first, then take the appropriate action
			 * below. */
			case S_NEED_AUTH:           ns = S_WAIT_AUTH;           break;
			case S_NEED_GLOBAL_SYNC:    ns = S_WAIT_GLOBAL_SYNC1;   break;
			case S_DO_GLOBAL_CALLBACKS: ns = S_SEND_LOGIN_RESPONSE; break;
			case S_SEND_LOGIN_RESPONSE: ns = S_LOGGEDIN;            break;
			case S_DO_FREQ_AND_ARENA_SYNC: ns = S_WAIT_ARENA_SYNC1; break;
			case S_ARENA_RESP_AND_CBS:  ns = S_PLAYING;             break;
			case S_LEAVING_ARENA:       ns = S_DO_ARENA_SYNC2;      break;
			case S_DO_ARENA_SYNC2:      ns = S_WAIT_ARENA_SYNC2;    break;
			case S_LEAVING_ZONE:        ns = S_WAIT_GLOBAL_SYNC2;   break;

			/* catch any other state */
			default:
				lm->Log(L_ERROR,"<core> [pid=%d] internal error: unknown player status %d",
						player->pid, oldstatus);
				continue;
		}

		player->status = ns; /* set it */

		/* add this player to the pending actions list, to be run when
		 * we release the status lock. */
		action = amalloc(sizeof(*action));
		action->player = player;
		action->oldstatus = oldstatus;
		LLAdd(&actions, action);
	}
	pd->WriteUnlock();

	for (link = LLGetHead(&actions); link; link = link->next)
	{
		Player *player = ((struct action_t*)(link->data))->player;
		pdata *d = PPDATA(player, pdkey);
		int oldstatus = ((struct action_t*)(link->data))->oldstatus;

		switch (oldstatus)
		{
			case S_NEED_AUTH:
				{
					Iauth *auth = mm->GetInterface(I_AUTH, ALLARENAS);

					if (auth && d->loginpkt != NULL && d->lplen > 0)
					{
						lm->Log(L_DRIVEL, "<core> authenticating with '%s'", auth->head.name);
						auth->Authenticate(player, d->loginpkt, d->lplen, AuthDone);
					}
					else
					{
						lm->Log(L_WARN, "<core> can't authenticate player!");
						pd->KickPlayer(player);
					}

					afree(d->loginpkt);
					d->loginpkt = NULL;
					d->lplen = 0;
					mm->ReleaseInterface(auth);
				}
				break;

			case S_NEED_GLOBAL_SYNC:
				if (persist)
					persist->GetPlayer(player, NULL, player_sync_done);
				else
					player_sync_done(player);
				d->hasdonegsync = TRUE;
				break;

			case S_DO_GLOBAL_CALLBACKS:
				DO_CBS(CB_PLAYERACTION,
				       ALLARENAS,
				       PlayerActionFunc,
				       (player, PA_CONNECT, NULL));
				d->hasdonegcallbacks = TRUE;
				break;

			case S_SEND_LOGIN_RESPONSE:
				SendLoginResponse(player);
				lm->Log(L_INFO, "<core> [%s] [pid=%d] player logged in from ip=%s macid=%i",
						player->name, player->pid, player->ipaddr, player->macid);
				break;

			case S_DO_FREQ_AND_ARENA_SYNC:
				/* the arena will be fully loaded here */
				requested_ship = player->p_ship;
				player->p_ship = -1;
				player->p_freq = -1;
				/* first, do pre-callbacks */
				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (player, PA_PREENTERARENA, player->arena));
				/* then, get a freq */
				if (player->p_ship == -1 || player->p_freq == -1)
				{
					Ifreqman *fm = mm->GetInterface(I_FREQMAN, player->arena);
					int freq = 0;

					/* if this arena has a manager, use it */
					if (fm)
					{
						fm->Initial(player, &requested_ship, &freq);
						mm->ReleaseInterface(fm);
					}

					/* set the results back */
					player->p_ship = requested_ship;
					player->p_freq = freq;
				}
				/* then, sync scores */
				if (persist)
					persist->GetPlayer(player, player->arena, player_sync_done);
				else
					player_sync_done(player);
				d->hasdoneasync = TRUE;
				break;

			case S_ARENA_RESP_AND_CBS:
				if (stats)
				{
					/* try to get scores in pdata packet */
					player->pkt.killpoints =
						stats->GetStat(player, STAT_KILL_POINTS, INTERVAL_RESET);
					player->pkt.flagpoints =
						stats->GetStat(player, STAT_FLAG_POINTS, INTERVAL_RESET);
					player->pkt.wins =
						stats->GetStat(player, STAT_KILLS, INTERVAL_RESET);
					player->pkt.losses =
						stats->GetStat(player, STAT_DEATHS, INTERVAL_RESET);
					/* also get other player's scores into their pdatas */
					stats->SendUpdates(player);
				}
				aman->SendArenaResponse(player);
				player->flags.sent_ppk = 0;
				player->flags.sent_wpn = 0;

				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (player, PA_ENTERARENA, player->arena));
				break;

			case S_LEAVING_ARENA:
				DO_CBS(CB_PLAYERACTION,
				       player->arena,
				       PlayerActionFunc,
				       (player, PA_LEAVEARENA, player->arena));
				break;

			case S_DO_ARENA_SYNC2:
				if (persist && d->hasdoneasync)
					persist->PutPlayer(player, player->arena, player_sync_done);
				else
					player_sync_done(player);
				d->hasdoneasync = FALSE;
				break;

			case S_LEAVING_ZONE:
				if (d->hasdonegcallbacks)
					DO_CBS(CB_PLAYERACTION, ALLARENAS, PlayerActionFunc,
							(player, PA_DISCONNECT, NULL));
				if (persist && d->hasdonegsync)
					persist->PutPlayer(player, NULL, player_sync_done);
				else
					player_sync_done(player);
				d->hasdonegsync = FALSE;
				break;
		}
	}

	/* clean up pending action list */
	LLEnum(&actions, afree);
	LLEmpty(&actions);

	return TRUE;
}


void fail_login_with(Player *p, int authcode, const char *text, const char *logmsg)
{
	AuthData auth;

	if (p->type == T_CONT && text)
	{
		auth.code = AUTH_CUSTOMTEXT;
		strncpy(auth.customtext, text, sizeof(auth.customtext));
	}
	else
		auth.code = authcode;

	pd->WriteLock();
	p->status = S_WAIT_AUTH;
	pd->WriteUnlock();

	AuthDone(p, &auth);

	lm->Log(L_DRIVEL, "<core> [pid=%d] login request denied: %s", p->pid, logmsg);
}


void PLogin(Player *p, byte *opkt, int l)
{
	pdata *d = PPDATA(p, pdkey);

	if (!IS_STANDARD(p))
		lm->Log(L_MALICIOUS, "<core> [pid=%d] login packet from wrong client type (%d)",
				p->pid, p->type);
#ifdef CFG_RELAX_LENGTH_CHECKS
	else if ( (p->type == T_VIE && l < LEN_LOGINPACKET_VIE) ||
	          (p->type == T_CONT && l < LEN_LOGINPACKET_CONT) )
#else
	else if ( (p->type == T_VIE && l != LEN_LOGINPACKET_VIE) ||
	          (p->type == T_CONT && l != LEN_LOGINPACKET_CONT) )
#endif
		lm->Log(L_MALICIOUS, "<core> [pid=%d] bad login packet length (%d)", p->pid, l);
	else if (p->status != S_CONNECTED)
		lm->Log(L_MALICIOUS, "<core> [pid=%d] login request from wrong stage: %d", p->pid, p->status);
	else
	{
		struct LoginPacket *pkt = (struct LoginPacket*)opkt, *lp;

#ifndef CFG_RELAX_LENGTH_CHECKS
		/* VIE clients can only have one version. Continuum clients will
		 * need to ask for an update. */
		if (p->type == T_VIE && pkt->cversion != CVERSION_VIE)
		{
			fail_login_with(p, AUTH_LOCKEDOUT, NULL, "bad VIE client version");
			return;
		}
#endif

		/* copy into storage for use by authenticator */
		if (l > 512) l = 512;
		lp = d->loginpkt = amalloc(l);
		memcpy(lp, pkt, l);
		d->lplen = l;

		/* name must be nul-terminated, also set name length limit at 19
		 * characters. */
		lp->name[19] = '\0';

		/* only allow printable characters in names, excluding colon.
		 * while we're at it, remove leading, trailing, and series of
		 * spaces */
		{
			unsigned char c, cc = ' ', *s = (unsigned char *)lp->name, *l = (unsigned char *)lp->name;

			while ((c = *s++))
				if (c >= 32 && c <= 126 && c != ':')
				{
					if (c == ' ' && cc == ' ')
						continue;

					*l++ = cc = c;
				}

			*l = '\0';
		}

		/* if nothing could be salvaged from their name, disconnect them */
		if (strlen(lp->name) == 0)
		{
			fail_login_with(p, AUTH_BADNAME, "Your player name contains no valid characters",
					"all invalid chars");
			return;
		}

		/* must start with number or letter */
		if (!isalnum(*lp->name))
		{
			fail_login_with(p, AUTH_BADNAME,
					"Your player name must start with a letter or number",
					"name doesn't start with alphanumeric");
			return;
		}

		/* pass must be nul-terminated */
		lp->password[31] = '\0';

		/* fill misc data */
		p->macid = pkt->macid;
		p->permid = pkt->D2;
		if (p->type == T_VIE)
			snprintf(p->clientname, sizeof(p->clientname),
					"<ss/vie client, v. %d>", pkt->cversion);
		else if (p->type == T_CONT)
			snprintf(p->clientname, sizeof(p->clientname),
					"<continuum, v. %d>", pkt->cversion);

		/* set up status */
		pd->WriteLock();
		p->status = S_NEED_AUTH;
		pd->WriteUnlock();
		lm->Log(L_DRIVEL, "<core> [pid=%d] login request: '%s'", p->pid, lp->name);
	}
}


void MLogin(Player *p, const char *line)
{
	pdata *d = PPDATA(p, pdkey);
	const char *t;
	char vers[64];
	struct LoginPacket *lp;
	int c, l;

	if (p->status != S_CONNECTED)
	{
		lm->Log(L_MALICIOUS, "<core> [pid=%d] login request from wrong stage: %d", p->pid, p->status);
		return;
	}

	lp = d->loginpkt = amalloc(LEN_LOGINPACKET_VIE);
	d->lplen = LEN_LOGINPACKET_VIE;

	/* extract fields */
	t = delimcpy(vers, line, sizeof(vers), ':');
	if (!t) return;
	lp->cversion = atoi(vers);
	if (strchr(vers, ';') != NULL)
		snprintf(p->clientname, sizeof(p->clientname),
				"chat: %s", strchr(vers, ';') + 1);
	t = delimcpy(lp->name, t, sizeof(lp->name), ':');
	/* replace nonprintables with underscores */
	l = strlen(lp->name);
	for (c = 0; c < l; c++)
		if (lp->name[c] < 32 || lp->name[c] > 126)
			lp->name[c] = '_';
	/* must start with number, letter, or underscore */
	if (!isalnum(lp->name[0]))
		lp->name[0] = '_';
	if (!t) return;
	astrncpy(lp->password, t, sizeof(lp->password));

	p->macid = p->permid = 101;
	/* set up status */
	pd->WriteLock();
	p->status = S_NEED_AUTH;
	pd->WriteUnlock();
	lm->Log(L_DRIVEL, "<core> [pid=%d] login request: '%s'", p->pid, lp->name);
}


void AuthDone(Player *p, AuthData *auth)
{
	pdata *d = PPDATA(p, pdkey);

	if (p->status != S_WAIT_AUTH)
	{
		lm->Log(L_WARN, "<core> [pid=%d] AuthDone called from wrong stage: %d",
				p->pid, p->status);
		return;
	}

	/* copy the authdata */
	d->authdata = amalloc(sizeof(AuthData));
	memcpy(d->authdata, auth, sizeof(AuthData));

	p->flags.authenticated = auth->authenticated;

	if (AUTH_IS_OK(auth->code))
	{
		/* login suceeded */

		/* try to locate existing player with the same name */
		Player *oldp = pd->FindPlayer(auth->name);

		/* set new player's name */
		strncpy(p->pkt.name, auth->sendname, 20);
		astrncpy(p->name, auth->name, 21);
		strncpy(p->pkt.squad, auth->squad, 20);
		astrncpy(p->squad, auth->squad, 21);

		/* make sure we don't have two identical players. if so, do not
		 * increment stage yet. we'll do it when the other player
		 * leaves. */
		if (oldp != NULL && oldp != p)
		{
			pdata *oldd = PPDATA(oldp, pdkey);
			lm->Log(L_DRIVEL,"<core> [%s] player already on, kicking him off "
					"(pid %d replacing %d)",
					auth->name, p->pid, oldp->pid);
			oldd->replaced_by = p;
			pd->KickPlayer(oldp);
		}
		else
		{
			/* increment stage */
			pd->WriteLock();
			p->status = S_NEED_GLOBAL_SYNC;
			pd->WriteUnlock();
		}
	}
	else
	{
		/* if the login didn't succeed status should go to S_CONNECTED
		 * instead of moving forward, and send the login response now,
		 * since we won't do it later. */
		SendLoginResponse(p);
		pd->WriteLock();
		p->status = S_CONNECTED;
		pd->WriteUnlock();
	}
}


void player_sync_done(Player *p)
{
	pd->WriteLock();
	if (p->status == S_WAIT_ARENA_SYNC1)
	{
		if (!p->flags.leave_arena_when_done_waiting)
			p->status = S_ARENA_RESP_AND_CBS;
		else
			p->status = S_DO_ARENA_SYNC2;
	}
	else if (p->status == S_WAIT_ARENA_SYNC2)
		p->status = S_LOGGEDIN;
	else if (p->status == S_WAIT_GLOBAL_SYNC1)
		p->status = S_DO_GLOBAL_CALLBACKS;
	else if (p->status == S_WAIT_GLOBAL_SYNC2)
	{
		pdata *d = PPDATA(p, pdkey);
		Player *replaced_by = d->replaced_by;
		if (replaced_by)
		{
			if (replaced_by->status != S_WAIT_AUTH)
			{
				lm->Log(L_WARN, "<core> [oldpid=%d] [newpid=%d] "
						"unexpected status when replacing players: %d",
						p->pid, replaced_by->pid, replaced_by->status);
			}
			else
			{
				replaced_by->status = S_NEED_GLOBAL_SYNC;
				d->replaced_by = NULL;
			}
		}

		p->status = S_TIMEWAIT;
	}
	else
		lm->Log(L_WARN, "<core> [pid=%d] player_sync_done called from wrong status: %d",
				p->pid, p->status);
	pd->WriteUnlock();
}


local const char *get_auth_code_msg(int code)
{
	switch (code)
	{
		case AUTH_OK: return "ok";
		case AUTH_NEWNAME: return "new user";
		case AUTH_BADPASSWORD: return "incorrect password";
		case AUTH_ARENAFULL: return "arena full";
		case AUTH_LOCKEDOUT: return "you have been locked out";
		case AUTH_NOPERMISSION: return "no permission";
		case AUTH_SPECONLY: return "you can spec only";
		case AUTH_TOOMANYPOINTS: return "you have too many points";
		case AUTH_TOOSLOW: return "too slow (?)";
		case AUTH_NOPERMISSION2: return "no permission (2)";
		case AUTH_NONEWCONN: return "the server is not accepting new connections";
		case AUTH_BADNAME: return "bad player name";
		case AUTH_OFFENSIVENAME: return "offensive player name";
		case AUTH_NOSCORES: return "the server is not recordng scores";
		case AUTH_SERVERBUSY: return "the server is busy";
		case AUTH_TOOLOWUSAGE: return "too low usage";
		case AUTH_ASKDEMOGRAPHICS: return "need demographics";
		case AUTH_TOOMANYDEMO: return "too many demo players";
		case AUTH_NODEMO: return "no demo players allowed";
		default: return "???";
	}
}


void SendLoginResponse(Player *p)
{
	pdata *d = PPDATA(p, pdkey);
	AuthData *auth = d->authdata;

	if (!auth)
	{
		lm->Log(L_ERROR, "<core> missing authdata for pid %d", p->pid);
		pd->KickPlayer(p);
	}
	else if (IS_STANDARD(p))
	{
		struct S2CLoginResponse lr =
			{ S2C_LOGINRESPONSE, 0, 134, 0, {0, 0, 0},
				0, {0, 0, 0, 0, 0}, 0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0} };
		lr.code = auth->code;
		lr.demodata = auth->demodata;
		lr.newschecksum = map->GetNewsChecksum();

		if (p->type == T_CONT)
		{
#pragma pack(push, 1)
			struct {
				u8 type;
				u16 contversion;
				u32 checksum;
			} pkt = { S2C_CONTVERSION, CVERSION_CONT, contchecksum };
#pragma pack(pop)
			net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);

			lr.exechecksum = contchecksum;
			lr.codechecksum = codechecksum;
		}
		else
		{
			/* old vie exe checksums */
			lr.exechecksum = 0xF1429CE8;
			lr.codechecksum = 0x281CC948;
		}

		if (capman && capman->HasCapability(p, CAP_SEEPRIVFREQ))
		{
			/* to make the client think it's a mod, set these checksums to -1 */
			lr.exechecksum = -1;
			lr.codechecksum = -1;
		}

		if (lr.code == AUTH_CUSTOMTEXT)
		{
			if (p->type == T_CONT)
			{
				/* send custom rejection text */
				char custom[256];
				custom[0] = S2C_LOGINTEXT;
				astrncpy(custom+1, auth->customtext, 255);
				net->SendToOne(p, (byte *)custom, strlen(custom+1) + 2, NET_RELIABLE);
			}
			else /* vie doesn't understand that packet */
				lr.code = AUTH_LOCKEDOUT;
		}

		net->SendToOne(p, (unsigned char*)&lr, sizeof(lr), NET_RELIABLE);
	}
	else if (IS_CHAT(p))
	{
		if (AUTH_IS_OK(auth->code))
			chatnet->SendToOne(p, "LOGINOK:%s", p->name);
		else if (auth->code == AUTH_CUSTOMTEXT)
			chatnet->SendToOne(p, "LOGINBAD:%s", auth->customtext);
		else
			chatnet->SendToOne(p, "LOGINBAD:%s", get_auth_code_msg(auth->code));
	}

	afree(auth);
	d->authdata = NULL;
}


void DefaultAuth(Player *p, struct LoginPacket *pkt, int lplen,
		void (*Done)(Player *p, AuthData *auth))
{
	AuthData auth;

	memset(&auth, 0, sizeof(auth));
	auth.demodata = 0;
	auth.code = AUTH_OK;
	auth.authenticated = FALSE;
	astrncpy(auth.name, pkt->name, 24);
	strncpy(auth.sendname, pkt->name, 20);
	memset(auth.squad, 0, sizeof(auth.squad));

	Done(p, &auth);
}


int SendKeepalive(void *q)
{
	byte keepalive = S2C_KEEPALIVE;
	if (net)
		net->SendToArena(ALLARENAS, NULL, &keepalive, 1, NET_RELIABLE);
	return 1;
}


