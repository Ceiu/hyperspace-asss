
/* dist: public */

#include <string.h>

#include "asss.h"
#include "banners.h"
#include "packets/banners.h"

local Imodman *mm;
local Inet *net;
local Iplayerdata *pd;
local Ilogman *lm;

typedef struct
{
	Banner banner;
	/* 0 = no banner present, 1 = present but not used, 2 = present and used */
	byte status;
	/* this way you can turn the banner on when they    *
	 * pass the CheckBanner test.                       *
	 * NOTE: If CheckBanner relies on points or some    *
	 *       other module, this will need to register   *
	 *       callbacks with that module so that it can  *
	 *       check if the player has his banner showing *
	 *       and if not but he passes CheckBanner now,  *
	 *       then send out his banner.                  *
	 *       Effectively, this is here so that people   *
	 *       don't have to re-enter arena in order that *
	 *       their banner be displayed to the arena     */
} bdata;

local int bdkey;
local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)


/* return true to allow banner */
local int CheckBanner(Player *p)
{
	/* FIXME: either put more logic in here, or have it call out to some
	 * interface to figure out if this player can have a banner. */
	/* for now, everyone can have banners. */
	return TRUE;
}


local void check_and_send_banner(Player *p, int callothers, int from_player)
{
	bdata *bd = PPDATA(p, bdkey);

	if (bd->status == 0)
		return;
	else if (bd->status == 1)
	{
		if (CheckBanner(p))
			bd->status = 2;
		else
			if (lm) lm->LogP(L_DRIVEL, "banners", p, "denied permission to use a banner");
	}

	if (bd->status == 2)
	{
		/* send to everyone */
		struct S2CBanner pkt = { S2C_BANNER, p->pid };

		memcpy(&pkt.banner, &bd->banner, sizeof(pkt.banner));

		if (!p->arena)
			return;

		net->SendToArena(p->arena, NULL, (byte*)&pkt, sizeof(pkt),
				NET_RELIABLE | NET_PRI_N1);

		/* notify other modules */
		if (callothers)
			DO_CBS(CB_SET_BANNER,
					p->arena,
					SetBannerFunc,
					(p, &bd->banner, from_player));
	}
}


local void SetBanner(Player *p, Banner *bnr)
{
	bdata *bd = PPDATA(p, bdkey);

	LOCK();

	bd->banner = *bnr;
	bd->status = 1;

	/* if he's not in the playing state yet, just hold the banner and
	 * status at 1. we'll check it when he enters the arena.
	 * if he is playing, though, do the check and send now. */
	if (p->status == S_PLAYING)
		check_and_send_banner(p, TRUE, FALSE);

	UNLOCK();
}

local void SetBannerFromPlayer(Player *p, Banner *bnr)
{
	bdata *bd = PPDATA(p, bdkey);

	LOCK();

	bd->banner = *bnr;
	bd->status = 1;

	/* if he's not in the playing state yet, just hold the banner and
	 * status at 1. we'll check it when he enters the arena.
	 * if he is playing, though, do the check and send now. */
	if (p->status == S_PLAYING)
		check_and_send_banner(p, TRUE, TRUE);

	UNLOCK();
}

local void PBanner(Player *p, byte *pkt, int len)
{
	struct C2SBanner *b = (struct C2SBanner*)pkt;

	if (len != sizeof(*b))
	{
		if (lm) lm->LogP(L_MALICIOUS, "banners", p, "bad size for banner packet");
		return;
	}

	/* this implicitly catches setting from pre-playing states */
	if (p->arena == NULL)
	{
		if (lm) lm->LogP(L_MALICIOUS, "banners", p, "tried to set a banner from outside an arena");
		return;
	}

	if (p->p_ship != SHIP_SPEC)
	{
		Ichat *chat = mm->GetInterface(I_CHAT, ALLARENAS);
		if (chat) chat->SendMessage(p, "You must be in spectator mode to set a banner.");
		mm->ReleaseInterface(chat);
		if (lm) lm->LogP(L_INFO, "banners", p, "tried to set a banner while in a ship");
		return;
	}

	SetBannerFromPlayer(p, &b->banner);
	if (lm) lm->LogP(L_DRIVEL, "banners", p, "set banner");
}


local void paction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
	{
		bdata *ibd;
		Link *link;
		Player *i;

		LOCK();

		/* first check permissions on a stored banner from the biller
		 * and send it to the arena. */
		check_and_send_banner(p, FALSE, FALSE);

		/* then send everyone's banner to him */
		pd->Lock();
		FOR_EACH_PLAYER_P(i, ibd, bdkey)
			if (i->status == S_PLAYING &&
			    i->arena == arena &&
			    ibd->status == 2 &&
			    i != p)
			{
				struct S2CBanner pkt = { S2C_BANNER, i->pid };
				memcpy(&pkt.banner, &ibd->banner, sizeof(pkt.banner));
				net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE | NET_PRI_N1);
			}
		pd->Unlock();
		UNLOCK();
	}
}


local Ibanners myint =
{
	INTERFACE_HEAD_INIT(I_BANNERS, "banners")
	SetBanner, SetBannerFromPlayer
};

EXPORT const char info_banners[] = CORE_MOD_INFO("banners");

EXPORT int MM_banners(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!net || !pd) return MM_FAIL;

		bdkey = pd->AllocatePlayerData(sizeof(bdata));
		if (bdkey == -1) return MM_FAIL;

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);
		net->AddPacket(C2S_BANNER, PBanner);
		mm->RegInterface(&myint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myint, ALLARENAS))
			return MM_FAIL;
		net->RemovePacket(C2S_BANNER, PBanner);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		pd->FreePlayerData(bdkey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

