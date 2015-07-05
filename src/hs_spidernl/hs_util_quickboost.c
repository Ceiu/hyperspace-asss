#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hs_util_energy.h"

#ifdef USE_AKD_LAG
#include "akd_lag.h"
#endif

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Iobjects *objs;
local Iplayerdata *pd;

//Selfpos interfaces
local Igame *game;
local Ilagquery *lagq;
local Inet *net;
#ifdef USE_AKD_LAG
local Iakd_lag *akd_lag;
#endif

typedef struct ArenaData
{
    Ihsutilenergy *utilnrg;

    struct
    {
        struct
        {
           int startID;
           int endID;
           int xOffset;
           int yOffset;
        } Config;

        int currentID;
    } Lvz;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    return P_ARENA_DATA(arena, adkey);
}

#define PlayerData PlayerData_
typedef struct PlayerData
{
    //Is the utility currently on?
    bool utilityOn;

    //Item properties
    bool canQuickBoost;
    int quickBoostDelay;
    int quickBoostPower;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    return PPDATA(p, pdkey);
}

//Prototypes
local void ppkCB(Player *p, struct C2SPosition *pos);
local void utilityStateChangedCB(Player *p, int state, u8 reason);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local void arenaActionCB(Arena *arena, int action);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB);

local void WarpPlayerWithFlash(Player *p, int dest_x, int dest_y, int v_x, int v_y, int rotation, int delta_t);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

local void readConfig(Arena *arena);

//Callbacks
local void ppkCB(Player *p, struct C2SPosition *pos)
{
    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

	if (p->flags.is_dead) return;
	if (!pdata->utilityOn) return;

	//Reset utility status
	adata->utilnrg->SetUtilityEnergy(p, -1);

	//Calculate speed values
	double theta = ((40 - (pos->rotation + 30) % 40) * 9) * (M_PI / 180);
	int xspeed = pdata->quickBoostPower * cos(theta);
	int yspeed = pdata->quickBoostPower * -sin(theta);

	//Warp player
	WarpPlayerWithFlash(p, pos->x, pos->y, xspeed, yspeed, pos->rotation, 0);

	//Play LVZ if configs are "correct"
	if (adata->Lvz.currentID < 0) return;

	Target t;
	t.type = T_ARENA;
	t.u.arena = p->arena;

	objs->Move(&t, adata->Lvz.currentID, pos->x + adata->Lvz.Config.xOffset,
				   pos->y + adata->Lvz.Config.yOffset, 0, 0);
	objs->Toggle(&t,adata->Lvz.currentID,1);

	adata->Lvz.currentID++;
	if (adata->Lvz.currentID > adata->Lvz.Config.endID)
	   adata->Lvz.currentID = adata->Lvz.Config.startID;
}

local void utilityStateChangedCB(Player *p, int state, u8 reason)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->canQuickBoost) return;

    pdata->utilityOn = (state > 0);
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, false);
}
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
}

local void arenaActionCB(Arena *arena, int action)
{
	if (action == AA_CONFCHANGED)
	{
		readConfig(arena);
	}
}

//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB)
{
    if (!IS_STANDARD(p)) return;
    if (!hull || hull != db->getPlayerCurrentHull(p)) return;

    PlayerData *pdata = getPlayerData(p);

    if (lockDB == true)
       db->lock();

    pdata->canQuickBoost = (items->getPropertySumOnHull(p, hull, "quickboost", 0) > 0);
	pdata->quickBoostDelay = items->getPropertySumOnHull(p, hull, "quickboostdelay", 0);
	pdata->quickBoostPower = items->getPropertySumOnHull(p, hull, "quickboostpower", 0);

    if (lockDB == true)
       db->unlock();
}

//Selfpos stuff
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
}

local void WarpPlayerWithFlash(Player *p, int dest_x, int dest_y, int v_x, int v_y, int rotation, int delta_t)
{
	struct S2CWeapons packet = {
		S2C_WEAPON, rotation, 0, dest_x, v_y,
		p->pid, v_x, 0,
		(p->position.status | STATUS_FLASH) & ~STATUS_STEALTH,
		0, dest_y, p->position.bounty
	};

	send_warp_packet(p, delta_t, &packet);
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    objs = mm->GetInterface(I_OBJECTS, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

    //Selfpos stuff
    game = mm->GetInterface(I_GAME, ALLARENAS);
    lagq = mm->GetInterface(I_LAGQUERY, ALLARENAS);
    net = mm->GetInterface(I_NET, ALLARENAS);

    #ifdef USE_AKD_LAG
	akd_lag = mm->GetInterface(I_AKD_LAG, ALLARENAS);
	#endif
}
local bool checkInterfaces()
{
    if (aman && cfg && db && items && objs && pd /*Selfpos stuff*/ && game && net)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(objs);
    mm->ReleaseInterface(pd);

    //Selfpos stuff
    mm->ReleaseInterface(game);
    if (lagq) mm->ReleaseInterface(lagq);
    mm->ReleaseInterface(net);

    #ifdef USE_AKD_LAG
    if (akd_lag) mm->ReleaseInterface(akd_lag);
    #endif
}

//Misc
local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    adata->Lvz.Config.startID = cfg->GetInt(ch, "Quickboost", "lvzStartID", -1);
    adata->Lvz.Config.endID = cfg->GetInt(ch, "Quickboost", "lvzEndID", -1);
    adata->Lvz.Config.xOffset = cfg->GetInt(ch, "Quickboost", "lvzXOffset", 0);
    adata->Lvz.Config.yOffset = cfg->GetInt(ch, "Quickboost", "lvzYOffset", 0);

    if (adata->Lvz.Config.startID < 0 || adata->Lvz.Config.endID < 0)
       adata->Lvz.currentID = -1;
    else
       adata->Lvz.currentID = adata->Lvz.Config.startID;
}

EXPORT const char info_hs_util_quickboost[] = "hs_util_quickboost v1.0 by Spidernl\n";
EXPORT int MM_hs_util_quickboost(int action, Imodman *mm_, Arena *arena)
{
    if (action == MM_LOAD)
	{
		mm = mm_;

		getInterfaces();
		if (!checkInterfaces())
		{
            releaseInterfaces();
            return MM_FAIL;
        }

        adkey = aman->AllocateArenaData(sizeof(struct ArenaData));
        pdkey = pd->AllocatePlayerData(sizeof(struct PlayerData));

        if (adkey == -1 || pdkey == -1) //Memory check
        {
            if (adkey  != -1) //free data if it was allocated
                aman->FreeArenaData(adkey);

            if (pdkey != -1) //free data if it was allocated
                pd->FreePlayerData(pdkey);

            releaseInterfaces();
            return MM_FAIL;
        }

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        aman->FreeArenaData(adkey);
        pd->FreePlayerData(pdkey);

        releaseInterfaces();

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena);
        adata->utilnrg = mm->GetInterface(I_HS_UTIL_ENERGY, arena);
        if (!adata->utilnrg)
        {
            return MM_FAIL;
        }

        readConfig(arena);

        mm->RegCallback(CB_PPK, ppkCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);
        mm->RegCallback(CB_ARENAACTION, arenaActionCB, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);
        mm->ReleaseInterface(adata->utilnrg);

        mm->UnregCallback(CB_ARENAACTION, arenaActionCB, arena);
        mm->UnregCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_PPK, ppkCB, arena);

        return MM_OK;
    }

	return MM_FAIL;
}
