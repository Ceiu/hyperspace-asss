
/* dist: public */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"
#include "persist.h"
#include "clientset.h"
#include "packets/flags.h"


#define MAXFLAGS 255
#define TURFUPDATEDELAY 3000
#define AD_OK(ad) ((ad)->carrymode >= 0 && (ad)->game != NULL && (ad)->fis != NULL)


local Imodman *mm;
local Inet *net;
local Ichatnet *chatnet;
local Imainloop *ml;
local Ilogman *lm;
local Iarenaman *aman;
local Iclientset *clientset;
local Iplayerdata *pd;

local int adkey;
local override_key_t override_flag_carryflags;

local pthread_mutex_t flagmtx;
#define LOCK() pthread_mutex_lock(&flagmtx);
#define UNLOCK() pthread_mutex_unlock(&flagmtx);

typedef struct adata
{
	Iflaggame *game;
	int count, carrymode;
	FlagInfo *fis;
	Player *toucher; /* see set_flag */
	int during_init; /* ''           */
	int during_cleanup; /* ugly hack for check_consistency */
} adata;


/* checks consistency internal data. currently only checks
 * pkt.flagscarried values against internal flag state.
 * temporarily disabled due to issues with locking order.
 */
local void check_consistency(void)
{
#if 0
	Player *p;
	Arena *a;
	adata *ad;
	Link *link;
	int i, *count;
	int pdkey = pd->AllocatePlayerData(sizeof(int));

	LOCK();
	aman->Lock();
	FOR_EACH_ARENA_P(a, ad, adkey)
		for (i = 0; i < ad->count; i++)
			if (ad->fis[i].state == FI_CARRIED)
			{
				assert(ad->fis[i].carrier);
				assert(ad->during_cleanup ||
				       ad->fis[i].freq == ad->fis[i].carrier->p_freq);
				(*((int*)PPDATA(ad->fis[i].carrier, pdkey)))++;
			}
	aman->Unlock();
	pd->Lock();
	FOR_EACH_PLAYER_P(p, count, pdkey)
		assert(*count == p->pkt.flagscarried);
	pd->Unlock();
	UNLOCK();

	pd->FreePlayerData(pdkey);
#endif
}


local void ensure_space(adata *ad, int wanted)
{
	if (ad->count < wanted)
	{
		int oldcount = ad->count;
		ad->count = wanted;
		ad->fis = arealloc(ad->fis, wanted * sizeof(FlagInfo));
		memset(ad->fis + oldcount, 0, (ad->count - oldcount) * sizeof(FlagInfo));
	}
	check_consistency();
}


/* cleanup all flags owned by a player */
local void cleanup(Player *p, Arena *a, int reason)
{
	int i;
	adata *ad = P_ARENA_DATA(a, adkey);

	LOCK();
	ad->during_cleanup = TRUE;
	for (i = 0; i < ad->count; i++)
		if (ad->fis[i].state == FI_CARRIED &&
		    ad->fis[i].carrier == p)
		{
			ad->fis[i].state = FI_NONE;
			ad->fis[i].carrier = NULL;
			p->pkt.flagscarried--;
			/* call the flag lost callbacks here because cleanup might
			 * change the state and invoke another set of callbacks. */
			DO_CBS(CB_FLAGLOST, a, FlagLostFunc, (a, p, i, reason));
			/* leave freq in case manager wants to use it */
			ad->game->Cleanup(a, i, reason, p, ad->fis[i].freq);
		}
	ad->during_cleanup = FALSE;
	UNLOCK();
}


local int make_turf_packet(Arena *a, struct SimplePacketA *pkt)
{
	int i, fc;
	adata *ad = P_ARENA_DATA(a, adkey);
	LOCK();
	fc = ad->count;
	assert(fc < MAXFLAGS);
	pkt->type = S2C_TURFFLAGS;
	for (i = 0; i < fc; i++)
	{
		assert(ad->fis[i].state == FI_ONMAP);
		pkt->d[i] = ad->fis[i].freq;
	}
	UNLOCK();
	return 1 + fc * 2;
}

local int send_turf_timer(void *av)
{
	Arena *a = av;
	u16 buf[MAXFLAGS+2];
	struct SimplePacketA *pkt = (struct SimplePacketA*)buf;

	net->SendToArena(a, NULL, (byte*)pkt, make_turf_packet(a, pkt), NET_UNRELIABLE);
	/* lm->Log(L_DRIVEL, "DBG: sending turf ownership pkt to arena"); */

	return TRUE;
}

local void send_one_turf_packet(Player *p)
{
	Arena *a = p->arena;
	u16 buf[MAXFLAGS+2];
	struct SimplePacketA *pkt = (struct SimplePacketA*)buf;

	net->SendToOne(p, (byte*)pkt, make_turf_packet(a, pkt), NET_RELIABLE);
	/* lm->Log(L_DRIVEL, "DBG: sending turf ownership pkt to player"); */
}


/* do whatever necessary to make the indicated state change */
local void set_flag(Arena *a, int fid, FlagInfo *nfi)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	FlagInfo *ofi;

	if (fid < 0 || fid >= MAXFLAGS)
		return;

	ensure_space(ad, fid+1);

	ofi = &ad->fis[fid];

	if (ad->carrymode == CARRY_NONE)
	{
		/* turf flags are handled a little differently. they're always
		 * FI_ONMAP and only the freq matters. */
		if (nfi->state != FI_ONMAP)
		{
			lm->LogA(L_ERROR, "flagcore", a, "asked to take turf flag off map");
			return;
		}

		/* force no position for turf flags */
		nfi->x = nfi->y = -1;

		/* this is a little hacky: we change how we notify the client
		 * based on how we got here. if it was due to a flag pickup
		 * packet, use the s2c flag pickup to notify clients. if it was
		 * during Init(), don't notify clients at all. otherwise use a
		 * regular flag location packet. */
		if (ad->toucher)
		{
			struct S2CFlagPickup pkt =
				{ S2C_FLAGPICKUP, fid, ad->toucher->pid };
			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			/* lm->Log(L_DRIVEL, "DBG: sent turf update with pickup packet"); */
		}
		else if (ad->during_init)
			;
		else
		{
			/* schedule a turf ownership packet to be sent after one
			 * second. */
			ml->ClearTimer(send_turf_timer, a);
			ml->SetTimer(send_turf_timer, 100, TURFUPDATEDELAY, a, a);
			/* lm->Log(L_DRIVEL, "DBG: sent turf update with ownership packet"); */
		}

		*ofi = *nfi;

		DO_CBS(CB_FLAGONMAP, a, FlagOnMapFunc, (a, fid, -1, -1, ofi->freq));

		check_consistency();
		return;
	}

	/* the rest of this function is for carried flags. */

	if (nfi->state == FI_CARRIED && nfi->carrier == NULL)
	{
		lm->LogA(L_ERROR, "flagcore", a, "asked to set carried flag with no carrier");
		return;
	}
	if (nfi->state != FI_CARRIED && nfi->carrier != NULL)
	{
		lm->LogA(L_ERROR, "flagcore", a, "asked to set non-carried flag with carrier");
		return;
	}

	/* always set freq for carried flags */
	if (nfi->carrier)
		nfi->freq = nfi->carrier->p_freq;

	if (ofi->state == FI_CARRIED)
	{
		/* doing something to a carried flag. these can be handled
		 * similarly. unless we're transferring to the same carrier, the
		 * flag has to be dropped first. we can't drop just one flag, so
		 * drop them all and then re-pick-up the rest. after that, the
		 * desired flag is gone as far as the client is concerned, so
		 * fall through to the ofi->state == FI_NONE case. */

		if (nfi->state == FI_CARRIED && nfi->carrier == ofi->carrier)
		{
			/* this means it's a carried flag being transferred to the
			 * same player. do nothing. */
			/* lm->Log(L_DRIVEL, "DBG: null update of carried flag"); */
			check_consistency();
			return;
		}
		else
		{
			int i;
			Player *carrier = ofi->carrier;
			struct S2CFlagDrop pkt = { S2C_FLAGDROP, carrier->pid };

			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			carrier->pkt.flagscarried = 0;
			ofi->state = FI_NONE;
			ofi->carrier = NULL;
			ofi->freq = -1;

			for (i = 0; i < ad->count; i++)
				if (ad->fis[i].state == FI_CARRIED &&
				    ad->fis[i].carrier == carrier)
				{
					struct S2CFlagPickup pkt =
						{ S2C_FLAGPICKUP, i, carrier->pid };
					net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
					carrier->pkt.flagscarried++;
				}

			if (carrier->pkt.flagscarried)
			{
				/* this is a messy operation that we should avoid if
				 * possible, so log it. */
				lm->LogA(L_WARN, "flagcore", a, "faked removing one carried flag %d", fid);
			}

			DO_CBS(CB_FLAGLOST, a, FlagLostFunc,
					(a, carrier, fid, CLEANUP_OTHER));
		}
	}

	if (ofi->state == FI_NONE)
	{
		if (nfi->state == FI_NONE)
		{
			/* this is easy. copy data since it might have changed
			 * position or freq. */
			*ofi = *nfi;
			/* lm->Log(L_DRIVEL, "DBG: null update of nonexitent flag"); */
		}
		else if (nfi->state == FI_CARRIED)
		{
			/* assigning a nonexistent flag */
			struct S2CFlagPickup pkt =
				{ S2C_FLAGPICKUP, fid, nfi->carrier->pid };
			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			nfi->carrier->pkt.flagscarried++;
			*ofi = *nfi;
			DO_CBS(CB_FLAGGAIN, a, FlagGainFunc,
					(a, nfi->carrier, fid, FLAGGAIN_OTHER));
			/* lm->Log(L_DRIVEL, "DBG: updated none->carried w/pickup pkt"); */
		}
		else if (nfi->state == FI_ONMAP)
		{
			/* placing a nonexistent flag */
			struct S2CFlagLocation pkt =
				{ S2C_FLAGLOC, fid, nfi->x, nfi->y, nfi->freq };
			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			*ofi = *nfi;
			DO_CBS(CB_FLAGONMAP, a, FlagOnMapFunc,
					(a, fid, nfi->x, nfi->y, nfi->freq));
			/* lm->Log(L_DRIVEL, "DBG: updated none->onmap w/loc pkt"); */
		}
	}
	else if (ofi->state == FI_ONMAP)
	{
		if (nfi->state == FI_NONE)
		{
			/* removing a flag on the map. fake it by moving outside of
			 * playable area. */
			struct S2CFlagLocation pkt =
				{ S2C_FLAGLOC, fid, -1, -1, -1 };
			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			nfi->x = nfi->y = nfi->freq = -1;
			*ofi = *nfi;
			/* this is messy, so log it */
			lm->LogA(L_WARN, "flagcore", a, "faked removing flag %d", fid);
			/* lm->Log(L_DRIVEL, "DBG: updated onmap->none w/loc (-1,-1) pkt"); */
		}
		else if (nfi->state == FI_CARRIED)
		{
			/* picking up a flag on the map */
			struct S2CFlagPickup pkt =
				{ S2C_FLAGPICKUP, fid, nfi->carrier->pid };
			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			nfi->carrier->pkt.flagscarried++;
			*ofi = *nfi;
			/* lm->Log(L_DRIVEL, "DBG: updated onmap->carried w/pickup pkt"); */
			DO_CBS(CB_FLAGGAIN, a, FlagGainFunc,
					(a, nfi->carrier, fid, FLAGGAIN_PICKUP));
		}
		else if (nfi->state == FI_ONMAP)
		{
			/* placing a flag that's on the map already */
			struct S2CFlagLocation pkt =
				{ S2C_FLAGLOC, fid, nfi->x, nfi->y, nfi->freq };
			net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
			*ofi = *nfi;
			/* lm->Log(L_DRIVEL, "DBG: updated onmap->onmap w/loc pkt"); */
			DO_CBS(CB_FLAGONMAP, a, FlagOnMapFunc,
					(a, fid, nfi->x, nfi->y, nfi->freq));
		}
	}
	else
	{
		lm->LogA(L_ERROR, "flagcore", a, "bad flag state: %d has %d",
				fid, ofi->state);
	}
	check_consistency();
}


local void aaction(Arena *arena, int action)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	if (action == AA_CREATE)
	{
		LOCK();
		ad->game = mm->GetInterface(I_FLAGGAME, arena);
		if (ad->game)
		{
			ad->carrymode = -1;
			lm->LogA(L_INFO, "flagcore", arena, "setting up flagcore using game %s",
					ad->game->head.name);
			ad->count = 0;
			ad->fis = NULL;
			ad->during_init = TRUE;
			ad->game->Init(arena);
			ad->during_init = FALSE;
			if (AD_OK(ad))
			{
				clientset->ArenaOverride(arena, override_flag_carryflags, ad->carrymode);

				if (ad->carrymode == CARRY_NONE)
					ml->SetTimer(send_turf_timer, TURFUPDATEDELAY,
							TURFUPDATEDELAY, arena, arena);
			}
			else
			{
				lm->LogA(L_ERROR, "flagcore", arena, "flag game didn't set carry mode");
				aaction(arena, AA_DESTROY);
			}
		}
		UNLOCK();
	}
	else if (action == AA_DESTROY)
	{
		ml->ClearTimer(send_turf_timer, arena);
		LOCK();
		afree(ad->fis);
		ad->count = 0;
		if (ad->game)
			mm->ReleaseInterface(ad->game);
		ad->game = NULL;
		UNLOCK();
	}
	check_consistency();
}

local void paction(Player *p, int action, Arena *a)
{
	if (action == PA_ENTERARENA)
	{
		adata *ad = P_ARENA_DATA(a, adkey);
		LOCK();
		p->pkt.flagscarried = 0;
		if (AD_OK(ad))
		{
			if (ad->carrymode == CARRY_NONE)
				send_one_turf_packet(p);
			else
			{
				int i;
				for (i = 0; i < ad->count; i++)
				{
					FlagInfo *fi = &ad->fis[i];
					if (fi->state == FI_ONMAP)
					{
						struct S2CFlagLocation fl =
						{ S2C_FLAGLOC, i, fi->x, fi->y, fi->freq };
						net->SendToOne(p, (byte*)&fl, sizeof(fl), NET_RELIABLE);
					}
				}
			}
		}
		UNLOCK();
	}
	else if (action == PA_LEAVEARENA)
	{
		cleanup(p, a, CLEANUP_LEFTARENA);
	}
	check_consistency();
}

local void newplayer(Player *p, int new)
{
	if (p->type == T_FAKE && p->arena && !new)
	{
		/* extra cleanup for fake players that were carrying flags,
		 * since PA_LEAVEARENA isn't called. */
		cleanup(p, p->arena, CLEANUP_LEFTARENA);
	}
	check_consistency();
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	if (newship == oldship)
	{
		cleanup(p, p->arena, CLEANUP_FREQCHANGE);
	}
	else
	{
		cleanup(p, p->arena, CLEANUP_SHIPCHANGE);
	}
	check_consistency();
}

local void mykill(Arena *a, Player *killer, Player *killed,
		int bty, int flags, int *pts, int *green)
{
	int cancarry, i, overflow = FALSE;
	adata *ad = P_ARENA_DATA(a, adkey);

	LOCK();

	if (!AD_OK(ad))
	{
		UNLOCK();
		return;
	}

	check_consistency();
	if (killed->pkt.flagscarried == 0)
	{
		UNLOCK();
		return;
	}

	if (ad->carrymode == CARRY_NONE)
	{
		lm->LogP(L_MALICIOUS, "flagcore", killed, "got flag kill in turf game");
		UNLOCK();
		return;
	}

	if (ad->carrymode == CARRY_ALL)
		cancarry = MAXFLAGS;
	else
		cancarry = ad->carrymode - 1;

	/* change our state to how clients now thinks things are, and then
	 * call our manager's Cleanup to fix up the state. */
	for (i = 0; i < ad->count; i++)
		if (ad->fis[i].state == FI_CARRIED &&
		    ad->fis[i].carrier == killed)
		{
			if (killer->pkt.flagscarried < cancarry)
			{
				int reason;

				killer->pkt.flagscarried++;
				killed->pkt.flagscarried--;
				ad->fis[i].carrier = killer;
				ad->fis[i].freq = killer->p_freq;

				if (killer->type == T_FAKE)
					reason = CLEANUP_KILL_FAKE;
				else if (killer->p_freq == killed->p_freq)
					reason = CLEANUP_KILL_TK;
				else
					reason = CLEANUP_KILL_NORMAL;
				DO_CBS(CB_FLAGLOST, a, FlagLostFunc,
						(a, killed, i, reason));
				DO_CBS(CB_FLAGGAIN, a, FlagGainFunc,
						(a, killer, i, FLAGGAIN_KILL));
				ad->game->Cleanup(a, i, reason, killed, killed->p_freq);
			}
			else
				overflow = TRUE;
		}

	/* special overflow handling for when the killer has been given too
	 * many flags. */
	/* first have the killer drop all his flags, and pick up the ones
	 * that really belong to him. */
	if (overflow)
	{
		struct S2CFlagDrop pkt = { S2C_FLAGDROP, killer->pid };
		net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);

		for (i = 0; i < ad->count; i++)
			if (ad->fis[i].state == FI_CARRIED)
			{
				if (ad->fis[i].carrier == killer)
				{
					/* re-pick-up killer flags */
					struct S2CFlagPickup pkt =
						{ S2C_FLAGPICKUP, i, killer->pid };
					net->SendToArena(a, NULL, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
				}
				else if (ad->fis[i].carrier == killed)
				{
					/* the client thought these transferred, but after the
					 * drop/re-pick-up maneuver, the client now thinks these
					 * are void. */
					killed->pkt.flagscarried--;
					ad->fis[i].state = FI_NONE;
					ad->fis[i].carrier = NULL;
					/* leave freq in case manager wants to use it */
					DO_CBS(CB_FLAGLOST, a, FlagLostFunc,
							(a, killed, i, CLEANUP_KILL_CANTCARRY));
					ad->game->Cleanup(a, i, CLEANUP_KILL_CANTCARRY, killed, killed->p_freq);
				}
			}
	}

	check_consistency();
	UNLOCK();
}


#define ERR(lev, msg) { lm->LogP(lev, "flags", p, msg); return; }
#define ERR_UNLOCK(lev, msg) { lm->LogP(lev, "flags", p, msg); UNLOCK(); return; }

local void p_flagtouch(Player *p, byte *pkt, int len)
{
	Arena *a = p->arena;
	adata *ad;
	int fid = ((struct C2SFlagPickup*)pkt)->fid;

	if (len != sizeof(struct C2SFlagPickup))
		ERR(L_MALICIOUS, "bad flag pickup packet length")

	if (!a || p->status != S_PLAYING)
		ERR(L_MALICIOUS, "flag pickup packet from bad state/arena")

	if (p->p_ship >= SHIP_SPEC)
		ERR(L_WARN, "state sync problem: flag pickup packet from spec")

	if (p->flags.during_change)
		ERR(L_INFO, "flag pickup packet before ack from ship/freq change")

	if (p->flags.no_flags_balls)
		ERR(L_INFO, "too lagged to pick up flag")

	ad = P_ARENA_DATA(a, adkey);

	LOCK();

	if (fid < 0 || fid >= ad->count)
		ERR_UNLOCK(L_MALICIOUS, "bad flag id in flag pickup packet");

	if (ad->fis[fid].state != FI_ONMAP)
		ERR_UNLOCK(L_MALICIOUS, "tried to pick up flag not on map");

	ad->toucher = p;
	ad->game->FlagTouch(a, p, fid);
	ad->toucher = NULL;

	check_consistency();
	UNLOCK();
}

local void p_flagtimer(Player *p, byte *pkt, int len)
{
	Arena *a = p->arena;
	adata *ad;
	int reason = (p->position.status & STATUS_SAFEZONE) ?
		CLEANUP_INSAFE : CLEANUP_DROPPED;
	struct S2CFlagDrop sfd = { S2C_FLAGDROP, p->pid };

	if (len != 1)
		ERR(L_MALICIOUS, "bad flag drop packet length")

	if (!a || p->status != S_PLAYING)
		ERR(L_MALICIOUS, "flag drop packet from bad state/arena")

	if (p->p_ship >= SHIP_SPEC)
		ERR(L_WARN, "state sync problem: flag drop packet from spec")

	ad = P_ARENA_DATA(a, adkey);

	LOCK();

	if (ad->carrymode == CARRY_NONE)
		ERR_UNLOCK(L_MALICIOUS, "flag drop packet in turf game");

	/* send drop packet so clients know flags are gone */
	net->SendToArena(a, NULL, (byte*)&sfd, sizeof(sfd), NET_RELIABLE);

	/* now call the cleanup handler */
	cleanup(p, a, reason);

	check_consistency();
	UNLOCK();
}

#undef ERR
#undef ERR_UNLOCK


local int GetFlags(Arena *a, int fid, FlagInfo *fis, int count)
{
	int i = 0;
	adata *ad = P_ARENA_DATA(a, adkey);
	LOCK();
	if (AD_OK(ad) && fid >= 0)
		for (; i < count && fid < ad->count; i++, fis++, fid++)
			*fis = ad->fis[fid];
	UNLOCK();
	return i;
}


local int SetFlags(Arena *a, int fid, FlagInfo *fis, int count)
{
	int i = 0;
	adata *ad = P_ARENA_DATA(a, adkey);
	LOCK();
	if (AD_OK(ad) && fid >= 0)
		for (; i < count; i++, fis++, fid++)
			set_flag(a, fid, fis);
	UNLOCK();
	return i;
}


local void FlagReset(Arena *a, int freq, int points)
{
	int i, endinterval = FALSE;
	adata *ad = P_ARENA_DATA(a, adkey);

	LOCK();

	if (!AD_OK(ad))
	{
		UNLOCK();
		return;
	}

	if (ad->carrymode == CARRY_NONE)
	{
		for (i = 0; i < ad->count; i++)
			ad->fis[i].freq = -1;

		send_turf_timer(a);
		if (chatnet)
			chatnet->SendToArena(a, NULL, "MSG:ARENA:Turf game reset.");
		lm->LogA(L_INFO, "flagcore", a, "turf game reset");
	}
	else
	{
		struct S2CFlagVictory fv = { S2C_FLAGRESET, freq, points };

		for (i = 0; i < ad->count; i++)
		{
			ad->fis[i].state = FI_NONE;
			if (ad->fis[i].carrier)
				ad->fis[i].carrier->pkt.flagscarried--;
			ad->fis[i].carrier = NULL;
			ad->fis[i].freq = -1;
		}

		net->SendToArena(a, NULL, (byte*)&fv, sizeof(fv), NET_RELIABLE);
		if (chatnet)
		{
			if (freq == -1)
				chatnet->SendToArena(a, NULL, "MSG:ARENA:Flag game reset.");
			else
				chatnet->SendToArena(a, NULL,
						"MSG:ARENA:Flag victory: freq %d won %d points.", freq, points);
		}

		if (freq == -1)
			lm->LogA(L_INFO, "flagcore", a, "flag reset");
		else
			lm->LogA(L_INFO, "flagcore", a, "flag victory: freq %d won %d points",
					freq, points);

		endinterval = TRUE;
	}

	DO_CBS(CB_FLAGRESET, a, FlagResetFunc, (a, freq, points));

	check_consistency();
	UNLOCK();

	/* now that we're outside of the flagcore lock, we can safely perform this check. */
	if (freq != -1 && ad->carrymode != CARRY_NONE)
	{
		/* determine which players have been effectively shipreset by this flag reset,
		 * update their is_dead flags and invoke CB_SPAWN. */
		Link *link;
		Player *p;
		pd->Lock();
		FOR_EACH_PLAYER_IN_ARENA(p, a)
		{
			int flags;

			if (p->p_freq != freq)
				continue;
			if (p->p_ship == SHIP_SPEC)
				continue;

			flags = SPAWN_FLAGVICTORY | SPAWN_SHIPRESET;

			if (p->flags.is_dead)
			{
				p->flags.is_dead = 0;
				flags |= SPAWN_AFTERDEATH;
			}

			DO_CBS(CB_SPAWN, a, SpawnFunc, (p, flags));
		}
		pd->Unlock();
	}

	if (endinterval)
	{
		/* note that this is being done after the CB_FLAGRESET
		 * callbacks. the stats incrementing will probably be done in
		 * one of those callbacks, so ending the interval after that
		 * means those stats get recorded in the correct interval. */
		Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (persist)
			persist->EndInterval(NULL, a, INTERVAL_GAME);
		mm->ReleaseInterface(persist);
	}

}


local int CountFlags(Arena *a)
{
	int count = 0;
	adata *ad = P_ARENA_DATA(a, adkey);
	LOCK();
	if (AD_OK(ad))
		count = ad->count;
	UNLOCK();
	return count;
}


local int CountFreqFlags(Arena *a, int freq)
{
	int i, count = 0;
	adata *ad = P_ARENA_DATA(a, adkey);
	LOCK();
	if (AD_OK(ad))
		for (i = 0; i < ad->count; i++)
			if ( ( ad->fis[i].state == FI_ONMAP &&
			       ad->fis[i].freq == freq ) ||
			     ( ad->fis[i].state == FI_CARRIED &&
			       ad->fis[i].carrier &&
			       ad->fis[i].carrier->p_freq == freq ) )
				count++;
	UNLOCK();
	return count;
}


local int CountPlayerFlags(Player *p)
{
	int count;
	LOCK();
	check_consistency();
	count = p->pkt.flagscarried;
	UNLOCK();
	return count;
}


local int IsWinning(Arena *a, int freq)
{
	// TODO: this needs to be passed on to ad->game instead
	int flags = CountFlags(a);
	if (flags && flags == CountFreqFlags(a, freq))
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}


local void SetCarryMode(Arena *a, int carrymode)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	LOCK();
	if (ad->during_init)
		ad->carrymode = carrymode;
	else
		lm->LogA(L_ERROR, "flagcore", a, "SetCarryMode called after initialization");
	UNLOCK();
}


local void ReserveFlags(Arena *a, int flagcount)
{
	adata *ad = P_ARENA_DATA(a, adkey);
	if (flagcount < 0 || flagcount >= MAXFLAGS)
		return;
	LOCK();
	ensure_space(ad, flagcount);
	UNLOCK();
}


local Iflagcore flagint =
{
	INTERFACE_HEAD_INIT(I_FLAGCORE, "flagcore")
	GetFlags, SetFlags, FlagReset,
	CountFlags, CountFreqFlags, CountPlayerFlags, IsWinning,
	SetCarryMode, ReserveFlags
};

EXPORT const char info_flagcore[] = CORE_MOD_INFO("flagcore");

EXPORT int MM_flagcore(int action, Imodman *mm_, Arena *a)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		clientset = mm->GetInterface(I_CLIENTSET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

		if (!net || !ml || !lm || !aman || !clientset || !pd)
			return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1)
			return MM_FAIL;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&flagmtx, &attr);
		pthread_mutexattr_destroy(&attr);

		override_flag_carryflags =
			clientset->GetOverrideKey("Flag", "CarryFlags");

		net->AddPacket(C2S_PICKUPFLAG, p_flagtouch);
		net->AddPacket(C2S_DROPFLAGS, p_flagtimer);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->RegCallback(CB_NEWPLAYER, newplayer, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, ALLARENAS);
		mm->RegCallback(CB_KILL, mykill, ALLARENAS);

		mm->RegInterface(&flagint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&flagint, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(send_turf_timer, NULL);

		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		mm->UnregCallback(CB_NEWPLAYER, newplayer, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, aaction, ALLARENAS);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, ALLARENAS);
		mm->UnregCallback(CB_KILL, mykill, ALLARENAS);

		net->RemovePacket(C2S_PICKUPFLAG, p_flagtouch);
		net->RemovePacket(C2S_DROPFLAGS, p_flagtimer);

		pthread_mutex_destroy(&flagmtx);

		aman->FreeArenaData(adkey);

		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(clientset);
		mm->ReleaseInterface(pd);

		return MM_OK;
	}
	else
		return MM_FAIL;
}

