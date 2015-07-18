
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "clientset.h"
#include "persist.h"


#define KEY_SHIPLOCK 46

#define WEAPONCOUNT 32


/* structs */

#include "packets/kill.h"
#include "packets/shipchange.h"
#include "packets/green.h"


/* these are bit positions for the personalgreen field */
enum { personal_thor, personal_burst, personal_brick };

typedef struct
{
	struct C2SPosition pos;
	Player *speccing;
	u32 wpnsent;
	int ignoreweapons, deathwofiring;

	/* epd/energy stuff */
	int epd_queries;
	struct { byte seenrg, seenrgspec, seeepd, pad1; } pl_epd;
	/*            enum    enum        bool              */

	/* some flags */
	byte lockship, rgnnoanti, rgnnoweapons, rgnnorecvanti, rgnnorecvweps; // TODO: the last two still need code in Pppk
	time_t expires; /* when the lock expires, or 0 for session-long lock */
	ticks_t lastrgncheck; /* when we last updated the region-based flags */
	LinkedList lastrgnset;
} pdata;

typedef struct
{
	int spec_epd, spec_nrg, all_nrg;
	/*  bool      enum      enum     */
	u32 personalgreen;
	int initlockship, initspec;
	int deathwofiring;
	int regionchecktime;
	int nosafeanti;
	int cfg_pospix;
	int cfg_sendanti;
	int wpnrange[WEAPONCOUNT]; /* there are 5 bits in the weapon type */
} adata;

typedef struct safezone_closure_t
{
	Arena *arena;
	Player *p;
	int x;
	int y;
	int entering;
} safezone_closure_t;

/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ichatnet *chatnet;
local Imainloop *ml;
local Ilogman *lm;
local Imodman *mm;
local Iarenaman *aman;
local Icapman *capman;
local Imapdata *mapdata;
local Ilagcollect *lagc;
local Ichat *chat;
local Iprng *prng;
local Icmdman *cmd;
local Ipersist *persist;

local int adkey, pdkey;

local pthread_mutex_t specmtx = PTHREAD_MUTEX_INITIALIZER;
local pthread_mutex_t freqshipmtx = PTHREAD_MUTEX_INITIALIZER;


#define SEE_ENERGY_MAP(F) \
	F(SEE_NONE)   /* nobody can see energy */  \
	F(SEE_ALL)    /* everyone can see everyone's */  \
	F(SEE_TEAM)   /* you can see only energy for teammates */  \
	F(SEE_SPEC)   /* can see energy/extra data only for who you are speccing */


DEFINE_ENUM(SEE_ENERGY_MAP)
DEFINE_FROM_STRING(see_nrg_val, SEE_ENERGY_MAP)



local void DoWeaponChecksum(struct S2CWeapons *pkt)
{
	int i;
	u8 ck = 0;
	pkt->checksum = 0;
	for (i = 0; i < sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData); i++)
		ck ^= ((unsigned char*)pkt)[i];
	pkt->checksum = ck;
}


local inline long lhypot (register long dx, register long dy)
{
	register unsigned long r, dd;

	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	/* initial hypotenuse guess (from Gems) */
	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));

	if (r == 0) return (long)r;

	/* converge 3 times */
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}


struct region_cb_params
{
	pdata *data;
	LinkedList newrgnset;
};

local void ppk_region_cb(void *clos, Region *rgn)
{
	struct region_cb_params *params = clos;
	LLAdd(&params->newrgnset, rgn);
	if (mapdata->RegionChunk(rgn, RCT_NOANTIWARP, NULL, NULL))
		params->data->rgnnoanti = 1;
	if (mapdata->RegionChunk(rgn, RCT_NOWEAPONS, NULL, NULL))
		params->data->rgnnoweapons = 1;
	if (mapdata->RegionChunk(rgn, RCT_NORECVANTI, NULL, NULL))
		params->data->rgnnorecvanti = 1;
	if (mapdata->RegionChunk(rgn, RCT_NORECVWEPS, NULL, NULL))
		params->data->rgnnorecvweps = 1;
}

local void do_region_callback(Player *p, Region *rgn, int x, int y, int entering)
{
	/* FIXME: make this asynchronous? */
	DO_CBS(CB_REGION, p->arena, RegionFunc, (p, rgn, x, y, entering));
}

local void update_regions(Player *p, int x, int y)
{
	Link *ol, *nl;
	struct region_cb_params params = { PPDATA(p, pdkey), LL_INITIALIZER };

	params.data->rgnnoanti = params.data->rgnnorecvanti = 0;
	params.data->rgnnoweapons = params.data->rgnnorecvweps = 0;

	mapdata->EnumContaining(p->arena, x, y, ppk_region_cb, &params);

	/* sort new list so we can do a linear diff */
	LLSort(&params.newrgnset, NULL);

	/* now walk through both in parallel and call appropriate callbacks */
	ol = LLGetHead(&params.data->lastrgnset);
	nl = LLGetHead(&params.newrgnset);
	while (ol || nl)
		if (!nl || (ol && ol->data < nl->data))
		{
			/* the new set is missing an old one. this is a region exit. */
			do_region_callback(p, ol->data, x, y, FALSE);
			ol = ol->next;
		}
		else if (!ol || (nl && ol->data > nl->data))
		{
			/* this is a region enter. */
			do_region_callback(p, nl->data, x, y, TRUE);
			nl = nl->next;
		}
		else /* ol->data == nl->data */
		{
			/* same state for this region */
			ol = ol->next;
			nl = nl->next;
		}

	/* and swap lists */
	LLEmpty(&params.data->lastrgnset);
	params.data->lastrgnset = params.newrgnset;
}


local int run_enter_game_cb(void *clos)
{
	Player *p = (Player *)clos;
	if (p->status == S_PLAYING)
		DO_CBS(CB_PLAYERACTION,
				p->arena,
				PlayerActionFunc,
				(p, PA_ENTERGAME, p->arena));
	return FALSE;
}

local int run_safezone_cb(void *clos)
{
	safezone_closure_t *closure = (safezone_closure_t *)clos;

	DO_CBS(CB_SAFEZONE, closure->arena, SafeZoneFunc, (closure->p, closure->x, closure->y, closure->entering));

	afree(closure);

	return FALSE;
}

local int run_spawn_cb(void *clos)
{
	Player *p = (Player *)clos;
	pd->Lock();
	/* check is_dead to make sure that someone else hasn't
	 * already done the CB_SPAWN call. */
	if (p->flags.is_dead)
	{
		p->flags.is_dead = 0;
		DO_CBS(CB_SPAWN, p->arena, SpawnFunc, (p, SPAWN_AFTERDEATH));
	}
	pd->Unlock();
	return FALSE;
}

local void handle_ppk(Player *p, struct C2SPosition *pos, int len, int isfake)
{
	byte *pkt = (byte*)pos;
	Arena *arena = p->arena;
	adata *adata = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey), *idata;
	int sendwpn = FALSE, sendtoall = FALSE, x1, y1, nflags;
	int randnum = prng->Rand();
	Player *i;
	Link *link, *alink;
	ticks_t gtc = current_ticks();
	int latency, isnewer;
	int modified, wpndirty, posdirty;
	struct C2SPosition copy;
	struct S2CWeapons wpn;
	struct S2CPosition sendpos;
	LinkedList advisers = LL_INITIALIZER;
	Appk *ppkadviser;
	int drop;

#ifdef CFG_RELAX_LENGTH_CHECKS
	if (len < 22)
#else
	if (len != 22 && len != 32)
#endif
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad position packet len=%i", len);
		return;
	}

	latency = TICK_DIFF(gtc, pos->time);
	if (latency < 0) latency = 0;
	if (latency > 255) latency = 255;

	/* handle common errors */
	if (!arena || arena->status != ARENA_RUNNING || p->status != S_PLAYING) return;

	/* do checksum */
	if (!isfake)
	{
		byte checksum = 0;
		int left = 22;
		while (left--)
			checksum ^= pkt[left];
		if (checksum != 0)
		{
			lm->LogP(L_MALICIOUS, "game", p, "bad position packet checksum");
			return;
		}
	}

	if (pos->x == -1 && pos->y == -1)
	{
		/* position sent after death, before respawn. these aren't
		 * really useful for anything except making sure the server
		 * knows the client hasn't dropped, so just ignore them. */
		return;
	}

	isnewer = TICK_DIFF(pos->time, data->pos.time) > 0;

	/* lag data */
	if (lagc && !isfake)
		lagc->Position(
				p,
				TICK_DIFF(gtc, pos->time) * 10,
				len >= 26 ? pos->extra.s2cping * 10 : -1,
				data->wpnsent);

	/* only copy if the new one is later */
	if (isnewer || isfake)
	{
		/* call the safety zone callback asynchronously */
		if (((pos->status ^ data->pos.status) & STATUS_SAFEZONE) && !isfake)
		{
			safezone_closure_t *closure = amalloc(sizeof(*closure));
			closure->arena = arena;
			closure->p = p;
			closure->x = pos->x;
			closure->y = pos->y;
			closure->entering = pos->status & STATUS_SAFEZONE;
			ml->SetTimer(run_safezone_cb, 0, 0, closure, NULL);
		}

		/* copy the whole thing. this will copy the epd, or, if the client
		 * didn't send any epd, it will copy zeros because the buffer was
		 * zeroed before data was recvd into it. */
		memcpy(&data->pos, pkt, sizeof(data->pos));

		/* update position in global player struct.
		 * only copy x/y if they are nonzero, so we keep track of last
		 * non-zero position. */
		if (pos->x != 0 || pos->y != 0)
		{
			p->position.x = pos->x;
			p->position.y = pos->y;
		}
		p->position.xspeed = pos->xspeed;
		p->position.yspeed = pos->yspeed;
		p->position.rotation = pos->rotation;
		p->position.bounty = pos->bounty;
		p->position.status = pos->status;
		p->position.energy = pos->energy;
		p->position.time = pos->time;
	}

	/* see if this is their first packet */
	if (p->flags.sent_ppk == 0 && !isfake)
	{
		p->flags.sent_ppk = 1;
		ml->SetTimer(run_enter_game_cb, 0, 0, p, NULL);
	}

	/* speccers don't get their position sent to anyone */
	if (p->p_ship != SHIP_SPEC)
	{
		x1 = pos->x;
		y1 = pos->y;

		/* update region-based stuff once in a while, for real players only */
		if (isnewer && !isfake &&
		    TICK_DIFF(gtc, data->lastrgncheck) >= adata->regionchecktime)
		{
			update_regions(p, x1 >> 4, y1 >> 4);
			data->lastrgncheck = gtc;
		}

		/* this check should be before the weapon ignore hook */
		if (pos->weapon.type)
		{
			p->flags.sent_wpn = 1;
			data->deathwofiring = 0;
		}

		/* this is the weapons ignore hook. also ignore weapons based on
		 * region. */
		if ((prng->Rand() < data->ignoreweapons) ||
		    data->rgnnoweapons)
			pos->weapon.type = 0;

		/* also turn off anti based on region */
		if (data->rgnnoanti)
			pos->status &= ~STATUS_ANTIWARP;

		/* if this is a plain position packet with no weapons, and is in
		 * the wrong order, there's no need to send it. but fake players
		 * never got data->pos.time initialized correctly, so do a
		 * little special case. */
		if (!isnewer && !isfake && pos->weapon.type == 0)
			return;

		/* consult the PPK advisers to allow other modules to edit the packet */
		mm->GetAdviserList(A_PPK, arena, &advisers);
		FOR_EACH(&advisers, ppkadviser, alink)
		{
			if (ppkadviser->EditPPK)
			{
				ppkadviser->EditPPK(p, pos);

				// allow advisers to drop the position packet
				if (pos->x == -1 && pos->y == -1)
				{
					mm->ReleaseAdviserList(&advisers);
					return;
				}
			}
		}
		/* NOTE: the adviser list is released at the end of the function */

		/* by default, send unreliable droppable packets. weapons get a
		 * higher priority. */
		nflags = NET_UNRELIABLE | NET_DROPPABLE |
			(pos->weapon.type ? NET_PRI_P5 : NET_PRI_P3);

		/* there are several reasons to send a weapon packet (05) instead of
		 * just a position one (28) */
		/* if there's a real weapon */
		if (pos->weapon.type > 0)
			sendwpn = TRUE;
		/* if the bounty is over 255 */
		if (pos->bounty & 0xFF00)
			sendwpn = TRUE;
		/* if the pid is over 255 */
		if (p->pid & 0xFF00)
			sendwpn = TRUE;

		/* send mines to everyone */
		if ( ( pos->weapon.type == W_BOMB ||
		       pos->weapon.type == W_PROXBOMB) &&
		     pos->weapon.alternate)
			sendtoall = TRUE;

		/* disable antiwarp if they're in a safe and nosafeanti is on */
		if ((pos->status & STATUS_ANTIWARP) &&
		     (pos->status & STATUS_SAFEZONE) &&
		     adata->nosafeanti)
		{
			pos->status &= ~STATUS_ANTIWARP;
		}

		/* send some percent of antiwarp positions to everyone */
		if ( pos->weapon.type == 0 &&
		     (pos->status & STATUS_ANTIWARP) &&
		     prng->Rand() < adata->cfg_sendanti)
			sendtoall = TRUE;

		/* send safe zone enters to everyone, reliably */
		if ((pos->status & STATUS_SAFEZONE) &&
		    !(p->position.status & STATUS_SAFEZONE))
		{
			sendtoall = TRUE;
			nflags = NET_RELIABLE;
		}

		/* send flashes to everyone, reliably */
		if (pos->status & STATUS_FLASH)
		{
			sendtoall = TRUE;
			nflags = NET_RELIABLE;
		}

		/* ensure that all packets get built before use */
		modified = 1;
		wpndirty = 1;
		posdirty = 1;

		pd->Lock();

		/* have to do this check inside pd->Lock(); */
		/* ignore packets from the first 500ms of death, and accept packets up to 500ms
		 * before their expected respawn. */
		if (p->flags.is_dead && TICK_DIFF(gtc, p->last_death) >= 50 && TICK_DIFF(p->next_respawn, gtc) <= 50)
		{
			/* setup the CB_SPAWN callback to run asynchronously. */
			ml->SetTimer(run_spawn_cb, 0, 0, p, NULL);
		}

		FOR_EACH_PLAYER_P(i, idata, pdkey)
			if (i->status == S_PLAYING &&
				IS_STANDARD(i) &&
				i->arena == arena &&
				(i != p || p->flags.see_own_posn))
			{
				long dist = lhypot(x1 - idata->pos.x, y1 - idata->pos.y);
				long range;

				/* determine the packet range */
				if (sendwpn && pos->weapon.type)
					range = adata->wpnrange[pos->weapon.type];
				else
					range = i->xres + i->yres;

				if (
						dist <= range ||
						sendtoall ||
						/* send it always to specers */
						idata->speccing == p ||
						/* send it always to turreters */
						i->p_attached == p->pid ||
						/* and send some radar packets */
						(pos->weapon.type == W_NULL &&
							dist <= adata->cfg_pospix &&
							randnum > ((double)dist / (double)adata->cfg_pospix * (RAND_MAX+1.0))) ||
						/* bots */
						i->flags.see_all_posn)
				{
					int extralen;

					const int plainlen = 0;
					const int nrglen = 2;
					const int epdlen = sizeof(struct ExtraPosData);

					if (i->p_ship == SHIP_SPEC)
					{
						if (idata->pl_epd.seeepd && idata->speccing == p)
						{
							if (len >= 32)
								extralen = epdlen;
							else
								extralen = nrglen;
						}
						else if (idata->pl_epd.seenrgspec == SEE_ALL ||
								 (idata->pl_epd.seenrgspec == SEE_TEAM &&
								  p->p_freq == i->p_freq) ||
								 (idata->pl_epd.seenrgspec == SEE_SPEC &&
								  data->speccing == p))
							extralen = nrglen;
						else
							extralen = plainlen;
					}
					else if (idata->pl_epd.seenrg == SEE_ALL ||
							 (idata->pl_epd.seenrg == SEE_TEAM &&
							  p->p_freq == i->p_freq))
						extralen = nrglen;
					else
						extralen = plainlen;

					if (modified)
					{
						memcpy(&copy, pos, sizeof(struct C2SPosition));
						modified = 0;
					}
					drop = 0;

					/* consult the ppk advisers to allow other modules to edit the
					 * packet going to player i */
					FOR_EACH(&advisers, ppkadviser, alink)
					{
						if (ppkadviser->EditIndividualPPK)
						{
							modified |= ppkadviser->EditIndividualPPK(p, i, &copy, &extralen);

							// allow advisers to drop the packet
							if (copy.x == -1 && copy.y == -1)
							{
								drop = 1;
								break;
							}
						}
					}
					wpndirty = wpndirty || modified;
					posdirty = posdirty || modified;

					if (!drop)
					{
						if ((!modified && sendwpn)
							|| copy.weapon.type > 0
							|| copy.bounty & 0xFF00
							|| p->pid & 0xFF00)
						{
							int length = sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData) + extralen;
							if (wpndirty)
							{
								wpn.type = S2C_WEAPON;
								wpn.rotation = copy.rotation;
								wpn.time = gtc & 0xFFFF;
								wpn.x = copy.x;
								wpn.yspeed = copy.yspeed;
								wpn.playerid = p->pid;
								wpn.xspeed = copy.xspeed;
								wpn.checksum = 0;
								wpn.status = copy.status;
								wpn.c2slatency = (u8)latency;
								wpn.y = copy.y;
								wpn.bounty = copy.bounty;
								wpn.weapon = copy.weapon;
								wpn.extra = copy.extra;
								/* move this field from the main packet to the extra data,
								 * in case they don't match. */
								wpn.extra.energy = copy.energy;

								wpndirty = modified;

								DoWeaponChecksum(&wpn);
							}

							if (wpn.weapon.type)
								idata->wpnsent++;

							net->SendToOne(i, (byte*)&wpn, length, nflags);
						}
						else
						{
							int length = sizeof(struct S2CPosition) - sizeof(struct ExtraPosData) + extralen;
							if (posdirty)
							{
								sendpos.type = S2C_POSITION;
								sendpos.rotation = copy.rotation;
								sendpos.time = gtc & 0xFFFF;
								sendpos.x = copy.x;
								sendpos.c2slatency = (u8)latency;
								sendpos.bounty = (u8)copy.bounty;
								sendpos.playerid = (u8)p->pid;
								sendpos.status = copy.status;
								sendpos.yspeed = copy.yspeed;
								sendpos.y = copy.y;
								sendpos.xspeed = copy.xspeed;
								sendpos.extra = copy.extra;
								/* move this field from the main packet to the extra data,
								 * in case they don't match. */
								sendpos.extra.energy = copy.energy;

								posdirty = modified;
							}

							net->SendToOne(i, (byte*)&sendpos, length, nflags);
						}
					}
				}
			}
		pd->Unlock();
		mm->ReleaseAdviserList(&advisers);

		/* do the position packet callback */
		DO_CBS(CB_PPK, arena, PPKFunc, (p, pos));
	}
}

local void Pppk(Player *p, byte *pkt, int len)
{
	handle_ppk(p, (struct C2SPosition *)pkt, len, 0);
}

local void FakePosition(Player *p, struct C2SPosition *pos, int len)
{
	handle_ppk(p, pos, len, 1);
}

local int IsAntiwarped(Player *p, LinkedList *players)
{
	pdata *data = PPDATA(p, pdkey);
	adata *adata = P_ARENA_DATA(p->arena, adkey);
	pdata *idata;
	Player *i;
	Link *link;
	int antiwarped = 0;

	if (!data->rgnnorecvanti)
	{
		pd->Lock();
		FOR_EACH_PLAYER_P(i, idata, pdkey)
		{
			if (i->arena == p->arena && i->p_freq != p->p_freq && i->p_ship != SHIP_SPEC
					&& (i->position.status & STATUS_ANTIWARP) && !idata->rgnnoanti
					&& (!(i->position.status & STATUS_SAFEZONE) || !adata->nosafeanti))
			{
				int xdelta = (i->position.x - p->position.x);
				int ydelta = (i->position.y - p->position.y);
				int distSquared = (xdelta * xdelta + ydelta * ydelta);
				int antiwarpRange = cfg->GetInt(p->arena->cfg, "Toggle", "AntiwarpPixels", 1);

				if (distSquared < antiwarpRange * antiwarpRange)
				{
					antiwarped = 1;
					if (players)
					{
						LLAdd(players, i);
					}
					else
					{
						break;
					}
				}
			}
		}
		pd->Unlock();
	}

	return antiwarped;
}

int RegionPacketEditor(Player *p, Player *t, struct C2SPosition *pos, int *extralen)
{
	int modified = 0;
	pdata *data = PPDATA(t, pdkey);
	if (data->rgnnorecvanti && (pos->status & STATUS_ANTIWARP))
	{
		pos->status &= ~STATUS_ANTIWARP;
		modified = 1;
	}
	if (data->rgnnorecvweps && pos->weapon.type)
	{
		pos->weapon.type = 0;
		modified = 1;
	}
	return modified;
}

/* call with specmtx locked */
local void clear_speccing(pdata *data)
{
	if (data->speccing)
	{
		if (data->pl_epd.seeepd)
		{
			pdata *odata = PPDATA(data->speccing, pdkey);
			if (--odata->epd_queries <= 0)
			{
				byte pkt[2] = { S2C_SPECDATA, 0 };
				net->SendToOne(data->speccing, pkt, 2, NET_RELIABLE);
				odata->epd_queries = 0;
			}
		}

		data->speccing = NULL;
	}
}

/* call with specmtx locked */
local void add_speccing(pdata *data, Player *t)
{
	data->speccing = t;

	if (data->pl_epd.seeepd)
	{
		pdata *tdata = PPDATA(t, pdkey);
		if (tdata->epd_queries++ == 0)
		{
			byte pkt[2] = { S2C_SPECDATA, 1 };
			net->SendToOne(t, pkt, 2, NET_RELIABLE);
		}
	}
}


local void PSpecRequest(Player *p, byte *pkt, int len)
{
	pdata *data = PPDATA(p, pdkey);
	int tpid = ((struct SimplePacket*)pkt)->d1;

	if (len != 3)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad spec req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || p->p_ship != SHIP_SPEC)
		return;

	pthread_mutex_lock(&specmtx);

	clear_speccing(data);

	if (tpid >= 0)
	{
		Player *t = pd->PidToPlayer(tpid);
		if (t && t->status == S_PLAYING && t->p_ship != SHIP_SPEC && t->arena == p->arena)
			add_speccing(data, t);
	}

	pthread_mutex_unlock(&specmtx);
}


/* ?spec command */

local helptext_t spec_help =
"Targets: any\n"
"Args: none\n"
"Displays players spectating you. When private, displays players\n"
"spectating the target.\n";

local void Cspec(const char *cmd, const char *params, Player *p, const Target *target)
{
	StringBuffer sb;
	int scnt = 0;
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;
	Player *pp;
	pdata *data;
	Link *link;

	SBInit(&sb);
	pd->Lock();
	FOR_EACH_PLAYER_P(pp, data, pdkey)
		if (data->speccing == t &&
		    (!capman->HasCapability(pp, CAP_INVISIBLE_SPECTATOR) ||
		     capman->HigherThan(p, pp)))
		{
			scnt++;
			SBPrintf(&sb, ", %s", pp->name);
		}
	pd->Unlock();

	if (scnt > 1)
	{
		chat->SendMessage(p, "%d spectators:", scnt);
		chat->SendWrappedText(p, SBText(&sb, 2));
	}
	else if (scnt == 1)
		chat->SendMessage(p, "1 spectator: %s", SBText(&sb, 2));
	else if (p == t)
		chat->SendMessage(p, "No players are spectating you.");
	else
		chat->SendMessage(p, "No players are spectating %s.", t->name);
	SBDestroy(&sb);
}


/* ?energy command */

local helptext_t energy_help =
"Targets: arena or player\n"
"Args: [-t] [-n] [-s]\n"
"If sent as a priv message, turns energy viewing on for that player.\n"
"If sent as a pub message, turns energy viewing on for the whole arena\n"
"(note that this will only affect new players entering the arena).\n"
"If {-t} is given, turns energy viewing on for teammates only.\n"
"If {-n} is given, turns energy viewing off.\n"
"If {-s} is given, turns energy viewing on/off for spectator mode.\n";

local void Cenergy(const char *cmd, const char *params, Player *p, const Target *target)
{
	Player *t = target->type == T_PLAYER ? target->u.p : NULL;
	int nval = SEE_ALL;
	int spec = FALSE;

	if (strstr(params, "-t"))
		nval = SEE_TEAM;
	if (strstr(params, "-n"))
		nval = SEE_NONE;
	if (strstr(params, "-s"))
		spec = TRUE;

	if (t)
	{
		pdata *data = PPDATA(t, pdkey);
		if (spec)
			data->pl_epd.seenrgspec = nval;
		else
			data->pl_epd.seenrg = nval;
	}
	else
	{
		adata *ad = P_ARENA_DATA(p->arena, adkey);
		if (spec)
			ad->spec_nrg = nval;
		else
			ad->all_nrg = nval;
	}
}

local void SetPlayerEnergyViewing(Player *p, int value)
{
	pdata *data = PPDATA(p, pdkey);
	data->pl_epd.seenrg = value;
}

local void SetSpectatorEnergyViewing(Player *p, int value)
{
	pdata *data = PPDATA(p, pdkey);
	data->pl_epd.seenrgspec = value;
}

local void ResetPlayerEnergyViewing(Player *p)
{
	int seenrg = SEE_NONE;
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(p->arena, adkey);

	if (ad->all_nrg)  seenrg = ad->all_nrg;
	if (capman && capman->HasCapability(p, "seenrg"))
	{
		seenrg = SEE_ALL;
	}

	data->pl_epd.seenrg = seenrg;
}

local void ResetSpectatorEnergyViewing(Player *p)
{
	int seenrgspec = SEE_NONE;
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(p->arena, adkey);

	if (ad->spec_nrg)  seenrgspec = ad->spec_nrg;
	if (capman && capman->HasCapability(p, "seenrg"))
	{
		seenrgspec = SEE_ALL;
	}

	data->pl_epd.seenrgspec = seenrgspec;
}

/* call with freqshipmtx lock held */
local void expire_lock(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	if (data->expires > 0)
		if (time(NULL) > data->expires)
		{
			data->lockship = FALSE;
			data->expires = 0;
			lm->LogP(L_DRIVEL, "game", p, "lock expired");
		}
}


local void reset_during_change(Player *p, int success, void *dummy)
{
	pthread_mutex_lock(&freqshipmtx);
	p->flags.during_change = 0;
	pthread_mutex_unlock(&freqshipmtx);
}


local void SetShipAndFreq(Player *p, int ship, int freq)
{
	pdata *data = PPDATA(p, pdkey);
	struct ShipChangePacket to = { S2C_SHIPCHANGE, ship, p->pid, freq };
	Arena *arena = p->arena;
	int oldship = p->p_ship;
	int oldfreq = p->p_freq;
	int flags = SPAWN_SHIPCHANGE;

	if (p->type == T_CHAT && ship != SHIP_SPEC)
	{
		lm->LogP(L_WARN, "game", p, "someone tried to forced chat client into playing ship");
		return;
	}

	if (freq < 0 || freq > 9999 || ship < 0 || ship > SHIP_SPEC)
		return;

	pthread_mutex_lock(&freqshipmtx);

	if (p->p_ship == ship &&
	    p->p_freq == freq)
	{
		/* nothing to do */
		pthread_mutex_unlock(&freqshipmtx);
		return;
	}

	if (IS_STANDARD(p))
		p->flags.during_change = 1;
	p->p_ship = ship;
	p->p_freq = freq;
	pthread_mutex_lock(&specmtx);
	clear_speccing(data);
	pthread_mutex_unlock(&specmtx);

	pthread_mutex_unlock(&freqshipmtx);

	/* send it to him, with a callback */
	if (IS_STANDARD(p))
		net->SendToOneWithCallback(p, (byte*)&to, 6, NET_RELIABLE, reset_during_change, NULL);
	/* sent it to everyone else */
	net->SendToArena(arena, p, (byte*)&to, 6, NET_RELIABLE);
	if (chatnet)
		chatnet->SendToArena(arena, NULL, "SHIPFREQCHANGE:%s:%d:%d",
				p->name, p->p_ship, p->p_freq);

	DO_CBS(CB_PRESHIPFREQCHANGE, arena, PreShipFreqChangeFunc,
			(p, ship, oldship, freq, oldfreq));
	DO_CBS(CB_SHIPFREQCHANGE, arena, ShipFreqChangeFunc,
			(p, ship, oldship, freq, oldfreq));

	/* now setup for the CB_SPAWN callback. */
	pd->Lock();
	if (p->flags.is_dead)
	{
		flags |= SPAWN_AFTERDEATH;
	}
	/* a shipchange will revive a dead player. */
	p->flags.is_dead = 0;
	pd->Unlock();

	if (ship != SHIP_SPEC)
	{
		/* flags = SPAWN_SHIPCHANGE set at the top of the function */
		if (oldship == SHIP_SPEC)
			flags |= SPAWN_INITIAL;
		DO_CBS(CB_SPAWN, arena, SpawnFunc, (p, flags));
	}

	lm->LogP(L_INFO, "game", p, "changed ship/freq to ship %d, freq %d",
			ship, freq);
}

local void SetShip(Player *p, int ship)
{
	SetShipAndFreq(p, ship, p->p_freq);
}

local void PSetShip(Player *p, byte *pkt, int len)
{
	pdata *data = PPDATA(p, pdkey);
	Arena *arena = p->arena;
	int ship = pkt[1];
	Ifreqman *fm;

	if (len != 2)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad ship req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
	{
		lm->LogP(L_WARN, "game", p, "state sync problem: ship request from bad status");
		return;
	}

	if (ship < SHIP_WARBIRD || ship > SHIP_SPEC)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad ship number: %d", ship);
		return;
	}

	pthread_mutex_lock(&freqshipmtx);

	if (p->flags.during_change)
	{
		lm->LogP(L_WARN, "game", p, "state sync problem: ship request before ack from previous change");
		pthread_mutex_unlock(&freqshipmtx);
		return;
	}

	if (ship == p->p_ship)
	{
		lm->LogP(L_WARN, "game", p, "state sync problem: already in requested ship");
		pthread_mutex_unlock(&freqshipmtx);
		return;
	}

	/* do this bit while holding the mutex. it's ok to check the flag
	 * afterwards, though. */
	expire_lock(p);

	pthread_mutex_unlock(&freqshipmtx);

	/* checked lock state (but always allow switching to spec) */
	if (data->lockship &&
	    ship != SHIP_SPEC &&
	    !(capman && capman->HasCapability(p, "bypasslock")))
	{
		if (chat)
			chat->SendMessage(p, "You have been locked in %s.",
					(p->p_ship == SHIP_SPEC) ? "spectator mode" : "your ship");
		return;
	}

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		char err_buf[200];
		err_buf[0] = '\0';
		fm->ShipChange(p, ship, err_buf, sizeof(err_buf));
		mm->ReleaseInterface(fm);
		if (chat && err_buf[0] != '\0')
			chat->SendMessage(p, "%s", err_buf);
	}
	else
	{
		SetShipAndFreq(p, ship, p->p_freq);
	}
}


local void SetFreq(Player *p, int freq)
{
	struct SimplePacket to = { S2C_FREQCHANGE, p->pid, freq, -1};
	Arena *arena = p->arena;
	int oldfreq = p->p_freq;

	if (freq < 0 || freq > 9999)
		return;

	pthread_mutex_lock(&freqshipmtx);

	if (p->p_freq == freq)
	{
		pthread_mutex_unlock(&freqshipmtx);
		return;
	}

	if (IS_STANDARD(p))
		p->flags.during_change = 1;
	p->p_freq = freq;

	pthread_mutex_unlock(&freqshipmtx);

	/* him, with callback */
	if (IS_STANDARD(p))
		net->SendToOneWithCallback(p, (byte*)&to, 6, NET_RELIABLE, reset_during_change, NULL);
	/* everyone else */
	net->SendToArena(arena, p, (byte*)&to, 6, NET_RELIABLE);
	if (chatnet)
		chatnet->SendToArena(arena, NULL, "SHIPFREQCHANGE:%s:%d:%d",
				p->name, p->p_ship, p->p_freq);

	DO_CBS(CB_PRESHIPFREQCHANGE, arena, PreShipFreqChangeFunc, (p, p->p_ship, p->p_ship, freq, oldfreq));
	DO_CBS(CB_SHIPFREQCHANGE, arena, ShipFreqChangeFunc, (p, p->p_ship, p->p_ship, freq, oldfreq));

	lm->LogP(L_INFO, "game", p, "changed freq to %d", freq);
}


local void freq_change_request(Player *p, int freq)
{
	pdata *data = PPDATA(p, pdkey);
	Arena *arena = p->arena;
	Ifreqman *fm;

	if (p->status != S_PLAYING || !arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "freq change from bad arena");
		return;
	}

	/* checked lock state */
	pthread_mutex_lock(&freqshipmtx);
	expire_lock(p);
	pthread_mutex_unlock(&freqshipmtx);

	if (data->lockship &&
	    !(capman && capman->HasCapability(p, "bypasslock")))
	{
		if (chat)
			chat->SendMessage(p, "You have been locked in %s.",
					(p->p_ship == SHIP_SPEC) ? "spectator mode" : "your ship");
		return;
	}

	fm = mm->GetInterface(I_FREQMAN, arena);
	if (fm)
	{
		char err_buf[200];
		err_buf[0] = '\0';
		fm->FreqChange(p, freq, err_buf, sizeof(err_buf));
		mm->ReleaseInterface(fm);
		if (chat && err_buf[0] != '\0')
			chat->SendMessage(p, "%s", err_buf);
	}
	else
	{
		SetFreq(p, freq);
	}
}


local void PSetFreq(Player *p, byte *pkt, int len)
{
	if (len != 3)
		lm->LogP(L_MALICIOUS, "game", p, "bad freq req packet len=%i", len);
	else if (p->flags.during_change)
		lm->LogP(L_WARN, "game", p, "state sync problem: freq change before ack from previous change");
	else
		freq_change_request(p, ((struct SimplePacket*)pkt)->d1);
}


local void MChangeFreq(Player *p, const char *line)
{
	freq_change_request(p, strtol(line, NULL, 0));
}


local void notify_kill(Player *killer, Player *killed, int bty, int flags, int green)
{
	struct KillPacket kp = { S2C_KILL, green, killer->pid, killed->pid, bty, flags };
	net->SendToArena(killer->arena, NULL, (byte*)&kp, sizeof(kp), NET_RELIABLE);
	if (chatnet)
		chatnet->SendToArena(killer->arena, NULL, "KILL:%s:%s:%d:%d",
				killer->name, killed->name, bty, flags);
}

local void PDie(Player *p, byte *pkt, int len)
{
	struct SimplePacket *dead = (struct SimplePacket*)pkt;
	int bty = dead->d2, pts = 0;
	int flagcount, green;
	int enterdelay;
	Arena *arena = p->arena;
	Player *killer;
	LinkedList advisers = LL_INITIALIZER;
	Link *alink;
	Akill *killAdviser;
	Ikillgreen *killgreen;
	ticks_t ct = current_ticks();

	if (len != 5)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad death packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "death packet from bad state");
		return;
	}

	killer = pd->PidToPlayer(dead->d1);
	if (!killer || killer->status != S_PLAYING || killer->arena != arena)
	{
		lm->LogP(L_MALICIOUS, "game", p, "reported kill by bad pid %d", dead->d1);
		return;
	}

	flagcount = p->pkt.flagscarried;

	/* these flags are primarily for the benefit of other modules */
	pd->Lock();
	p->flags.is_dead = 1;
	/* continuum clients take EnterDelay + 100 ticks to respawn after death */
	enterdelay = cfg->GetInt(arena->cfg, "Kill", "EnterDelay", 0) + 100;
	/* setting of 0 or less means respawn in place, with 1 second delay */
	if (enterdelay <= 0)
		enterdelay = 100;
	p->last_death = ct;
	p->next_respawn = TICK_MAKE(ct + enterdelay);
	pd->Unlock();

	/* Consult the advisers after setting the above flags, the flags reflect the real state of the player */
	mm->GetAdviserList(A_KILL, arena, &advisers);
	FOR_EACH(&advisers, killAdviser, alink)
	{
		if (killAdviser->EditDeath)
		{
			killAdviser->EditDeath(arena, &killer, &p, &bty);

			if (!p || !killer) // The advisor wants to drop the kill packet
			{
				mm->ReleaseAdviserList(&advisers);
				return;
			}

			if (p->status != S_PLAYING || p->arena != arena)
			{
				lm->LogP(L_ERROR, "game", p, "An A_KILL adviser set killed to a bad player");
				mm->ReleaseAdviserList(&advisers);
				return;
			}

			if (killer->status != S_PLAYING || killer->arena != arena)
			{
				lm->LogP(L_ERROR, "game", p, "An A_KILL adviser set killer to a bad player");
				mm->ReleaseAdviserList(&advisers);
				return;
			}
		}
	}


	/* pick the green */
	/* cfghelp: Prize:UseTeamkillPrize, arena, int, def: 0
	 * Whether to use a special prize for teamkills.
	 * Prize:TeamkillPrize specifies the prize #. */
	if (p->p_freq == killer->p_freq && cfg->GetInt(arena->cfg, "Prize", "UseTeamkillPrize", 0))
	{
		/* cfghelp: Prize:TeamkillPrize, arena, int, def: 0
		 * The prize # to give for a teamkill, if
		 * Prize:UseTeamkillPrize=1. */
		green = cfg->GetInt(arena->cfg, "Prize", "TeamkillPrize", 0);
	}
	else
	{
		/* pick a random green */
		Iclientset *cset = mm->GetInterface(I_CLIENTSET, arena);
		green = cset ? cset->GetRandomPrize(arena) : 0;
		mm->ReleaseInterface(cset);
	}

	/* this will figure out how many points to send in the packet */
	FOR_EACH(&advisers, killAdviser, alink)
	{
		if (killAdviser->KillPoints)
		{
			pts += killAdviser->KillPoints(arena, killer, p, bty, flagcount);
		}
	}

	/* allow a module to modify the green sent in the packet */
	killgreen = mm->GetInterface(I_KILL_GREEN, arena);
	if (killgreen)
	{
		green = killgreen->KillGreen(arena, killer, p, bty, flagcount, pts, green);
	}
	mm->ReleaseInterface(killgreen);

	/* record the kill points on our side */
	if (pts)
	{
		Istats *stats = mm->GetInterface(I_STATS, arena);
		if (stats) stats->IncrementStat(killer, STAT_KILL_POINTS, pts);
		mm->ReleaseInterface(stats);
	}

	notify_kill(killer, p, pts, flagcount, green);

	DO_CBS(CB_KILL, arena, KillFunc,
	                (arena, killer, p, bty, flagcount, &pts, &green));

	lm->Log(L_INFO, "<game> {%s} [%s] killed by [%s] (bty=%d,flags=%d,pts=%d)",
			arena->name,
			p->name,
			killer->name,
			bty,
			flagcount,
			pts);

	if (!p->flags.sent_wpn)
	{
		pdata *data = PPDATA(p, pdkey);
		adata *ad = P_ARENA_DATA(arena, adkey);
		if (data->deathwofiring++ == ad->deathwofiring)
		{
			lm->LogP(L_INFO, "game", p, "specced for too many deaths without firing");
			SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
		}
	}

	/* reset this so we can accurately check deaths without firing */
	p->flags.sent_wpn = 0;

	mm->ReleaseAdviserList(&advisers);
}

local void FakeKill(Player *killer, Player *killed, int pts, int flags)
{
	notify_kill(killer, killed, pts, flags, 0);
}


local void PGreen(Player *p, byte *pkt, int len)
{
	struct GreenPacket *g = (struct GreenPacket *)pkt;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (len != 11)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad green packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
		return;

	/* don't forward non-shared prizes */
	if (!(g->green == PRIZE_THOR  && (ad->personalgreen & (1<<personal_thor))) &&
	    !(g->green == PRIZE_BURST && (ad->personalgreen & (1<<personal_burst))) &&
	    !(g->green == PRIZE_BRICK && (ad->personalgreen & (1<<personal_brick))))
	{
		g->pid = p->pid;
		g->type = S2C_GREEN; /* HACK :) */
		net->SendToArena(arena, p, pkt, sizeof(struct GreenPacket), NET_UNRELIABLE);
		g->type = C2S_GREEN;
	}

	DO_CBS(CB_GREEN, arena, GreenFunc, (p, g->x, g->y, g->green));
}


local void PAttach(Player *p, byte *pkt2, int len)
{
	int pid2 = ((struct SimplePacket*)pkt2)->d1;
	Arena *arena = p->arena;
	Player *to = NULL;

	if (len != 3)
	{
		lm->LogP(L_MALICIOUS, "game", p, "bad attach req packet len=%i", len);
		return;
	}

	if (p->status != S_PLAYING || !arena)
		return;

	if (pid2 != -1)
	{
		to = pd->PidToPlayer(pid2);
		if (!to ||
		    to->status != S_PLAYING ||
		    to == p ||
		    p->arena != to->arena ||
		    p->p_freq != to->p_freq)
		{
			lm->LogP(L_MALICIOUS, "game", p, "tried to attach to bad pid %d", pid2);
			return;
		}
	}

	/* only send it if state has changed */
	if (p->p_attached != pid2)
	{
		struct SimplePacket pkt = { S2C_TURRET, p->pid, pid2 };
		net->SendToArena(arena, NULL, (byte*)&pkt, 5, NET_RELIABLE);
		p->p_attached = pid2;

		DO_CBS(CB_ATTACH, p->arena, AttachFunc, (p, to));
	}
}


local void PKickoff(Player *p, byte *pkt2, int len)
{
	struct SimplePacket pkt = { S2C_TURRETKICKOFF, p->pid };

	if (p->status == S_PLAYING)
		net->SendToArena(p->arena, NULL, (byte*)&pkt, 3, NET_RELIABLE);
}


local void WarpTo(const Target *target, int x, int y)
{
	struct SimplePacket wto = { S2C_WARPTO, x, y };
	net->SendToTarget(target, (byte *)&wto, 5, NET_RELIABLE | NET_URGENT);
}


local void GivePrize(const Target *target, int type, int count)
{
	struct SimplePacket prize = { S2C_PRIZERECV, (short)count, (short)type };
	net->SendToTarget(target, (byte*)&prize, 5, NET_RELIABLE);
}


local void PlayerAction(Player *p, int action, Arena *arena)
{
	pdata *data = PPDATA(p, pdkey), *idata;
	adata *ad = P_ARENA_DATA(arena, adkey);

	if (action == PA_PREENTERARENA)
	{
		/* clear the saved ppk, but set time to the present so that new
		 * position packets look like they're in the future. also set a
		 * bunch of other timers to now. */
		memset(&data->pos, 0, sizeof(data->pos));
		data->pos.time = data->lastrgncheck = current_ticks();

		LLInit(&data->lastrgnset);

		data->lockship = ad->initlockship;
		if (ad->initspec)
		{
			p->p_ship = SHIP_SPEC;
			p->p_freq = arena->specfreq;
		}
		p->p_attached = -1;

		pd->Lock();
		p->flags.is_dead = 0;
		p->last_death = 0;
		p->next_respawn = 0;
		pd->Unlock();
	}
	else if (action == PA_ENTERARENA)
	{
		int seenrg = SEE_NONE, seenrgspec = SEE_NONE, seeepd = SEE_NONE;

		if (ad->all_nrg)  seenrg = ad->all_nrg;
		if (ad->spec_nrg) seenrgspec = ad->spec_nrg;
		if (ad->spec_epd) seeepd = TRUE;
		if (capman && capman->HasCapability(p, "seenrg"))
			seenrg = seenrgspec = SEE_ALL;
		if (capman && capman->HasCapability(p, "seeepd"))
			seeepd = TRUE;

		data->pl_epd.seenrg = seenrg;
		data->pl_epd.seenrgspec = seenrgspec;
		data->pl_epd.seeepd = seeepd;
		data->epd_queries = 0;

		data->wpnsent = 0;
		data->deathwofiring = 0;
		p->flags.sent_wpn = 0;
	}
	else if (action == PA_LEAVEARENA)
	{
		Link *link;
		Player *i;

		pthread_mutex_lock(&specmtx);

		pd->Lock();
		FOR_EACH_PLAYER_P(i, idata, pdkey)
			if (idata->speccing == p)
				clear_speccing(idata);
		pd->Unlock();

		if (data->epd_queries > 0)
			lm->LogP(L_ERROR, "game", p, "epd_queries is still nonzero");

		clear_speccing(data);

		pthread_mutex_unlock(&specmtx);

		LLEmpty(&data->lastrgnset);
	}
	else if (action == PA_ENTERGAME)
	{
		if (p->p_ship != SHIP_SPEC)
		{
			DO_CBS(CB_SPAWN, arena, SpawnFunc, (p, SPAWN_INITIAL));
		}
	}
}


local void NewPlayer(Player *p, int new)
{
	if (p->type == T_FAKE && !new)
	{
		/* extra cleanup for fake players since PA_LEAVEARENA isn't
		 * called. fake players can't be speccing anyone else, but other
		 * players can be speccing them. */
		Link *link;
		Player *i;
		pdata *idata;

		pthread_mutex_lock(&specmtx);

		pd->Lock();
		FOR_EACH_PLAYER_P(i, idata, pdkey)
			if (idata->speccing == p)
				clear_speccing(idata);
		pd->Unlock();

		pthread_mutex_unlock(&specmtx);
	}
}


local void ArenaAction(Arena *arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		unsigned int pg = 0;
		adata *ad = P_ARENA_DATA(arena, adkey);

		/* cfghelp: Misc:RegionCheckInterval, arena, int, def: 100
		 * How often to check for region enter/exit events (in ticks). */
		ad->regionchecktime =
			cfg->GetInt(arena->cfg, "Misc", "RegionCheckInterval", 100);
		/* cfghelp: Misc:SpecSeeExtra, arena, bool, def: 1
		 * Whether spectators can see extra data for the person they're
		 * spectating. */
		ad->spec_epd =
			cfg->GetInt(arena->cfg, "Misc", "SpecSeeExtra", 1);
		/* cfghelp: Misc:SpecSeeEnergy, arena, enum, def: SEE_NONE
		 * Whose energy levels spectators can see. The options are the
		 * same as for Misc:SeeEnergy, with one addition: SEE_SPEC
		 * means only the player you're spectating. */
		ad->spec_nrg =
			see_nrg_val(cfg->GetStr(arena->cfg, "Misc", "SpecSeeEnergy"), SEE_ALL);
		/* cfghelp: Misc:SeeEnergy, arena, enum, def: SEE_NONE
		 * Whose energy levels everyone can see: SEE_NONE means nobody
		 * else's, SEE_ALL is everyone's, SEE_TEAM is only teammates. */
		ad->all_nrg =
			see_nrg_val(cfg->GetStr(arena->cfg, "Misc", "SeeEnergy"), SEE_NONE);

		/* cfghelp: Security:MaxDeathWithoutFiring, arena, int, def: 5
		 * The number of times a player can die without firing a weapon
		 * before being placed in spectator mode. */
		ad->deathwofiring =
			cfg->GetInt(arena->cfg, "Security", "MaxDeathWithoutFiring", 5);

		/* cfghelp: Misc:NoSafeAntiwarp, arena, int, def: 0
		 * Disables antiwarp on players in safe zones. */
		ad->nosafeanti =
			cfg->GetInt(arena->cfg, "Misc", "NoSafeAntiwarp", 0);

		/* cfghelp: Prize:DontShareThor, arena, bool, def: 0
		 * Whether Thor greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Prize", "DontShareThor", 0))
			pg |= (1<<personal_thor);
		/* cfghelp: Prize:DontShareBurst, arena, bool, def: 0
		 * Whether Burst greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Prize", "DontShareBurst", 0))
			pg |= (1<<personal_burst);
		/* cfghelp: Prize:DontShareBrick, arena, bool, def: 0
		 * Whether Brick greens don't go to the whole team. */
		if (cfg->GetInt(arena->cfg, "Prize", "DontShareBrick", 0))
			pg |= (1<<personal_brick);

		ad->personalgreen = pg;

		/* cfghelp: Net:BulletPixels, arena, int, def: 1500
		 * How far away to always send bullets (in pixels) */
		int cfg_bulletpix = cfg->GetInt(arena->cfg, "Net", "BulletPixels", 1500);

		/* cfghelp: Net:WeaponPixels, arena, int, def: 2000
		 * How far away to always send weapons (in pixels) */
		int cfg_wpnpix = cfg->GetInt(arena->cfg, "Net", "WeaponPixels", 2000);

		/* cfghelp: Net:PositionExtraPixels, arena, int, def: 8000
		 * How far away to send positions of players on radar */
		ad->cfg_pospix = cfg->GetInt(arena->cfg, "Net", "PositionExtraPixels", 8000);

		/* cfghelp: Net:AntiWarpSendPercent, arena, int, def: 5
		 * Percent of position packets with antiwarp enabled to send to
		 * the whole arena. */
		ad->cfg_sendanti = cfg->GetInt(arena->cfg, "Net", "AntiWarpSendPercent", 5);
		/* convert to a percentage of RAND_MAX */
		ad->cfg_sendanti = RAND_MAX / 100 * ad->cfg_sendanti;

		for (int i = 0; i < WEAPONCOUNT; i++) {
			ad->wpnrange[i] = cfg_wpnpix;
		}

		/* exceptions: */
		ad->wpnrange[W_BULLET] = cfg_bulletpix;
		ad->wpnrange[W_BOUNCEBULLET] = cfg_bulletpix;
		ad->wpnrange[W_THOR] = 30000;

		if (action == AA_CREATE)
			ad->initlockship = ad->initspec = FALSE;
	}
}


/* locking/unlocking players/arena */

local void lock_work(const Target *target, int nval, int notify, int spec, int timeout)
{
	LinkedList set = LL_INITIALIZER;
	Link *l;

	pd->TargetToSet(target, &set);
	for (l = LLGetHead(&set); l; l = l->next)
	{
		Player *p = l->data;
		pdata *pdata = PPDATA(p, pdkey);

		if (spec && p->arena && p->p_ship != SHIP_SPEC)
			SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);

		if (notify && pdata->lockship != nval && chat)
			chat->SendMessage(p, nval ?
					(p->p_ship == SHIP_SPEC ?
					 "You have been locked to spectator mode." :
					 "You have been locked to your ship.") :
					"Your ship has been unlocked.");

		pdata->lockship = nval;
		pdata->expires = (nval == FALSE || timeout == 0) ? 0 : time(NULL) + timeout;
	}
	LLEmpty(&set);
}


local void Lock(const Target *t, int notify, int spec, int timeout)
{
	lock_work(t, TRUE, notify, spec, timeout);
}


local void Unlock(const Target *t, int notify)
{
	lock_work(t, FALSE, notify, FALSE, 0);
}


local void LockArena(Arena *a, int notify, int onlyarenastate, int initial, int spec)
{
	adata *ad = P_ARENA_DATA(a, adkey);

	ad->initlockship = TRUE;
	if (!initial)
		ad->initspec = TRUE;
	if (!onlyarenastate)
	{
		Target t = { T_ARENA };
		t.u.arena = a;
		lock_work(&t, TRUE, notify, spec, 0);
	}
}


local void UnlockArena(Arena *a, int notify, int onlyarenastate)
{
	adata *ad = P_ARENA_DATA(a, adkey);

	ad->initlockship = FALSE;
	ad->initspec = FALSE;
	if (!onlyarenastate)
	{
		Target t = { T_ARENA };
		t.u.arena = a;
		lock_work(&t, FALSE, notify, FALSE, 0);
	}
}


local double GetIgnoreWeapons(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	return data->ignoreweapons / (double)RAND_MAX;
}


local void SetIgnoreWeapons(Player *p, double proportion)
{
	pdata *data = PPDATA(p, pdkey);
	data->ignoreweapons = (int)((double)RAND_MAX * proportion);
}


local void ShipReset(const Target *target)
{
	byte pkt = S2C_SHIPRESET;
	LinkedList list = LL_INITIALIZER;
	Link *link;
	Player *p;

	net->SendToTarget(target, &pkt, 1, NET_RELIABLE);

	pd->Lock();

	pd->TargetToSet(target, &list);
	FOR_EACH(&list, p, link)
	{
		if (p->p_ship == SHIP_SPEC)
			continue;

		int flags = SPAWN_SHIPRESET;
		if (p->flags.is_dead)
		{
			p->flags.is_dead = 0;
			flags |= SPAWN_AFTERDEATH;
		}
		DO_CBS(CB_SPAWN, p->arena, SpawnFunc, (p, flags));
	}

	pd->Unlock();
	LLEmpty(&list);
}


local void IncrementWeaponPacketCount(Player *p, int packets)
{
	pdata *data = PPDATA(p, pdkey);
	data->wpnsent += packets;
}


local void clear_data(Player *p, void *v)
{
	/* this was taken care of in PA_PREENTERARENA */
}

local int get_data(Player *p, void *data, int len, void *v)
{
	pdata *pdata = PPDATA(p, pdkey);
	int ret = 0;

	pthread_mutex_lock(&freqshipmtx);
	expire_lock(p);
	if (pdata->expires)
	{
		memcpy(data, &pdata->expires, sizeof(pdata->expires));
		ret = sizeof(pdata->expires);
	}
	pthread_mutex_unlock(&freqshipmtx);
	return ret;
}

local void set_data(Player *p, void *data, int len, void *v)
{
	pdata *pdata = PPDATA(p, pdkey);

	pthread_mutex_lock(&freqshipmtx);
	if (len == sizeof(pdata->expires))
	{
		memcpy(&pdata->expires, data, sizeof(pdata->expires));
		pdata->lockship = TRUE;
		/* try expiring once now, and... */
		expire_lock(p);
		/* if the lock is still active, force into spec */
		if (pdata->lockship)
		{
			p->p_ship = SHIP_SPEC;
			p->p_freq = p->arena->specfreq;
		}
	}
	pthread_mutex_unlock(&freqshipmtx);
}

local PlayerPersistentData persdata =
{
	KEY_SHIPLOCK, INTERVAL_FOREVER_NONSHARED, PERSIST_ALLARENAS,
	get_data, set_data, clear_data
};

local Appk _region_ppk_adv =
{
	ADVISER_HEAD_INIT(A_PPK)
	NULL, RegionPacketEditor
};

local Igame _myint =
{
	INTERFACE_HEAD_INIT(I_GAME, "game")
	SetFreq, SetShip, SetShipAndFreq, WarpTo, GivePrize,
	Lock, Unlock, LockArena, UnlockArena,
	FakePosition, FakeKill,
	GetIgnoreWeapons, SetIgnoreWeapons,
	ShipReset,
	IncrementWeaponPacketCount,
	SetPlayerEnergyViewing, SetSpectatorEnergyViewing,
	ResetPlayerEnergyViewing, ResetSpectatorEnergyViewing,
	DoWeaponChecksum, IsAntiwarped
};

EXPORT const char info_game[] = CORE_MOD_INFO("game");

EXPORT int MM_game(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		chatnet = mm->GetInterface(I_CHATNET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);

		if (!ml || !net || !cfg || !lm || !aman || !prng) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (adkey == -1 || pdkey == -1) return MM_FAIL;

		if (persist)
			persist->RegPlayerPD(&persdata);

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_NEWPLAYER, NewPlayer, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		net->AddPacket(C2S_POSITION, Pppk);
		net->AddPacket(C2S_SPECREQUEST, PSpecRequest);
		net->AddPacket(C2S_SETSHIP, PSetShip);
		net->AddPacket(C2S_SETFREQ, PSetFreq);
		net->AddPacket(C2S_DIE, PDie);
		net->AddPacket(C2S_GREEN, PGreen);
		net->AddPacket(C2S_ATTACHTO, PAttach);
		net->AddPacket(C2S_TURRETKICKOFF, PKickoff);

		if (chatnet)
			chatnet->AddHandler("CHANGEFREQ", MChangeFreq);

		if (cmd)
		{
			cmd->AddCommand("spec", Cspec, ALLARENAS, spec_help);
			cmd->AddCommand("energy", Cenergy, ALLARENAS, energy_help);
		}

		mm->RegAdviser(&_region_ppk_adv, ALLARENAS);
		mm->RegInterface(&_myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;
		mm->UnregAdviser(&_region_ppk_adv, ALLARENAS);
		if (chatnet)
			chatnet->RemoveHandler("CHANGEFREQ", MChangeFreq);
		if (cmd)
		{
			cmd->RemoveCommand("spec", Cspec, ALLARENAS);
			cmd->RemoveCommand("energy", Cenergy, ALLARENAS);
		}
		net->RemovePacket(C2S_SPECREQUEST, PSpecRequest);
		net->RemovePacket(C2S_POSITION, Pppk);
		net->RemovePacket(C2S_SETSHIP, PSetShip);
		net->RemovePacket(C2S_SETFREQ, PSetFreq);
		net->RemovePacket(C2S_DIE, PDie);
		net->RemovePacket(C2S_GREEN, PGreen);
		net->RemovePacket(C2S_ATTACHTO, PAttach);
		net->RemovePacket(C2S_TURRETKICKOFF, PKickoff);
		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregCallback(CB_NEWPLAYER, NewPlayer, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);
		if (persist)
			persist->UnregPlayerPD(&persdata);
		ml->ClearTimer(run_enter_game_cb, NULL);
		ml->ClearTimer(run_spawn_cb, NULL);
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(chatnet);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(lagc);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(persist);
		return MM_OK;
	}
	return MM_FAIL;
}

