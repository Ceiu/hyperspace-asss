#include <stdio.h>
#include <stdbool.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hs_util_energy.h"
#include "hs_util/selfpos.h"

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmd;
local Igame *game;
local Ihscoredatabase *db;
local Ihscoreitems *items;
local Imapdata *map;
local Imainloop *ml;
local Iobjects *objs;
local Iplayerdata *pd;
local Iselfpos *sp;

#define PlayerData PlayerData_
typedef struct PlayerData
{
    bool utilityOn;
    int repairTargetPid;

    struct
    {
        bool repair;
        int repairRange;
    } ItemProperties;

    ticks_t lastShowedLVZ;
    ticks_t lastPositionPacket;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

typedef struct ArenaData
{
    Ihsutilenergy *utilnrg;

    struct
    {
        int outwardCurrentID;
        int inwardCurrentID;

        struct
        {
            int outwardStartID;
            int outwardEndID;
            int outwardXOffset;
            int outwardYOffset;

            int inwardStartID;
            int inwardEndID;
            int inwardXOffset;
            int inwardYOffset;
        } Config;
    } Lvz;

    struct
    {
        int lvzDisplayDelay;
        bool lvzPredictLocation;
    } Config;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

//Prototypes
local void ppkCB(Player *p, struct C2SPosition *pos);
local void utilityStateChangedCB(Player *p, int state, u8 reason);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB);
local long lhypot(register long dx, register long dy);
local void displayDrainLVZ(Player *p);
local void displayChargeLVZ(Player *p);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Commands
local void Crepair(const char *cmd, const char *params, Player *p, const Target *target)
{
    if (target->type == T_PLAYER)
    {
        PlayerData *pdata = getPlayerData(p);
		if (!pdata->ItemProperties.repair)
		{
            chat->SendMessage(p, "You do not have an item capable of repairing another player on your ship.");
			return;
		}

		Player *t = target->u.p;
		if (t)
		{
			if (p == t)
			{
                chat->SendMessage(p, "You can't repair yourself.");
				return;
			}

			if (!IS_STANDARD(t))
			{
                chat->SendMessage(p, "That player can't be repaired.");
				return;
			}

			if (t->p_ship == SHIP_SPEC)
			{
				chat->SendMessage(p, "You cannot repair players in spectator mode.");
				return;
			}

			if (t->p_freq != p->p_freq)
			{
				chat->SendMessage(p, "You can only repair players on your own frequency.");
				return;
			}

			chat->SendMessage(p, "Repair target set: %s.", t->name);
			pdata->repairTargetPid = t->pid;
			return;
		}

		chat->SendMessage(p, "Could not set repair target.");
    }
	else
	{
		chat->SendMessage(p, "You can only repair players.");
	}
}

//Callbacks
local void ppkCB(Player *p, struct C2SPosition *pos)
{
    if (!IS_STANDARD(p)) return;

    PlayerData *pdata = getPlayerData(p);
    pdata->lastPositionPacket = current_ticks();

    if (!pdata->ItemProperties.repair) return; // No damn was given that day
    if (!(pos->status & STATUS_STEALTH)) return; // ^

    ArenaData *adata = getArenaData(p->arena);

    if (TICK_DIFF(current_ticks(), pdata->lastShowedLVZ) >= adata->Config.lvzDisplayDelay)
    {
		pdata->lastShowedLVZ = current_ticks();

        if (pdata->repairTargetPid == -1)
        {
            if (adata->utilnrg)
               adata->utilnrg->SetUtilityEnergy(p, -1);
            return;
        }
        Player *target = pd->PidToPlayer(pdata->repairTargetPid);
        if (!target)
        {
            pdata->repairTargetPid = -1;

            if (adata->utilnrg)
               adata->utilnrg->SetUtilityEnergy(p, -1);
            return;
        }

        if (target->p_freq != p->p_freq)
        {
            pdata->repairTargetPid = -1;

            chat->SendMessage(p, "Repair aborted: %s is no longer on your frequency.", target->name);
            if (adata->utilnrg)
               adata->utilnrg->SetUtilityEnergy(p, -1);
            return;
        }

        if (target->p_ship == SHIP_SPEC)
        {
            pdata->repairTargetPid = -1;

            chat->SendMessage(p, "Repair aborted: %s switched to spectator mode.", target->name);
            if (adata->utilnrg)
               adata->utilnrg->SetUtilityEnergy(p, -1);
            return;
        }

        long distance = lhypot(target->position.x - pos->x, target->position.y - pos->y);
        if (distance > pdata->ItemProperties.repairRange)
        {
            pdata->repairTargetPid = -1;

            chat->SendMessage(p, "Repair aborted: %s is out of range.", target->name);
            if (adata->utilnrg)
               adata->utilnrg->SetUtilityEnergy(p, -1);
        }
    }
}

local void utilityStateChangedCB(Player *p, int state, u8 reason)
{
    PlayerData *pdata = getPlayerData(p);
    if (!pdata->ItemProperties.repair) return;

    pdata->utilityOn = (state > 0);
    if (pdata->utilityOn)
    {
        Player *target = pd->PidToPlayer(pdata->repairTargetPid);
        if (!target)
        {
            pdata->repairTargetPid = -1;
            chat->SendMessage(p, "Repair aborted: could not find target player.");
            return;
        }

        //Play LVZ
        displayDrainLVZ(p);
        displayChargeLVZ(target);

        //Prize player full charge
        Target t;
        t.type = T_PLAYER;
        t.u.p = target;

        game->GivePrize(&t, 13, 1);

        //Reset utility status
        ArenaData *adata = getArenaData(p->arena);
        if (adata->utilnrg)
           adata->utilnrg->SetUtilityEnergy(p, -1);
    }
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, false);
}
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
}

//Misc/utility functions
local void updatePlayerData(Player *p, ShipHull *hull, bool lockDB)
{
    if (!IS_STANDARD(p)) return;
    if (!hull || hull != db->getPlayerCurrentHull(p)) return;

    PlayerData *pdata = getPlayerData(p);

    if (lockDB == true)
       db->lock();

    pdata->ItemProperties.repair = (items->getPropertySumOnHull(p, hull, "repair", 0) > 0);
    pdata->ItemProperties.repairRange = items->getPropertySumOnHull(p, hull, "repairrange", 0);

    if (lockDB == true)
       db->unlock();

    pdata->repairTargetPid = -1;
}

local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    adata->Lvz.Config.outwardStartID = cfg->GetInt(ch, "Repair", "outwardLvzStartID", -1);
    adata->Lvz.Config.outwardEndID = cfg->GetInt(ch, "Repair", "outwardLvzEndID", -1);
    adata->Lvz.Config.outwardXOffset = cfg->GetInt(ch, "Repair", "outwardLvzXOffset", 0);
    adata->Lvz.Config.outwardYOffset = cfg->GetInt(ch, "Repair", "outwardLvzYOffset", 0);

    adata->Lvz.Config.inwardStartID = cfg->GetInt(ch, "Repair", "inwardLvzStartID", -1);
    adata->Lvz.Config.inwardEndID = cfg->GetInt(ch, "Repair", "inwardLvzEndID", -1);
    adata->Lvz.Config.inwardXOffset = cfg->GetInt(ch, "Repair", "inwardLvzXOffset", 0);
    adata->Lvz.Config.inwardYOffset = cfg->GetInt(ch, "Repair", "inwardLvzYOffset", 0);

    adata->Config.lvzDisplayDelay = cfg->GetInt(ch, "Repair", "lvzDisplayDelay", 100); // 100 ticks ~ 1 second by default
    adata->Config.lvzPredictLocation = (cfg->GetInt(ch, "Repair", "lvzPredictLocation", 0) > 0);

    if (adata->Lvz.Config.outwardStartID < 0 || adata->Lvz.Config.outwardEndID < 0)
       adata->Lvz.outwardCurrentID = -1;
    else
       adata->Lvz.outwardCurrentID = adata->Lvz.Config.outwardStartID;

    if (adata->Lvz.Config.inwardStartID < 0 || adata->Lvz.Config.inwardEndID < 0)
       adata->Lvz.inwardCurrentID = -1;
    else
       adata->Lvz.inwardCurrentID = adata->Lvz.Config.inwardStartID;
}

local long lhypot(register long dx, register long dy)
{
	register unsigned long r, dd;
	dd = dx*dx+dy*dy;

	if (dx < 0) dx = -dx;
	if (dy < 0) dy = -dy;

	r = (dx > dy) ? (dx+(dy>>1)) : (dy+(dx>>1));
	if (r == 0) return (long)r;

	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;
	r = (dd/r+r)>>1;

	return (long)r;
}

local void displayDrainLVZ(Player *p)
{
    Arena *arena = p->arena;
    if (!arena) return;

    ArenaData *adata = getArenaData(arena);

    int x = p->position.x;
    int y = p->position.y;

    if (adata->Config.lvzPredictLocation)
    {
        PlayerData *pdata = getPlayerData(p);
        ticks_t dt = TICK_DIFF(current_ticks(), pdata->lastPositionPacket);

        x += p->position.xspeed * (dt / 100.0);
        y += p->position.yspeed * (dt / 100.0);
    }

    Target t;
	t.type = T_ARENA;
	t.u.arena = arena;

	objs->Move(&t, adata->Lvz.outwardCurrentID, x + adata->Lvz.Config.outwardXOffset,
				   y + adata->Lvz.Config.outwardYOffset, 0, 0);
	objs->Toggle(&t,adata->Lvz.outwardCurrentID,1);

	adata->Lvz.outwardCurrentID++;
	if (adata->Lvz.outwardCurrentID > adata->Lvz.Config.outwardEndID)
	   adata->Lvz.outwardCurrentID = adata->Lvz.Config.outwardStartID;
}
local void displayChargeLVZ(Player *p)
{
    Arena *arena = p->arena;
    if (!arena) return;

    ArenaData *adata = getArenaData(arena);

    int x = p->position.x;
    int y = p->position.y;

    if (adata->Config.lvzPredictLocation)
    {
        PlayerData *pdata = getPlayerData(p);
        ticks_t dt = TICK_DIFF(current_ticks(), pdata->lastPositionPacket);

        x += p->position.xspeed * (dt / 1000.0);
        y += p->position.yspeed * (dt / 1000.0);
    }

    Target t;
	t.type = T_ARENA;
	t.u.arena = arena;

	objs->Move(&t, adata->Lvz.inwardCurrentID, x + adata->Lvz.Config.inwardXOffset,
				   y + adata->Lvz.Config.inwardYOffset, 0, 0);
	objs->Toggle(&t,adata->Lvz.inwardCurrentID,1);

	adata->Lvz.inwardCurrentID++;
	if (adata->Lvz.inwardCurrentID > adata->Lvz.Config.inwardEndID)
	   adata->Lvz.inwardCurrentID = adata->Lvz.Config.inwardStartID;
}

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    map = mm->GetInterface(I_MAPDATA, ALLARENAS);
    ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    objs = mm->GetInterface(I_OBJECTS, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    sp = mm->GetInterface(I_SELFPOS, ALLARENAS);
}
local bool checkInterfaces()
{
    if (aman && cfg && chat && cmd && db && game && items && map && ml && objs && pd && sp)
       return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(chat);
    mm->ReleaseInterface(cmd);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(map);
    mm->ReleaseInterface(ml);
    mm->ReleaseInterface(objs);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(sp);
}

local helptext_t Crepair_help =
	"Targets: player\n"
	"Sets the specified player as your 'repair target'."
    " This player will be prized full charge when your repair"
    " item is done charging.";

EXPORT const char info_hs_repair[] = "hs_repair v1.0 by Spidernl\n";
EXPORT int MM_hs_repair(int action, Imodman *mm_, Arena *arena)
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

        cmd->AddCommand("repair", Crepair, arena, Crepair_help);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        cmd->RemoveCommand("repair", Crepair, arena);

        mm->UnregCallback(CB_PPK, ppkCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_UTILITY_STATE_CHANGED, utilityStateChangedCB, arena);

        ArenaData *adata = getArenaData(arena);
        mm->ReleaseInterface(adata->utilnrg);

        return MM_OK;
    }

	return MM_FAIL;
}
