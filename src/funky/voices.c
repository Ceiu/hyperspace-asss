
/* dist: public */

/* changelog/fixmes

Catid (cat02e@fsu.edu) Oct 24, 2003
	Decided not to include an interface in the first
	version, because likely there will be no need for it.

Catid (cat02e@fsu.edu) Oct 21, 2003
	Help!  I'm not hearing voices!
	Wrote a short essay on player voices
*/

/*  A Short eSSay on player voices

the main problem i see from voices is the possibility that
large numbers of users can eat up large amounts of server
memory.  my solution to this problem is to limit each user's
voice buffers to a fixed upper limit, and to limit all
users' voice data to within a certain amount of memory

secondary problem is that it can be used to hog the 00 08
uploads or maybe flood a user offline.  i'm going to
rate-limit these packets to fix this problem.  the limit
will be on the receiving players and the sending players,
so many users cannot flood a single user, and single users
cannot hog server upstream bandwidth.  however, it will
be possible for many users to hog server upstream b/w

SubGame happily allowed any sum of memory to be consumed
by these rather non-critical packets, and thus it had no
protocol for informing the client that his voice message
had not been sent.  we'll have to do that with arena msgs
*/

#include <string.h>

#include "asss.h"
#include "packets/voice.h"

/* 250 users can each have one 32 KB audio sample */

#define MAX_VOICE_MEMORY 16000000 /* (bytes) 16 MB */
#define PER_PLAYER_LIMIT 32000 /* (bytes) */

local int allocated_voice_sum = 0;


/* max number of samples per user is 256, but
   Continuum only supports 4 samples at a time

   in the future, some bot developers may
   decide to increase this limit.  it's
   presently set so low to keep memory
   usage low-ish.  they should not just
   increase NUM_SAMPLES to 255, but should
   instead dynamically allocate the array
   and add a capability for exceeding the
   normal limit
*/

#define NUM_SAMPLES 4 /* SS had 2, Ctm has 4 */

/* 32 KB / ~1 KB/sec = 32 seconds */
#define VOICE_RECEIVE_RATE_LIMIT 4000 /* (ticks) 40 seconds */
#define VOICE_RELAY_RATE_LIMIT 2100 /* (ticks) 21 seconds */

typedef struct vdata
{
	struct S2CVoice *samples[NUM_SAMPLES];
	int lengths[NUM_SAMPLES];

	int used, sized_sample, sized_target;

	ticks_t last_recv, last_relay;
	int warned;

	pthread_mutex_t mtx;
} vdata;

#define MUTEX_CREATE(vd) pthread_mutex_init(&(vd)->mtx, NULL)
#define MUTEX_LOCK(vd) pthread_mutex_lock(&(vd)->mtx)
#define MUTEX_UNLOCK(vd) pthread_mutex_unlock(&(vd)->mtx)
#define MUTEX_DESTROY(vd) pthread_mutex_destroy(&(vd)->mtx)

typedef struct adata
{
	byte allow_voices;
} adata;

local int vdkey;
local int avkey;


/* callbacks */

local void PVoice(Player *, byte *, int);
local void PVoiceSized(Player *p, byte *pkt, int len, int offset, int totallen);

local void PlayerAction(Player *p, int action, Arena *arena);
local void ArenaAction(Arena *arena, int action);


/* global data */

local Imodman *mm;
local Inet *net;
local Ilogman *lm;
local Iplayerdata *pd;
local Ichat *chat;
local Iconfig *cfg;
local Iarenaman *aman;


/* lleeet's go! */

EXPORT int MM_voices(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);

		if (!aman ||
			-1 == ( avkey = aman->AllocateArenaData(sizeof(adata)) )
			)
		{
			mm->ReleaseInterface(aman);
			return MM_FAIL;
		}

		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);

		if (!net || !pd || !cfg ||
			-1 == ( vdkey = pd->AllocatePlayerData(sizeof(vdata)) )
			)
		{
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(aman);
			return MM_FAIL;
		}

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		net->AddPacket(C2S_RELAYVOICE, PVoice);
		net->AddSizedPacket(C2S_RELAYVOICE, PVoiceSized);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_RELAYVOICE, PVoice);
		net->RemoveSizedPacket(C2S_RELAYVOICE, PVoiceSized);

		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		pd->FreePlayerData(vdkey);
		aman->FreeArenaData(avkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(aman);

		return MM_OK;
	}
	return MM_FAIL;
}


local void PVoice(Player *p, byte *pkt, int len)
{
	struct C2SVoice *voice = (struct C2SVoice *)pkt;
	Player *t;
	vdata *vd = PPDATA(p, vdkey);
	struct S2CVoice *rvd;
	int vid, rlen, slen;

	if (len < sizeof(struct C2SVoice))
	{
		if (lm) lm->LogP(L_MALICIOUS, "voices", p, "bad packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING)
	{
		if (lm) lm->LogP(L_WARN, "voices", p, "ignored request from bad state");
		return;
	}

	vid = voice->voice_index;
	if (/* vid < 0 || */ vid >= NUM_SAMPLES)
	{
		if (lm) lm->LogP(L_MALICIOUS, "voices", p, "bad sound sample index");
		return;
	}

	MUTEX_LOCK(vd);

	/* ignore this silently. it means the client is requesting to send something
	   that it hasn't finished uploading */
	if (vid == vd->sized_sample)
	{
		MUTEX_UNLOCK(vd);
		return;
	}

	rvd = vd->samples[vid];
	rlen = vd->lengths[vid];
	slen = len - sizeof(struct C2SVoice);

	/* we will accept 00 08/9 as an alternate transfer mechanism this way */
	if (slen)
	{
		/* store sample now if we can, because Ctm thinks we did */
		if (rvd)
		{
			afree(rvd);
			vd->samples[vid] = 0;

			vd->used -= rlen;
			allocated_voice_sum -= rlen;
		}

		rlen = sizeof(struct S2CVoice) + slen;

		if (vd->used + rlen > PER_PLAYER_LIMIT || allocated_voice_sum + rlen > MAX_VOICE_MEMORY)
		{
			if (chat)
				chat->SendMessage(p, "Server has reached its limit for caching voice samples. "
								"Players will not receive this sound message");
			MUTEX_UNLOCK(vd);
			return;
		}

		rvd = (struct S2CVoice *)amalloc(rlen);
		vd->lengths[vid] = rlen;
		vd->samples[vid] = rvd;

		/* fill in the cached packet */
		rvd->type = S2C_VOICE;
		rvd->source_id = p->pid;
		memcpy((byte*)(rvd) + sizeof(struct S2CVoice), pkt + sizeof(struct C2SVoice), slen);

		vd->used += rlen;
		allocated_voice_sum += rlen;
	}
	else if (!rvd)
	{
		if (lm) lm->LogP(L_MALICIOUS, "voices", p, "ignored null voice relay request");
		MUTEX_UNLOCK(vd);
		return;
	}

	t = pd->PidToPlayer(voice->target_id);

	if (t && t->status == S_PLAYING && t->arena == p->arena && t != p && t->pkt.acceptaudio)
	{
		ticks_t curr = current_ticks();

		if (curr - vd->last_relay >= VOICE_RELAY_RATE_LIMIT)
		{
			vdata *td = PPDATA(t, vdkey);

			if (curr - td->last_recv >= VOICE_RECEIVE_RATE_LIMIT)
			{
				net->SendToOne(t, (byte*)rvd, rlen, NET_RELIABLE | NET_PRI_N1);

				if (lm) lm->LogP(L_DRIVEL, "voices", p, "relayed sample %i -> %s", vid, t->name);

				vd->last_relay = td->last_recv = curr;
				vd->warned = FALSE;
			}
			else
			{
				if (chat)
					chat->SendMessage(p, "%s did not hear your voice. They may receive "
							"another sound message in about %i seconds", t->name,
							(VOICE_RECEIVE_RATE_LIMIT - curr + td->last_recv) / 100);
			}
		}
		else if (!vd->warned)
		{
			if (chat)
				chat->SendMessage(p, "%s did not hear your voice. You may send "
						"another sound message in about %i seconds", t->name,
						(VOICE_RELAY_RATE_LIMIT - curr + vd->last_relay) / 100);
			vd->warned = TRUE;
		}
	}

	MUTEX_UNLOCK(vd);
}


local void PVoiceSized(Player *p, byte *pkt, int len, int offset, int totallen)
{
	/* <type(1)> <sample index(1)> <target id(2)> <compressed wave(len-4)> */

	/*	sized errata:
			offset == -1 when transfer is aborted
			len == 0 when transfer is completed
			offset == 0 when transfer has just started
			else transfer is in progress */

	struct C2SVoice *voice = (struct C2SVoice *)pkt;
	Player *t;
	vdata *vd = PPDATA(p, vdkey);
	struct S2CVoice *rvd;
	int vid, rlen;

	MUTEX_LOCK(vd);

	if (offset == -1)
	{
		/* aborted */

		vid = vd->sized_sample;

		if (vid != -1)
		{
			rvd = vd->samples[vid];

			if (rvd)
			{
				afree(rvd);
				vd->samples[vid] = 0;

				rlen = vd->lengths[vid];
				vd->used -= rlen;
				allocated_voice_sum -= rlen;
			}
		}
	}
	else if (len == 0)
	{
		/* completed */

		vid = vd->sized_sample;
		rvd = vd->samples[vid];
		rlen = vd->lengths[vid];

		vd->sized_sample = -1;

		t = pd->PidToPlayer(vd->sized_target);

		if (t && t->status == S_PLAYING && t->arena == p->arena && t != p && t->pkt.acceptaudio)
		{
			ticks_t curr = current_ticks();

			if (curr - vd->last_relay >= VOICE_RELAY_RATE_LIMIT)
			{
				vdata *td = PPDATA(t, vdkey);

				if (curr - td->last_recv >= VOICE_RECEIVE_RATE_LIMIT)
				{
					net->SendToOne(t, (byte*)rvd, rlen, NET_RELIABLE | NET_PRI_N1);

					if (lm) lm->LogP(L_DRIVEL, "voices", p, "relayed new sample %i -> %s", vid, t->name);

					vd->last_relay = td->last_recv = curr;

					vd->warned = FALSE;
				}
				else
				{
					if (chat)
						chat->SendMessage(p, "%s did not hear your voice. They may receive "
								"another sound message in about %i seconds", t->name,
								(VOICE_RECEIVE_RATE_LIMIT - curr + td->last_recv) / 100);
				}
			}
			else if (!vd->warned)
			{
				if (chat)
					chat->SendMessage(p, "%s did not hear your voice. You may send "
							"another sound message in about %i seconds", t->name,
							(VOICE_RELAY_RATE_LIMIT - curr + vd->last_relay) / 100);
				vd->warned = TRUE;
			}
		}
	}
	else if (offset == 0)
	{
		/* just started */

		if (totallen <= sizeof(struct C2SVoice) || len < sizeof(struct C2SVoice))
		{
			if (lm) lm->LogP(L_MALICIOUS, "voices", p, "bad packet len=%i of %i", len, totallen);
			MUTEX_UNLOCK(vd);
			return;
		}

		vid = voice->voice_index;
		if (/* vid < 0 || */ vid >= NUM_SAMPLES || vd->sized_sample != -1)
		{
			if (lm) lm->LogP(L_MALICIOUS, "voices", p, "bad sound sample index");
			MUTEX_UNLOCK(vd);
			return;
		}

		rvd = vd->samples[vid];
		rlen = vd->lengths[vid];

		if (rvd)
		{
			afree(rvd);
			vd->samples[vid] = 0;

			vd->used -= rlen;
			allocated_voice_sum -= rlen;
		}

		rlen = sizeof(struct S2CVoice) + totallen - sizeof(struct C2SVoice);

		if (vd->used + rlen > PER_PLAYER_LIMIT || allocated_voice_sum + rlen > MAX_VOICE_MEMORY)
		{
			if (chat)
				chat->SendMessage(p, "Server has reached its limit for caching voice samples. "
								"Players will not receive this sound message");
			MUTEX_UNLOCK(vd);
			return;
		}

		rvd = (struct S2CVoice *)amalloc(rlen);
		vd->lengths[vid] = rlen;
		vd->samples[vid] = rvd;

		vd->used += rlen;
		allocated_voice_sum += rlen;

		/* start filling in the cached packet */
		rvd->type = S2C_VOICE;
		rvd->source_id = p->pid;
		memcpy((byte*)(rvd) + sizeof(struct S2CVoice), pkt + sizeof(struct C2SVoice), len - sizeof(struct C2SVoice));

		vd->sized_sample = vid;
		vd->sized_target = voice->target_id;
	}
	else
	{
		/* in progress */

		vid = vd->sized_sample;

		if (vid != -1)
			memcpy((byte*)(vd->samples[vid]) + offset - sizeof(struct C2SVoice) + sizeof(struct S2CVoice), pkt, len);
	}

	MUTEX_UNLOCK(vd);
}


local void PlayerAction(Player *p, int action, Arena *arena)
{
	vdata *vd = PPDATA(p, vdkey);

	if (action == PA_CONNECT)
	{
		vd->sized_sample = -1;

		MUTEX_CREATE(vd);
	}
	else if (action == PA_PREENTERARENA)
	{
		adata *ad = P_ARENA_DATA(arena, avkey);

		if (!ad->allow_voices)
			p->pkt.acceptaudio = 0;
	}
	else if (action == PA_DISCONNECT)
	{
		struct S2CVoice *rvd;
		int ii, rlen;

		for (ii = 0; ii < NUM_SAMPLES; ++ii)
		{
			rvd = vd->samples[ii];
			rlen = vd->lengths[ii];

			if (rvd)
			{
				afree(rvd);

				vd->used -= rlen;
				allocated_voice_sum -= rlen;
			}
		}

		MUTEX_DESTROY(vd);
	}
}


local void ArenaAction(Arena *arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		adata *ad = P_ARENA_DATA(arena, avkey);

		ad->allow_voices = cfg->GetInt(arena->cfg, "Message", "AllowAudioMessages", 1);
	}
}
