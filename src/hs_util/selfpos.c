#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "asss.h"
#include "selfpos.h"

#ifdef USE_AKD_LAG
#include "akd_lag.h"
#endif

local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Iconfig *cfg;
local Igame *game;
local Ilagquery *lagq;
#ifdef USE_AKD_LAG
local Iakd_lag *akd_lag;
#endif

local void do_c2s_checksum(struct C2SPosition *pkt)
{
	int i;
	u8 ck = 0;
	pkt->checksum = 0;
	for (i = 0; i < sizeof(struct C2SPosition) - sizeof(struct ExtraPosData); i++)
		ck ^= ((unsigned char*)pkt)[i];
	pkt->checksum = ck;
}

local int get_player_lag(Player *p)
{
	int lag = 0;

#ifdef USE_AKD_LAG
	if (akd_lag)
	{
		akd_lag_report report;
		akd_lag->lagReport(p, &report);
		lag = report.c2s_ping_ave;
	}
	else
#endif
		if (lagq)
		{
			struct PingSummary pping;
			lagq->QueryPPing(p, &pping);
			lag = pping.avg;
		}


	// round to nearest tick
	if (lag % 10 >= 5)
	{
		lag = lag/10 + 1;
	}
	else
	{
		lag = lag/10;
	}

	return lag;
}

local void send_warp_packet(Player *p, int delta_t, struct S2CWeapons *packet)
{
	struct C2SPosition arena_packet = {
		C2S_POSITION, packet->rotation, current_ticks() + delta_t, packet->xspeed,
		packet->y, 0, packet->status, packet->x, packet->yspeed, packet->bounty, p->position.energy,
		packet->weapon
	};

	// send the warp packet to the player
	packet->time = (current_ticks() + delta_t + get_player_lag(p)) & 0xFFFF;
	game->DoWeaponChecksum(packet);
	net->SendToOne(p, (byte*)packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
	game->IncrementWeaponPacketCount(p, 1);

	// send the packet to other players
	do_c2s_checksum(&arena_packet);
	game->FakePosition(p, &arena_packet, sizeof(struct C2SPosition) - sizeof(struct ExtraPosData));
	// TODO: send it to the whole arena?
	// TODO: warp bit in status?
}

local void WarpPlayer(Player *p, int dest_x, int dest_y, int v_x, int v_y, int rotation, int delta_t)
{
	struct S2CWeapons packet = {
		S2C_WEAPON, rotation, 0, dest_x, v_y,
		p->pid, v_x, 0, p->position.status, 0,
		dest_y, p->position.bounty
	};

	send_warp_packet(p, delta_t, &packet);
}

local void WarpPlayerWithWeapon(Player *p, int dest_x, int dest_y, int v_x, int v_y, int rotation, int delta_t, struct Weapons *weapon)
{
	struct S2CWeapons packet = {
		S2C_WEAPON, rotation, 0, dest_x, v_y,
		p->pid, v_x, 0, p->position.status, 0,
		dest_y, p->position.bounty
	};

	packet.weapon = *weapon;

	send_warp_packet(p, delta_t, &packet);
}

local void SetBounty(Player *p, int bounty)
{
	// TODO: this should be more intelligent!
	p->position.bounty = bounty;
	WarpPlayer(p, p->position.x, p->position.y, p->position.xspeed,
			p->position.yspeed, p->position.rotation, 0);
}

local void SetStatus(Player *p, int status)
{
	// TODO: this should be more intelligent!
	p->position.status = status;
	WarpPlayer(p, p->position.x, p->position.y, p->position.xspeed,
			p->position.yspeed, p->position.rotation, 0);
}

local Iselfpos interface =
{
	INTERFACE_HEAD_INIT(I_SELFPOS, "selfpos")
	WarpPlayer, WarpPlayerWithWeapon, SetBounty, SetStatus
};

EXPORT const char info_selfpos[] = "v2.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_selfpos(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!lm || !net || !cfg || !game) return MM_FAIL;

		lagq = mm->GetInterface(I_LAGQUERY, ALLARENAS);
#ifdef USE_AKD_LAG
		akd_lag = mm->GetInterface(I_AKD_LAG, ALLARENAS);
#endif

		mm->RegInterface(&interface, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
		{
			return MM_FAIL;
		}

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(game);

		mm->ReleaseInterface(lagq);
#ifdef USE_AKD_LAG
		mm->ReleaseInterface(akd_lag);
#endif

		return MM_OK;
	}

	return MM_FAIL;
}

