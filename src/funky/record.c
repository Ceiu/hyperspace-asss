
/* dist: public */

#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef WIN32
#include <unistd.h>
#else
#include <io.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "zlib.h"

#include "asss.h"
#include "fake.h"
#include "clientset.h"

#pragma pack(1)

/* recorded game file format */
enum
{
	EV_NULL,
	EV_ENTER,
	EV_LEAVE,
	EV_SHIPCHANGE,
	EV_FREQCHANGE,
	EV_KILL,
	EV_CHAT,
	EV_POS,
	EV_PACKET,
};

struct event_header
{
	u32 tm;
	i16 type;
};

struct event_enter
{
	struct event_header head;
	i16 pid;
	char name[24], squad[24];
	u16 ship, freq;
};

struct event_leave
{
	struct event_header head;
	i16 pid;
};

struct event_sc
{
	struct event_header head;
	i16 pid;
	i16 newship, newfreq;
};

struct event_fc
{
	struct event_header head;
	i16 pid;
	i16 newfreq;
};

struct event_kill
{
	struct event_header head;
	i16 killer, killed, pts, flags;
};

struct event_chat
{
	struct event_header head;
	i16 pid;
	u8 type, sound;
	u16 len;
	char msg[1];
};

struct event_pos
{
	struct event_header head;
	/* special: the type byte holds the length of the rest of the
	 * packet, since the last two fields are optional. the time field
	 * holds the pid. */
	struct C2SPosition pos;
};

struct event_packet
{
	struct event_header head;
	i16 len;
	byte data[0];
};


#define FILE_VERSION 2

struct file_header
{
	char header[8];        /* always "asssgame" */
	u32 version;           /* to tell if the file is compatible */
	u32 offset;            /* offset of start of events from beginning of the file */
	u32 events;            /* number of events in the file */
	u32 endtime;           /* ending time of recorded game */
	u32 maxpid;            /* the highest numbered pid in the file */
	u32 specfreq;          /* the spec freq at the time the game was recorded */
	time_t recorded;       /* the date and time this game was recorded */
	u32 mapchecksum;       /* a checksum for the map this was recorded on */
	char recorder[24];     /* the name of the player who recorded it */
	char arenaname[24];    /* the name of the arena that was recorded */
};


/* start of module code */

typedef struct rec_adata
{
	enum { s_none, s_recording, s_playing } state;
	gzFile gzf;
	const char *fname;
	u32 events, maxpid;
	ticks_t started, total;
	int specfreq;
	int ispaused;
	double curpos;
	MPQueue mpq;
	pthread_t thd;
} rec_adata;


local Imodman *mm;
local Iarenaman *aman;
local Iplayerdata *pd;
local Icmdman *cmd;
local Igame *game;
local Ifake *fake;
local Ilogman *lm;
local Ichat *chat;
local Inet *net;
local Iconfig *cfg;
local Iflagcore *flagcore;
local Iballs *balls;
local Imapdata *mapdata;
local Iclientset *clientset;

local int adkey;
local pthread_mutex_t recmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK(a) pthread_mutex_lock(&recmtx)
#define UNLOCK(a) pthread_mutex_unlock(&recmtx)
#define TRYLOCK(a) (pthread_mutex_trylock(&recmtx) == 0)


/********** game recording **********/

local inline int get_size(struct event_header *ev)
{
	switch (ev->type)
	{
		case EV_ENTER:          return sizeof(struct event_enter);
		case EV_LEAVE:          return sizeof(struct event_leave);
		case EV_SHIPCHANGE:     return sizeof(struct event_sc);
		case EV_FREQCHANGE:     return sizeof(struct event_fc);
		case EV_KILL:           return sizeof(struct event_kill);
		case EV_CHAT:           return ((struct event_chat *)ev)->len +
		                            offsetof(struct event_chat, msg);
		case EV_POS:            return ((struct event_pos *)ev)->pos.type +
		                            offsetof(struct event_pos, pos);
		case EV_PACKET:         return abs(((struct event_packet *)ev)->len) +
		                            offsetof(struct event_packet, len);
		default:                return 0;
	}
}

local inline int get_event_pid(struct event_header *ev)
{
	int pid1, pid2;
	switch (ev->type)
	{
		case EV_ENTER:          return ((struct event_enter*)ev)->pid;
		case EV_LEAVE:          return ((struct event_leave*)ev)->pid;
		case EV_SHIPCHANGE:     return ((struct event_sc*)ev)->pid;
		case EV_FREQCHANGE:     return ((struct event_fc*)ev)->pid;
		case EV_CHAT:           return ((struct event_chat *)ev)->pid;
		case EV_POS:            return ((struct event_pos *)ev)->pos.time;
		case EV_KILL:
			pid1 = ((struct event_kill*)ev)->killer;
			pid2 = ((struct event_kill*)ev)->killed;
			return (pid1 > pid2) ? pid1 : pid2;
		case EV_PACKET:         return 0;
		default:                return 0;
	}
}


/* callbacks that pass events to writing thread */

local void cb_paction(Player *p, int action, Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);

	if (action == PA_ENTERARENA)
	{
		struct event_enter *ev = amalloc(sizeof(*ev));
		ev->head.tm = current_ticks();
		ev->head.type = EV_ENTER;
		ev->pid = p->pid;
		astrncpy(ev->name, p->name, sizeof(ev->name));
		astrncpy(ev->squad, p->squad, sizeof(ev->squad));
		ev->ship = p->p_ship;
		ev->freq = p->p_freq;
		MPAdd(&ra->mpq, ev);
	}
	else if (action == PA_LEAVEARENA)
	{
		struct event_leave *ev = amalloc(sizeof(*ev));
		ev->head.tm = current_ticks();
		ev->head.type = EV_LEAVE;
		ev->pid = p->pid;
		MPAdd(&ra->mpq, ev);
	}
}


local void cb_shipchange(Player *p, int ship, int freq)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);
	struct event_sc *ev = amalloc(sizeof(*ev));

	ev->head.tm = current_ticks();
	ev->head.type = EV_SHIPCHANGE;
	ev->pid = p->pid;
	ev->newship = ship;
	ev->newfreq = freq;
	MPAdd(&ra->mpq, ev);
}


local void cb_freqchange(Player *p, int freq)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);
	struct event_fc *ev = amalloc(sizeof(*ev));

	ev->head.tm = current_ticks();
	ev->head.type = EV_FREQCHANGE;
	ev->pid = p->pid;
	ev->newfreq = freq;
	MPAdd(&ra->mpq, ev);
}

local void cb_shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	if (newship == oldship)
	{
		cb_freqchange(p, newfreq);
	}
	else
	{
		cb_shipchange(p, newship, newfreq);
	}
}

local void cb_kill(Arena *a, Player *killer, Player *killed,
		int bty, int flags, int *pts, int *green)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_kill *ev = amalloc(sizeof(*ev));

	ev->head.tm = current_ticks();
	ev->head.type = EV_KILL;
	ev->killer = killer->pid;
	ev->killed = killed->pid;
	ev->pts = *pts; /* FIXME: this is only accurate if this is the last callback to get called */
	ev->flags = flags;
	MPAdd(&ra->mpq, ev);
}


local void cb_chat(Player *p, int type, int sound, Player *target, int freq, const char *txt)
{
	rec_adata *ra = P_ARENA_DATA(p->arena, adkey);

	if (type == MSG_ARENA || type == MSG_PUB || (type == MSG_FREQ && freq == ra->specfreq))
	{
		int len = strlen(txt) + 1;
		struct event_chat *ev = amalloc(sizeof(*ev) + len);

		ev->head.tm = current_ticks();
		ev->head.type = EV_CHAT;
		ev->pid = p ? p->pid : -1;
		ev->type = type;
		ev->sound = sound;
		ev->len = len;
		memcpy(ev->msg, txt, len);
		MPAdd(&ra->mpq, ev);
	}
}


local inline int check_arena(Arena *a, rec_adata *ra)
{
	if (!a)
		return FALSE;
	/* the idea here is to not hold up the ppk thread if we can't get
	 * the mutex immediately. in that case, just say that we're not
	 * recording. position packets are unreliable, so this is ok. in
	 * general, this mutex isn't held often, but when it is it might be
	 * held for a while. */
	if (TRYLOCK(a))
	{
		int ok = ra->state == s_recording;
		UNLOCK(a);
		return ok;
	}
	else
		return FALSE;
}

local void ppk(Player *p, byte *pkt, int len)
{
	Arena *a = p->arena;
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_pos *ev;

	if (!check_arena(a, ra)) return;

	ev = amalloc(len + offsetof(struct event_pos, pos));
	ev->head.tm = current_ticks();
	ev->head.type = EV_POS;
	memcpy(&ev->pos, pkt, len);
	ev->pos.type = len;
	ev->pos.time = p->pid;
	MPAdd(&ra->mpq, ev);
}


/*local void arenapkt(Arena *a, byte *pkt, int n, int flags)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_packet *ev;

	switch (*pkt)
	{
		case S2C_TURRET:
		case S2C_SETTINGS:
		case S2C_FLAGLOC:
		case S2C_FLAGPICKUP:
		case S2C_FLAGRESET:
		case S2C_FLAGDROP:
		case S2C_TURFFLAGS:
		case S2C_BRICK:
		case S2C_BALL:
			break;
		default:
			return;
	}

	if (n > 4000)
		return;

	ev = amalloc(sizeof(*ev) + n);
	ev->head.tm = current_ticks();
	ev->head.type = EV_PACKET;
	ev->len = (flags & NET_RELIABLE) ? -n : n;
	memcpy(ev->data, pkt, n);
	MPAdd(&ra->mpq, ev);
}*/


/* writer thread */

local int stop_recording(Arena *a, int suicide);

local void *recorder_thread(void *v)
{
	Arena *a = v;
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	struct event_header *ev;
	int pid;

	assert(ra->gzf);

	while ((ev = MPRemove(&ra->mpq)))
	{
		int len = get_size(ev);
		/* normalize events to start from 0 */
		ev->tm = TICK_DIFF(ev->tm, ra->started);
		if (gzwrite(ra->gzf, ev, len) == len)
		{
			pid = get_event_pid(ev);
			if (pid > ra->maxpid)
				ra->maxpid = pid;
			afree(ev);
			ra->events++;
			continue;
		}
		else
		{
			afree(ev);
			lm->LogA(L_ERROR, "record", a, "write to game file failed. "
					"stopping recorder. out of disk space?");
			stop_recording(a, TRUE);
			return NULL;
		}
	}

	return NULL;
}


/* starting and stopping functions and helpers */

local void write_current_players(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	Link *link;
	Player *p;

	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->arena == a && p->status == S_PLAYING)
		{
			struct event_enter ev;
			ev.head.tm = 0;
			ev.head.type = EV_ENTER;
			ev.pid = p->pid;
			if (p->pid > ra->maxpid)
				ra->maxpid = p->pid;
			astrncpy(ev.name, p->name, sizeof(ev.name));
			astrncpy(ev.squad, p->squad, sizeof(ev.squad));
			ev.ship = p->p_ship;
			ev.freq = p->p_freq;
			gzwrite(ra->gzf, (byte*)&ev, sizeof(ev));
		}
	pd->Unlock();
}


local int start_recording(Arena *a, const char *file, const char *recorder, const char *comments)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE, fd;
	int cmtlen = comments ? strlen(comments) + 1 : 0;

	char fullpath[256];

	mkdir("recordings", 0755);

	/* append file to fullpath if the base is not recordings/
		else set fullpath to file (for backwards compatibility with ?rec play recordings/blah ) */
	if (!strncmp("recordings/", file, sizeof("recordings/")-1))
	{
		astrncpy(fullpath, file, sizeof(fullpath));
	}
	else
	{
		snprintf(fullpath, sizeof(fullpath), "recordings/%s", file);
	}

	LOCK(a);
	if (ra->state == s_none)
	{
		fd = open(fullpath, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
		if (fd != -1)
		{
			/* leave the header wrong until we finish it properly in
			 * stop_recording */
			struct file_header header = { "ass$game" };

			/* fill in file header */
			header.version = FILE_VERSION;
			header.offset = sizeof(struct file_header) + cmtlen;
			/* we don't know these next 3 yet */
			header.events = 0;
			header.endtime = 0;
			header.maxpid = 0;
			header.specfreq = a->specfreq;
			header.mapchecksum = mapdata->GetChecksum(a, MODMAN_MAGIC);
			time(&header.recorded);
			astrncpy(header.recorder, recorder, sizeof(header.recorder));
			astrncpy(header.arenaname, a->name, sizeof(header.arenaname));

			/* write headers to the file uncompressed, then convert the
			 * fd to a zlib file */
			if (write(fd, &header, sizeof(header)) == sizeof(header) &&
			    (!cmtlen || write(fd, comments, cmtlen) == cmtlen) &&
			    (ra->gzf = gzdopen(fd, "wb9f")) != NULL)
			{
				/* generate fake enter events for the current players in
				 * this arena */
				ra->maxpid = 0;
				write_current_players(a);

				ra->specfreq = header.specfreq;

				MPInit(&ra->mpq);

				mm->RegCallback(CB_PLAYERACTION, cb_paction, a);
				mm->RegCallback(CB_SHIPFREQCHANGE, cb_shipfreqchange, a);
				mm->RegCallback(CB_KILL, cb_kill, a);
				mm->RegCallback(CB_CHATMSG, cb_chat, a);
				/* net->SetArenaPacketHook(a, arenapkt); */

				/* force settings packet update */
				/* FIXME: clientset->Reconfigure(a); */

				ra->started = current_ticks();
				ra->fname = astrdup(file);

				chat->SendArenaMessage(a, "Starting game recording");

				ra->state = s_recording;

				pthread_create(&ra->thd, NULL, recorder_thread, a);

				ok = TRUE;
			}
			else
				lm->LogA(L_WARN, "record", a, "gzdopen failed");

			if (!ok)
				close(fd);
		}
		else
			lm->LogA(L_INFO, "record", a, "can't open '%s' for writing", fullpath);
	}
	else
		lm->LogA(L_INFO, "record", a, "tried to %s game, but state wasn't none",
				"record");
	UNLOCK(a);
	return ok;
}


local int stop_recording(Arena *a, int suicide)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE, fd;

	LOCK(a);
	if (ra->state == s_recording)
	{
		u32 fields[3];

		ra->state = s_none;

		if (!suicide)
		{
			MPAdd(&ra->mpq, NULL);
			pthread_join(ra->thd, NULL);
		}
		else
			pthread_detach(ra->thd);

		mm->UnregCallback(CB_PLAYERACTION, cb_paction, a);
		mm->UnregCallback(CB_SHIPFREQCHANGE, cb_shipfreqchange, a);
		mm->UnregCallback(CB_KILL, cb_kill, a);
		mm->UnregCallback(CB_CHATMSG, cb_chat, a);
		/* net->SetArenaPacketHook(a, NULL); */

		MPDestroy(&ra->mpq);

		gzclose(ra->gzf);
		ra->gzf = NULL;

		chat->SendArenaMessage(a, "Game recording stopped");

		/* fill in header fields we couldn't get before */
		fd = open(ra->fname, O_WRONLY | O_BINARY);
		if (fd != -1)
		{
			struct stat st;

			fields[0] = ra->events;
			fields[1] = TICK_DIFF(current_ticks(), ra->started);
			fields[2] = ra->maxpid;
			lseek(fd, offsetof(struct file_header, events), SEEK_SET);
			write(fd, fields, sizeof(fields));
			lseek(fd, 0, SEEK_SET);
			write(fd, "asssgame", 8);

			/* ugly overloading of a field */
			ra->events = fstat(fd, &st) ? 0 : st.st_size;

			close(fd);
		}
		else
			lm->LogA(L_WARN, "record", a, "can't finalize recorded game file '%s'",
					ra->fname);

		afree(ra->fname);
		ra->fname = NULL;

		ok = TRUE;
	}
	UNLOCK(a);
	return ok;
}


/********** game playback functions **********/

local void get_watching_set(LinkedList *set, Arena *arena)
{
	Link *link;
	Player *p;
	pd->Lock();
	FOR_EACH_PLAYER(p)
		if (p->status == S_PLAYING &&
		    p->arena == arena &&
		    p->type != T_FAKE)
			LLAdd(set, p);
	pd->Unlock();
}

/* locking humans to spec */

local shipmask_t GetAllowableShips(Player *p, int freq, char *err_buf, int buf_len)
{
	if (err_buf)
		snprintf(err_buf, buf_len, "Ships are disabled for playback.");
	return 0;
}

local int CanChangeFreq(Player *p, int new_freq, char *err_buf, int buf_len)
{
	if (err_buf)
		snprintf(err_buf, buf_len, "Teams are locked for playback.");
	return 0;
}

local struct Aenforcer lockspec =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
	NULL,
	NULL,
	NULL,
	CanChangeFreq,
	GetAllowableShips
};


local void lock_all_spec(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	LinkedList set = LL_INITIALIZER;
	Link *l;

	mm->RegAdviser(&lockspec, a);

	get_watching_set(&set, a);
	for (l = LLGetHead(&set); l; l = l->next)
		game->SetShipAndFreq(l->data, SHIP_SPEC, ra->specfreq);
	LLEmpty(&set);
}

local void unlock_all_spec(Arena *a)
{
	mm->UnregAdviser(&lockspec, a);
}


/* helpers for playback thread */

#include "packets/flags.h"
#include "packets/balls.h"
#include "packets/brick.h"

local int process_packet_event(Arena *a, struct event_packet *ev, int rel, Player **pidmap, int pidmaplen)
{
	int flags = rel ? NET_RELIABLE : NET_UNRELIABLE;

#define CVT_PID(f) do { \
		if ((f) < 0 || (f) >= pidmaplen) return FALSE; \
		if (pidmap[(f)] == NULL) return FALSE; \
		(f) = pidmap[(f)]->pid; \
	} while (0)

	switch (ev->data[0])
	{
		case S2C_TURRET:
		{
			struct SimplePacket *pkt = (struct SimplePacket*)ev->data;
			CVT_PID(pkt->d1);
			CVT_PID(pkt->d2);
			break;
		}
		case S2C_SETTINGS:
			break;
		case S2C_FLAGLOC:
			break;
		case S2C_FLAGPICKUP:
		{
			struct S2CFlagPickup *pkt = (struct S2CFlagPickup*)ev->data;
			CVT_PID(pkt->pid);
			break;
		}
		case S2C_FLAGRESET:
			break;
		case S2C_FLAGDROP:
			break;
		case S2C_TURFFLAGS:
			break;
		case S2C_BRICK:
		{
			struct S2CBrickPacket *pkt = (struct S2CBrickPacket*)ev->data;
			pkt->starttime = current_ticks();
			break;
		}
		case S2C_BALL:
		{
			struct BallPacket *pkt = (struct BallPacket*)ev->data;
			CVT_PID(pkt->player);
			pkt->time = current_ticks(); /* FIXME: be more accurate */
			break;
		}
		default:
			lm->LogA(L_WARN, "record", a, "unknown packet type in event %d",
					ev->data[0]);
			return FALSE;
	}

#undef CVT_PID

	net->SendToArena(a, NULL, ev->data, ev->len, flags);
	return TRUE;
}


local inline int check_chat_len(Arena *a, int len)
{
	if (len >= 1 && len < 512)
		return TRUE;
	else
	{
		lm->LogA(L_WARN, "record", a, "bad chat msg length: %d", len);
		return FALSE;
	}
}

local inline int check_pos_len(Arena *a, int len)
{
	if (len == 22 || len == 24 || len == 32)
		return TRUE;
	else
	{
		lm->LogA(L_WARN, "record", a, "bad position length: %d", len);
		return FALSE;
	}
}

local inline int check_pkt_len(Arena *a, int len)
{
	if (len >= 1 || len < 4000)
		return TRUE;
	else
	{
		lm->LogA(L_WARN, "record", a, "bad general packet length: %d", len);
		return FALSE;
	}
}


enum
{
	PC_NULL = 0,
	PC_STOP,
	PC_PAUSE,
	PC_RESUME,
};

/* playback thread */

local void *playback_thread(void *v)
{
	union
	{
		char buf[4096]; /* no event can be larger than this */
		struct event_header head;
		struct event_enter enter;
		struct event_enter leave;
		struct event_sc sc;
		struct event_fc fc;
		struct event_kill kill;
		struct event_chat chat;
		struct event_pos pos;
		struct event_packet pkt;
	} ev;

	Arena *a = v;
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int cmd, r, isrel;
	ticks_t started, now, paused = 0;

	Player **pidmap;
	int pidmaplen;

	/* first lock everyone to spec so they don't interfere with the
	 * playback */
	lock_all_spec(a);

	memset(ev.buf, 0, sizeof(ev));

	pidmaplen = ra->maxpid + 1;
	pidmap = amalloc(pidmaplen * sizeof(Player*));
	memset(pidmap, 0, pidmaplen * sizeof(Player*));

	started = current_ticks();
	ra->ispaused = 0;

	for (;;)
	{
		/* try reading a control command */
		cmd = (long)MPTryRemove(&ra->mpq);
		switch (cmd)
		{
			case PC_NULL:
				break;

			case PC_STOP:
				goto out;

			case PC_PAUSE:
				paused = current_ticks();
				LOCK(a);
				ra->ispaused = TRUE;
				UNLOCK(a);
				chat->SendArenaMessage(a, "Game playback paused");
				break;

			case PC_RESUME:
				LOCK(a);
				ra->ispaused = FALSE;
				UNLOCK(a);
				if (paused)
					started = TICK_MAKE(started + TICK_DIFF(current_ticks(), paused));
				chat->SendArenaMessage(a, "Game playback resumed");
				break;
		}

		/* get an event if we don't have one */
		if (ev.head.type == 0)
		{
			/* read header */
			r = gzread(ra->gzf, &ev, sizeof(struct event_header));
			/* read rest */
			switch (ev.head.type)
			{
#define REST(type) (sizeof(struct type) - sizeof(struct event_header))
				case EV_NULL:
					break;
				case EV_ENTER:
					r = gzread(ra->gzf, &ev.enter.pid, REST(event_enter));
					break;
				case EV_LEAVE:
					r = gzread(ra->gzf, &ev.leave.pid, REST(event_leave));
					break;
				case EV_SHIPCHANGE:
					r = gzread(ra->gzf, &ev.sc.pid, REST(event_sc));
					break;
				case EV_FREQCHANGE:
					r = gzread(ra->gzf, &ev.fc.pid, REST(event_fc));
					break;
				case EV_KILL:
					r = gzread(ra->gzf, &ev.kill.killer, REST(event_kill));
					break;
				case EV_CHAT:
					/* read enough bytes to get len field */
					r = gzread(ra->gzf, &ev.chat.pid, REST(event_chat) - 1);
					if (!check_chat_len(a, ev.chat.len))
						goto out;
					/* now read more for len field */
					r = gzread(ra->gzf, ev.chat.msg, ev.chat.len);
					break;
				case EV_POS:
					r = gzread(ra->gzf, &ev.pos.pos, 1);
					if (!check_pos_len(a, ev.pos.pos.type))
						goto out;
					r = gzread(ra->gzf, ((char*)&ev.pos.pos) + 1, ev.pos.pos.type - 1);
					break;
				case EV_PACKET:
					r = gzread(ra->gzf, &ev.pkt.len, sizeof(ev.pkt.len));
					if (ev.pkt.len < 0)
						isrel = 1, ev.pkt.len = -ev.pkt.len;
					else
						isrel = 0;
					if (!check_pkt_len(a, ev.pkt.len))
						goto out;
					r = gzread(ra->gzf, ev.pkt.data, ev.pkt.len);
					break;
				default:
					lm->LogA(L_WARN, "record", a, "bad event type in game file: %d",
							ev.head.type);
					goto out;
#undef REST
			}
			if (r == 0)
				goto out;
		}

		/* do stuff with current time */
		now = current_ticks();

		if (!ra->ispaused)
			ra->curpos = 100.0*(double)TICK_DIFF(now, started)/(double)ra->total;

		/* only process it if its time has come aready. if not, sleep
		 * for a bit and go for another iteration around the loop. */
		if (!ra->ispaused && TICK_DIFF(now, started) >= ev.head.tm)
		{
			Player *p1, *p2;

			switch (ev.head.type)
			{
#define CHECK(pid) \
	if ((pid) < 0 || (pid) >= pidmaplen) { \
		lm->LogA(L_WARN, "record", a, "bad pid in game file: %d", (pid)); \
		goto out; }

				case EV_NULL:
					break;
				case EV_ENTER:
				{
					char newname[24] = "~";
					CHECK(ev.enter.pid)
					strncat(newname, ev.enter.name, 19);
					p1 = fake->CreateFakePlayer(newname, a, ev.enter.ship, ev.enter.freq);
					if (p1)
						pidmap[ev.enter.pid] = p1;
					else
						lm->LogA(L_WARN, "record", a, "can't create fake player for pid %d",
								ev.enter.pid);
				}
					break;
				case EV_LEAVE:
					CHECK(ev.leave.pid)
					p1 = pidmap[ev.leave.pid];
					if (p1)
						fake->EndFaked(p1);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.leave.pid);
					pidmap[ev.leave.pid] = NULL;
					break;
				case EV_SHIPCHANGE:
					CHECK(ev.sc.pid)
					p1 = pidmap[ev.sc.pid];
					if (p1)
						game->SetShipAndFreq(p1, ev.sc.newship, ev.sc.newfreq);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_FREQCHANGE:
					CHECK(ev.fc.pid)
					p1 = pidmap[ev.fc.pid];
					if (p1)
						game->SetFreq(p1, ev.fc.newfreq);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_KILL:
					CHECK(ev.kill.killer)
					CHECK(ev.kill.killed)
					p1 = pidmap[ev.kill.killer];
					p2 = pidmap[ev.kill.killed];
					if (p1 && p2)
						game->FakeKill(p1, p2, ev.kill.pts, ev.kill.flags);
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_CHAT:
					if (ev.chat.type == MSG_ARENA)
					{
						chat->SendArenaSoundMessage(a, ev.chat.sound, "%s", ev.chat.msg);
					}
					else if (ev.chat.type == MSG_PUB || ev.chat.type == MSG_FREQ)
					{
						CHECK(ev.chat.pid)
						p1 = pidmap[ev.chat.pid];
						if (p1)
						{
							LinkedList set = LL_INITIALIZER;
							get_watching_set(&set, a);
							chat->SendAnyMessage(&set, ev.chat.type,
									ev.chat.sound, p1, "%s",
									ev.chat.msg);
							LLEmpty(&set);
						}
						else
							lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
									ev.sc.pid);
					}
					break;
				case EV_POS:
					CHECK(ev.pos.pos.time)
					p1 = pidmap[ev.pos.pos.time];
					if (p1)
					{
						ev.pos.pos.time = now; /* FIXME: be more accurate */
						game->FakePosition(p1, &ev.pos.pos, ev.pos.pos.type);
					}
					else
						lm->LogA(L_WARN, "record", a, "no mapping for pid %d",
								ev.sc.pid);
					break;
				case EV_PACKET:
					process_packet_event(a, &ev.pkt, isrel, pidmap, pidmaplen);
					break;
#undef CHECK
			}
			ev.head.type = 0; /* signal to read another event */
		}
		else
			usleep(10000);
	}

out:
	/* make sure everyone leaves */
	for (r = 0; r < pidmaplen; r++)
		if (pidmap[r])
			fake->EndFaked(pidmap[r]);
	afree(pidmap);

	/* force settings update since playback may have messed with it */
	/* FIXME: clientset->Reconfigure(a); */
	/* FIXME: if (flags) flags->DisableFlags(a, FALSE); */
	if (balls) balls->SetBallCount(a, 0);

	chat->SendArenaMessage(a, "Game playback stopped");

	unlock_all_spec(a);

	LOCK(a);

	/* nobody should be playing with this except us */
	assert(ra->state == s_playing);
	ra->state = s_none;

	gzclose(ra->gzf);
	ra->gzf = NULL;
	afree(ra->fname);
	ra->fname = NULL;

	UNLOCK(a);

	return NULL;
}


/* starting and stopping playback */

local int start_playback(Arena *a, const char *file)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE, fd;

	char fullpath[256];

	/* append file to fullpath if the base is not recordings/
		else set fullpath to file (for backwards compatibility with ?rec play recordings/blah ) */
	if (!strncmp("recordings/", file, sizeof("recordings/")-1))
	{
		astrncpy(fullpath, file, sizeof(fullpath));
	}
	else
	{
		snprintf(fullpath, sizeof(fullpath), "recordings/%s", file);
	}

	LOCK(a);
	if (ra->state == s_none)
	{
		fd = open(fullpath, O_RDONLY | O_BINARY);
		if (fd != -1)
		{
			struct file_header header;

			if (read(fd, &header, sizeof(header)) != sizeof(header))
				lm->LogA(L_INFO, "record", a, "can't read header");
			else if (strncmp(header.header, "asssgame", 8) != 0)
				lm->LogA(L_INFO, "record", a, "bad header in game file");
			else if (header.version != FILE_VERSION)
				lm->LogA(L_INFO, "record", a, "bad version number in game file");
			else if (header.mapchecksum != mapdata->GetChecksum(a, MODMAN_MAGIC))
				lm->LogA(L_INFO, "record", a, "map checksum mismatch in game file");
			else
			{
				char date[32];

				ra->fname = astrdup(file);
				ra->maxpid = header.maxpid;
				ra->total = header.endtime;
				ra->events = header.events;
				ra->specfreq = header.specfreq;

				/* move to where the data is */
				lseek(fd, header.offset, SEEK_SET);

				/* convert fd to zlib file */
				ra->gzf = gzdopen(fd, "rb");

				if (ra->gzf)
				{
					struct tm _tm;
					/* FIXME: if (flags) flags->DisableFlags(a, TRUE); */
					if (balls) balls->SetBallCount(a, 0);

					/* tell people about the game */
					chat->SendArenaMessage(a, "Starting game playback: %s", file);

					alocaltime_r(&header.recorded, &_tm);
					strftime(date, sizeof(date), "%a %b %d %H:%M:%S %Y", &_tm);
					chat->SendArenaMessage(a, "Game recorded in arena %s by %s on %s",
							header.arenaname, header.recorder, date);

					MPInit(&ra->mpq);

					ra->state = s_playing;

					pthread_create(&ra->thd, NULL, playback_thread, a);
					pthread_detach(ra->thd);

					ok = TRUE;
				}
				else
					lm->LogA(L_WARN, "record", a, "gzdopen failed");
			}

			if (!ok)
				close(fd);
		}
		else
			lm->LogA(L_INFO, "record", a, "can't open game file '%s'", fullpath);
	}
	else
		lm->LogA(L_INFO, "record", a, "tried to %s game, but state wasn't none",
				"play");
	UNLOCK(a);

	return ok;
}


local int stop_playback(Arena *a)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);
	int ok = FALSE;

	LOCK(a);
	if (ra->state == s_playing)
	{
		/* all we can do is tell it to stop */
		MPAdd(&ra->mpq, (void*)PC_STOP);

		ok = TRUE;
	}
	UNLOCK(a);
	return ok;
}


/* the main controlling command */

local helptext_t gamerecord_help =
"Module: record\n"
"Targets: none\n"
"Args: status | record <file> | play <file> | pause | restart | stop\n"
"TODO: write more here.\n";

local void Cgamerecord(const char *tc, const char *params, Player *p, const Target *target)
{
	Arena *a = p->arena;
	rec_adata *ra = P_ARENA_DATA(a, adkey);

	if (strncasecmp(params, "record", 6) == 0)
	{
		const char *fn = params + 6;
		while (*fn && isspace(*fn)) fn++;
		if (*fn)
		{
			if (!start_recording(a, fn, p->name, NULL))
				chat->SendMessage(p, "There was an error %s."
						" Check the server log for details.",
						"recording");
		}
		else
			chat->SendMessage(p, "You must specify a filename to %s.",
					"record to");
	}
	else if (strncasecmp(params, "play", 4) == 0)
	{
		const char *fn = params + 4;
		while (*fn && isspace(*fn)) fn++;
		if (*fn)
		{
			if (!start_playback(a, fn))
				chat->SendMessage(p, "There was an error %s."
						" Check the server log for details.",
						"playing the recorded game");
		}
		else
		{
			int state, isp;
			LOCK(a);
			state = ra->state;
			isp = ra->ispaused;
			UNLOCK(a);
			if (state == s_playing && isp)
				MPAdd(&ra->mpq, (void*)PC_RESUME);
			else
				chat->SendMessage(p, "You must specify a filename to %s.",
						"play from");
		}
	}
	else if (strcasecmp(params, "stop") == 0)
	{
		int state;
		LOCK(a);
		state = ra->state;
		UNLOCK(a);
		if (state == s_none)
			chat->SendMessage(p, "There's nothing being played or recorded here.");
		else if (state == s_playing)
		{
			if (stop_playback(a))
				chat->SendMessage(p, "Stopped playback.");
			else
				chat->SendMessage(p, "There was an error stopping %s.",
						"playback");
		}
		else if (state == s_recording)
		{
			if (stop_recording(a, FALSE))
				chat->SendMessage(p, "Stopped recording. The game file is %u bytes long.",
						ra->events);
			else
				chat->SendMessage(p, "There was an error stopping %s.",
						"recording");
		}
		else
			chat->SendMessage(p, "The recorder module is in an invalid state.");
	}
	else if (strcasecmp(params, "pause") == 0)
	{
		int state, isp;
		LOCK(a);
		state = ra->state;
		isp = ra->ispaused;
		UNLOCK(a);
		if (state == s_playing)
			MPAdd(&ra->mpq, isp ? (void*)PC_RESUME : (void*)PC_PAUSE);
		else
			chat->SendMessage(p, "There is no game being played here.");
	}
	else
	{
		LOCK(a);
		switch (ra->state)
		{
			case s_none:
				chat->SendMessage(p, "No games are being played or recorded.");
				break;
			case s_recording:
				chat->SendMessage(p, "A game is being recorded (to '%s').",
						ra->fname);
				break;
			case s_playing:
				chat->SendMessage(p, "A game is being played (from '%s'), "
						"current pos %.1f%%%s",
						ra->fname,
						ra->curpos,
						ra->ispaused ? ", paused" : "");
				break;
			default:
				chat->SendMessage(p, "The recorder module is in an invalid state.");
		}
		UNLOCK(a);
	}
}


local void cb_aaction(Arena *a, int action)
{
	rec_adata *ra = P_ARENA_DATA(a, adkey);

	if (action == AA_CREATE)
	{
		ra->state = s_none;
	}
	else if (action == AA_DESTROY)
	{
		if (ra->state == s_recording)
			stop_recording(a, FALSE);
		else if (ra->state == s_playing)
			stop_playback(a);
	}
}

EXPORT const char info_record[] = CORE_MOD_INFO("record");

EXPORT int MM_record(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		fake = mm->GetInterface(I_FAKE, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		clientset = mm->GetInterface(I_CLIENTSET, ALLARENAS);
		if (!aman || !pd || !cmd || !game || !fake || !lm || !net ||
				!chat || !cfg || !mapdata || !clientset)
			return MM_FAIL;
		adkey = aman->AllocateArenaData(sizeof(rec_adata));
		if (adkey == -1) return MM_FAIL;
		cmd->AddCommand("gamerecord", Cgamerecord, ALLARENAS, gamerecord_help);
		cmd->AddCommand("rec", Cgamerecord, ALLARENAS, gamerecord_help);
		net->AddPacket(C2S_POSITION, ppk);
		mm->RegCallback(CB_ARENAACTION, cb_aaction, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		Arena *a;
		rec_adata *ra;
		Link *link;

		/* make sure that there is nothing being played or recorded
		 * right now */
		aman->Lock();
		FOR_EACH_ARENA_P(a, ra, adkey)
			if (ra->state != s_none)
			{
				aman->Unlock();
				return MM_FAIL;
			}
		aman->Unlock();

		aman->FreeArenaData(adkey);
		mm->UnregCallback(CB_ARENAACTION, cb_aaction, ALLARENAS);
		net->RemovePacket(C2S_POSITION, ppk);
		cmd->RemoveCommand("gamerecord", Cgamerecord, ALLARENAS);
		cmd->RemoveCommand("rec", Cgamerecord, ALLARENAS);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(fake);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(clientset);
		return MM_OK;
	}
	return MM_FAIL;
}

