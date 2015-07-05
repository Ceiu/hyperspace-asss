#include "asss.h"
#include "hscore.h"
#include "kill.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Imapdata *mapdata;
local Ikill *kill;
local Iarenaman *aman;
local Iprng *prng;
local Iplayerdata *pd;
local Ihscoredatabase *database;
local Ichat *chat;
local Imainloop *mainloop;
local Iconfig *cfg;
local Iprng *prng;

#define KILLER_NAME "<energy vortex>"
#define COOLDOWN_TIME 200
#define GRANT_MONEY 10000
#define GRANT_PERCENT 1000

local void startSequence(Arena *arena);

typedef struct hsvortexdata
{
	int attached;
	Killer *killer;

	int initialAngle;
	int angle;
	int x;
	int y;
	Region *vortex;
	ticks_t nextShot;
} hsvortexdata;

local int adkey;

local void Pppk(Player *p, byte *p2, int len)
{
	hsvortexdata *ad;
	struct C2SPosition *pos = (struct C2SPosition *)p2;

	if (len < 22)
		return;

	/* handle common errors */
	if (!p->arena) return;

	ad = P_ARENA_DATA(p->arena, adkey);

	if (!ad->attached)
		return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (ad->nextShot > current_ticks())
		return;

	if (!ad->vortex)
		ad->vortex = mapdata->FindRegionByName(p->arena, "vortex");

	if (ad->vortex != NULL)
	{
		if (mapdata->Contains(ad->vortex, pos->x>>4, pos->y>>4))
		{
			int chance = GRANT_PERCENT;
			int rand = prng->Number(1, chance);

			//check for grant!
			if (rand == 1)
			{
				database->addMoney(p, MONEY_TYPE_EVENT, GRANT_MONEY);
				chat->SendMessage(p, "Vortex has been vanquished! $%d given!", GRANT_MONEY);
			}
			else
			{
				kill->Kill(p, ad->killer, 0, 0);
				startSequence(p->arena);
			}

			ad->nextShot = current_ticks() + COOLDOWN_TIME;
		}
	}
}

local int firingCallback(void *param)
{
	Arena *arena = param;
	hsvortexdata *ad = P_ARENA_DATA(arena, adkey);
	Player *fake_player = kill->GetKillerPlayer(ad->killer);
	if (fake_player == NULL)
		return FALSE;

	struct Weapons weapon = {
		W_THOR,	//u16 type,;
		3,		//u16 level : 2;
		1,		//u16 shrapbouncing : 1;
		3,		//u16 shraplevel : 2;
		31,		//u16 shrap : 5;
		0		//u16 alternate : 1;
	};

	struct S2CWeapons packet = {
		S2C_WEAPON, ad->angle, current_ticks() & 0xFFFF, ad->x, 0,
		fake_player->pid, 0, 0, STATUS_STEALTH | STATUS_CLOAK | STATUS_UFO, 0,
		ad->y, 0 /*bounty*/
	};

	packet.weapon = weapon;

	net->SendToArena(arena, NULL, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);

	ad->angle = (ad->angle + 1) % 40;

	if (ad->angle == ad->initialAngle)
	{
		return FALSE; //done
	}
	else
	{
		return TRUE;
	}
}

local void startSequence(Arena *arena)
{
	hsvortexdata *ad = P_ARENA_DATA(arena, adkey);

	ad->angle = 0;
	ad->initialAngle = ad->angle;
	ad->x = cfg->GetInt(arena->cfg, "Vortex", "FireX", 454) << 4;
	ad->y = cfg->GetInt(arena->cfg, "Vortex", "FireY", 253) << 4;

	// start the firing callback
	//we don't want to annoy our zone hosts with a 1ms delay weapon that likes to clog the lines..
	mainloop->SetTimer(firingCallback, 0, 6, arena, arena);
}

local int startSequenceCallback(void *param)
{
	Arena *arena = param;
	int delay;

	startSequence(arena);

	// next start is between 0 and 15 minutes (90000 ticks)
	delay = cfg->GetInt(arena->cfg, "Vortex", "Delay", 90000);
	mainloop->SetTimer(startSequenceCallback, delay, 0, arena, arena);

	chat->SendArenaMessage(arena, "CAUTION: Vortex flare detected!");

	return FALSE;
}

EXPORT const char info_hs_vortex[] = "v1.1 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_vortex(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		kill = mm->GetInterface(I_KILL, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		mainloop = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);

		if (!lm || !net || !mapdata || !kill || !aman || !prng || !pd || !database || !chat || !mainloop || !cfg || !prng) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(hsvortexdata));
		if (adkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);

		aman->FreeArenaData(adkey);

		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(kill);
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(database);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(mainloop);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(prng);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		hsvortexdata *ad = P_ARENA_DATA(arena, adkey);

		ad->killer = kill->LoadKiller(KILLER_NAME, arena, 0, 9999);

		ad->attached = 1;
		ad->vortex = 0;

		mainloop->SetTimer(startSequenceCallback, 100, 30000, arena, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		hsvortexdata *ad = P_ARENA_DATA(arena, adkey);
		ad->attached = 0;
		kill->UnloadKiller(ad->killer);

		mainloop->ClearTimer(startSequenceCallback, NULL);
		mainloop->ClearTimer(firingCallback, NULL);

		return MM_OK;
	}
	return MM_FAIL;
}

