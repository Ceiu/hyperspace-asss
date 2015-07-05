#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "packets/ppk.h"
#include "hs_util/selfpos.h"


// Modules...
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Icmdman *cmdman;
local Ihscoreitems *items;
local Ihscoredatabase *database;
local Ilogman *lm;
local Imainloop *ml;
local Imapdata *map;
local Imodman *mm;
local Inet *net;
local Iobjects *obj;
local Iplayerdata *pd;
local Iselfpos *sp;


// Prototypes!
////////////////////////////////////////////////////////////////////////////////////////////////////
local void cmd_quickboost(const char *cmd, const char *params, Player *player, const Target *target);
local void cmd_blinkstrike(const char *cmd, const char *params, Player *player, const Target *target);

local void pkt_position(Player *player, byte *pkt, int len);
local void pkt_death(Player *player, byte *pkt, int len);

local int timer_recharge(void* param);

local void cb_arenaaction(Arena *arena, int action);
local void cb_playeraction(Player *player, int action, Arena *arena);
local void cb_itemschanged(Player* player, ShipHull *hull);
local void cb_shipsetchanged(Player *player, int oldshipset, int newshipset);

local void update_status(Player *player, ShipHull *hull);
local void load_arena_config(Arena *arena);
local void show_lvz(Arena *arena, int obj_id, int img_id, int x, int y);


// Globals, structs, etc...
////////////////////////////////////////////////////////////////////////////////////////////////////
#define KB_PROP_QUICKBOOST  "quickboost"
#define KB_PROP_QB_POWER    "boostpower"

#define KB_PROP_BLINKSTRIKE "blinkstrike"
#define KB_PROP_BS_DISTANCE "blinkdistance"

#define KB_PROP_MAXCHARGES  "k-maxcharges"
#define KB_PROP_CHARGETIME  "k-rechargetime"

#define KB_ACTION_QB    1
#define KB_ACTION_BS    2


local int KBArenaKey;
typedef struct
{
    u8 loaded       : 1;    // If the lvz settings have been loaded.
    u8 padding1     : 7;

    int lvz_objid_offset;   // The starting object id allocated for us.
    int lvz_objid_count;    // The number of object ids we can use.
    int lvz_objid_last;     // The last object id we've used.

    int lvz_imgid_boost;    // The image id of the quickboost burst.
    int lvz_img_boost_w;    // Width
    int lvz_img_boost_h;    // Height

    int lvz_imgid_blink;    // The image id for the blinkstrike graphic.
    int lvz_img_blink_w;    // Width
    int lvz_img_blink_h;    // Height
} KBArenaData;


local int KBStatusKey;      // Engine status player data key
typedef struct
{
    u8 quickboost   : 1;    // If the player is capable of quickboosting
    u8 blinkstrike  : 1;    // if the player is capable of using blink strike
    u8 padding1     : 6;

    u8 recharging   : 1;    // True if this player has a recharge timer
    u8 padding2     : 7;

    int charges;            // The amount of charges the player currently has
    int max_charges;        // The maximum number of charges this player can have
    int recharge_time;      // Amount of time between charges

    int boost_power;        // Quickboost Power (pixels)

    int blink_distance;     // Blink distance (pixels)

    int action;             // The action to take on the next position packet
    int param;              // An action parameter

} KBStatus;

typedef struct C2SPosition C2SPosition;
typedef struct S2CPosition S2CPosition;

#pragma pack(push,1)

typedef struct
{
    u8  packet_header;      // Core packet id: 0x00
    u8  packet_id;          // Cluster packet id: 0x0E
    u8  packet_size_1;      // LVZ Modify packet size: 12

    u8  lvzmod_pkt_id;      // LVZ Modify packet id: 0x36
    u8  lvzmod_flags;       // Binary: 0001-0011
    u16 lvzmod_obj_map  :  1;
    u16 lvzmod_obj_id   : 15;
    u16 lvzmod_pos_x;       // Position X (and origin)
    u16 lvzmod_pos_y;       // Position Y
    u8  lvzmod_img_id;
    u8  lvzmod_img_layer;
    u16 lvzmod_display_time;

    u8  packet_size_2;      // LVZ Toggle packet size: 3

    u8  lvztgl_pkt_id;
    u16 lvztgl_obj_id   : 15;
    u16 lvztgl_obj_show :  1;

} LVZCluster;
#pragma pack(pop)

// Command handlers...
////////////////////////////////////////////////////////////////////////////////////////////////////
local void cmd_quickboost(const char *cmd, const char *params, Player *player, const Target *target)
{
    KBStatus* kb_status = PPDATA(player, KBStatusKey);

    // Validation...
    if(!target->type == T_ARENA)
        return; // Don't care about player commands.

    if(!params || !*params)
        return; // No params or zero length (aka no params? heh).

    if(!kb_status->quickboost)
        return; // They don't have an item with quickboost.


    kb_status->action = KB_ACTION_QB;
    kb_status->param = atoi(params);


    // Reload arena config?
    KBArenaData* kb_adata = P_ARENA_DATA(player->arena, KBArenaKey);
    if(!kb_adata->loaded) load_arena_config(player->arena);
}

local void cmd_blinkstrike(const char *cmd, const char *params, Player *player, const Target *target)
{
    KBStatus* kb_status = PPDATA(player, KBStatusKey);

    // Validation...
    if(!target->type == T_ARENA)
        return; // Don't care about player commands.

    if(!kb_status->blinkstrike)
        return; // They don't have an item with blink strike.

    // Activate
    kb_status->action = KB_ACTION_BS;
    kb_status->param = 14;

    // Reload arena config?
    KBArenaData* kb_adata = P_ARENA_DATA(player->arena, KBArenaKey);
    if(!kb_adata->loaded) load_arena_config(player->arena);
}


// Packet handlers...
////////////////////////////////////////////////////////////////////////////////////////////////////
local void pkt_position(Player *player, byte *pkt, int len)
{
    KBStatus* kb_status = PPDATA(player, KBStatusKey);
    KBArenaData* kb_adata = P_ARENA_DATA(player->arena, KBArenaKey);


    // Validation...
    if(!kb_status->action || !(kb_status->charges > 0) || !((C2SPosition*)pkt)->energy)
    {
        kb_status->action = 0;
        kb_status->param = 0;
        return;
    }


    C2SPosition* pos = (C2SPosition*)pkt;

    double theta;
    int obj_id, vel_x, vel_y, delta_x, delta_y;

    switch(kb_status->action)
    {
        case KB_ACTION_QB:    // Quickboost: Forward
            theta = (((40 - (pos->rotation + 30) % 40) * 9) + kb_status->param) * M_PI / 180;
            vel_x = kb_status->boost_power * cos(theta);
            vel_y = kb_status->boost_power * -sin(theta);

            sp->WarpPlayer(player, pos->x, pos->y, pos->xspeed + vel_x, pos->yspeed + vel_y, pos->rotation, 0);

            if(kb_adata->loaded)
            {
                obj_id = kb_adata->lvz_objid_last;

                if(obj_id < kb_adata->lvz_objid_offset || obj_id >= kb_adata->lvz_objid_offset + kb_adata->lvz_objid_count)
                    obj_id = kb_adata->lvz_objid_offset;

                kb_adata->lvz_objid_last = obj_id + 1;

                show_lvz(player->arena, obj_id, kb_adata->lvz_imgid_boost, pos->x - (kb_adata->lvz_img_boost_w >> 1), pos->y - (kb_adata->lvz_img_boost_h >> 1));
            }
            break;

        case KB_ACTION_BS:      // Blink Strike
            theta = ((40 - (pos->rotation + 30) % 40) * 9) * M_PI / 180;
            delta_x = kb_status->blink_distance * cos(theta);
            delta_y = kb_status->blink_distance * -sin(theta);
            vel_x = -pos->xspeed;
            vel_y = -pos->yspeed;

            sp->WarpPlayer(player, pos->x + delta_x, pos->y + delta_y, vel_x, vel_y, (pos->rotation + 20) % 40, 0);

            if(kb_adata->loaded)
            {
                obj_id = kb_adata->lvz_objid_last;

                if(obj_id < kb_adata->lvz_objid_offset || obj_id >= kb_adata->lvz_objid_offset + kb_adata->lvz_objid_count)
                    obj_id = kb_adata->lvz_objid_offset;

                kb_adata->lvz_objid_last = obj_id + 1;

                show_lvz(player->arena, obj_id, kb_adata->lvz_imgid_blink, pos->x - (kb_adata->lvz_img_blink_w >> 1), pos->y - (kb_adata->lvz_img_blink_h >> 1));
            }
            break;
    }

    // Charge management...
    --kb_status->charges;
    chat->SendMessage(player, "K-Charges Remaining: %d", kb_status->charges);

    if(!kb_status->recharging && *((u8*)kb_status) && kb_status->charges < kb_status->max_charges && kb_status->recharge_time)
    {
        kb_status->recharging = 1;
        ml->SetTimer(timer_recharge, kb_status->recharge_time, kb_status->recharge_time, player, player);
    }

    // Clear action...
    kb_status->action = 0;
    kb_status->param = 0;
}

local void pkt_death(Player *player, byte *pkt, int len)
{
    KBStatus* kb_status = PPDATA(player, KBStatusKey);

    kb_status->charges = 0;

    // Kill old timer...
    ml->ClearTimer(timer_recharge, player);

    // Start recharge timer...
    if(*((u8*)kb_status) && kb_status->max_charges && kb_status->recharge_time)
    {
        kb_status->recharging = 1;
        ml->SetTimer(timer_recharge, kb_status->recharge_time, kb_status->recharge_time, player, player);
    }
}

// Timers...
////////////////////////////////////////////////////////////////////////////////////////////////////
local int timer_recharge(void* param)
{
    KBStatus* kb_status = PPDATA((Player*)param, KBStatusKey);

    chat->SendMessage((Player*)param, "K-Charges Remaining: %d", ++kb_status->charges);
    return (kb_status->recharging = (kb_status->charges < kb_status->max_charges));
}


// Callbacks...
////////////////////////////////////////////////////////////////////////////////////////////////////
local void cb_arenaaction(Arena *arena, int action)
{
    KBArenaData* kb_adata = P_ARENA_DATA(arena, KBArenaKey);

    switch(action)
    {
        case AA_CREATE:
        case AA_CONFCHANGED:
            kb_adata->loaded = 0;
            break;
    }
}

local void cb_playeraction(Player *player, int action, Arena *arena)
{
    switch(action)
    {
        case PA_ENTERARENA:
            update_status(player, database->getPlayerShipHull(player, player->pkt.ship));
            break;

        case PA_LEAVEARENA:
            ml->ClearTimer(timer_recharge, player);
            break;
    }
}

local void cb_shipfreqchange(Player *player, int newship, int oldship, int newfreq, int oldfreq)
{
    update_status(player, database->getPlayerShipHull(player, newship));
}

local void cb_itemschanged(Player* player, ShipHull *hull)
{
    update_status(player, hull);
}

local void cb_shipsetchanged(Player *player, int oldshipset, int newshipset)
{
    update_status(player, database->getPlayerHull(player, player->pkt.ship, newshipset));
}


// Utility Functions
////////////////////////////////////////////////////////////////////////////////////////////////////
local void update_status(Player *player, ShipHull *hull)
{
    KBStatus* kb_status = PPDATA(player, KBStatusKey);

    if (!hull)
        return; // No hull. Likely in spec.

    if (database->getPlayerCurrentHull(player) != hull)
        return; // Different hull was updated.


    // Load item properties...
    kb_status->quickboost = (items->getPropertySumOnHull(player, hull, KB_PROP_QUICKBOOST, 0) != 0);
    kb_status->blinkstrike = (items->getPropertySumOnHull(player, hull, KB_PROP_BLINKSTRIKE, 0) != 0);

    kb_status->recharging = 0;

    kb_status->charges = 0;
    kb_status->max_charges = items->getPropertySumOnHull(player, hull, KB_PROP_MAXCHARGES, 0);
    kb_status->recharge_time = items->getPropertySumOnHull(player, hull, KB_PROP_CHARGETIME, 0) * 10;

    kb_status->boost_power = items->getPropertySumOnHull(player, hull, KB_PROP_QB_POWER, 0);

    kb_status->blink_distance = items->getPropertySumOnHull(player, hull, KB_PROP_BS_DISTANCE, 0);

    kb_status->action = 0;



    // Kill old timer...
    ml->ClearTimer(timer_recharge, player);

    // Start recharge timer...
    if(*((u8*)kb_status) && kb_status->max_charges && kb_status->recharge_time)
    {
        kb_status->recharging = 1;
        ml->SetTimer(timer_recharge, kb_status->recharge_time, kb_status->recharge_time, player, player);
    }
}

local void load_arena_config(Arena *arena)
{
    KBArenaData* kb_adata = P_ARENA_DATA(arena, KBArenaKey);

    kb_adata->lvz_objid_offset = cfg->GetInt(arena->cfg, "Kojima", "ObjIdOffset", 0);
    kb_adata->lvz_objid_count = cfg->GetInt(arena->cfg, "Kojima", "ObjIdCount", 0);

    kb_adata->lvz_imgid_boost = cfg->GetInt(arena->cfg, "Kojima", "QB_Image", 0);
    kb_adata->lvz_img_boost_w = cfg->GetInt(arena->cfg, "Kojima", "QB_Image_W", 0);
    kb_adata->lvz_img_boost_h = cfg->GetInt(arena->cfg, "Kojima", "QB_Image_H", 0);

    kb_adata->lvz_imgid_blink = cfg->GetInt(arena->cfg, "Kojima", "BS_Image", 0);
    kb_adata->lvz_img_blink_w = cfg->GetInt(arena->cfg, "Kojima", "BS_Image_W", 0);
    kb_adata->lvz_img_blink_h = cfg->GetInt(arena->cfg, "Kojima", "BS_Image_H", 0);

    kb_adata->loaded = 1;
}

local void show_lvz(Arena *arena, int obj_id, int img_id, int x, int y)
{
    LVZCluster objCluster;

    objCluster.packet_header = 0x00;
    objCluster.packet_id = 0x0E;
    objCluster.packet_size_1 = 12;

    objCluster.lvzmod_pkt_id = 0x36;
    objCluster.lvzmod_flags = 0x03;
    objCluster.lvzmod_obj_map = 0;
    objCluster.lvzmod_obj_id = obj_id;
    objCluster.lvzmod_pos_x = x;
    objCluster.lvzmod_pos_y = y;
    objCluster.lvzmod_img_id = img_id;
    objCluster.lvzmod_img_layer = 0;    // We don't use this...
    objCluster.lvzmod_display_time = 0; // or this.

    objCluster.packet_size_2 = 3;

    objCluster.lvztgl_pkt_id = 0x35;
    objCluster.lvztgl_obj_id = obj_id;
    objCluster.lvztgl_obj_show = 0;


    net->SendToArena(arena, 0, (byte*)&objCluster, sizeof(LVZCluster), NET_RELIABLE);
}


// Module nonsense.
////////////////////////////////////////////////////////////////////////////////////////////////////
EXPORT const char info_hs_kojima[] = "Kojima Boosters mk1 -- Chris \"Cerium\" Rog";

EXPORT int MM_hs_kojima(int action, Imodman *_mm, Arena *arena)
{
    switch(action)
    {
        case MM_LOAD:
            mm = _mm;

            // Register interfaces...
            aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
            cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
            chat = mm->GetInterface(I_CHAT, ALLARENAS);
            cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
            items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
			database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
            lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
            ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
            map = mm->GetInterface(I_MAPDATA, ALLARENAS);
            net = mm->GetInterface(I_NET, ALLARENAS);
            obj = mm->GetInterface(I_OBJECTS, ALLARENAS);
            pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
            sp = mm->GetInterface(I_SELFPOS, ALLARENAS);


            if (!aman || !cfg || !chat || !cmdman || !items || !database || !lm || !ml || !map || !net || !obj || !pd || !sp)
            {
                mm->ReleaseInterface(aman);
                mm->ReleaseInterface(cfg);
                mm->ReleaseInterface(chat);
                mm->ReleaseInterface(cmdman);
                mm->ReleaseInterface(items);
				mm->ReleaseInterface(database);
                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(ml);
                mm->ReleaseInterface(map);
                mm->ReleaseInterface(net);
                mm->ReleaseInterface(obj);
                mm->ReleaseInterface(pd);
                mm->ReleaseInterface(sp);

                return MM_FAIL;
            }


            // Arena/Player data...
            KBArenaKey = aman->AllocateArenaData(sizeof(KBArenaData));
            KBStatusKey = pd->AllocatePlayerData(sizeof(KBStatus));

            // Callbacks
            mm->RegCallback(CB_ARENAACTION, cb_arenaaction, ALLARENAS);
            mm->RegCallback(CB_PLAYERACTION, cb_playeraction, ALLARENAS);
            mm->RegCallback(CB_SHIPFREQCHANGE, cb_shipfreqchange, ALLARENAS);
			mm->RegCallback(CB_SHIPSET_CHANGED, cb_shipsetchanged, ALLARENAS);
            mm->RegCallback(CB_ITEMS_CHANGED, cb_itemschanged, ALLARENAS);

            // Packets
            net->AddPacket(C2S_POSITION, pkt_position);
            net->AddPacket(C2S_DIE, pkt_death);

            // Commands...
            cmdman->AddCommand("quickboost", cmd_quickboost, ALLARENAS, "");
            cmdman->AddCommand("blinkstrike", cmd_blinkstrike, ALLARENAS, "");

            // YAY!
            return MM_OK;

    //////////////////////////////////////////////////

        case MM_UNLOAD:
            // Timers
            ml->ClearTimer(timer_recharge, 0);

            // Commands
            cmdman->RemoveCommand("quickboost", cmd_quickboost, ALLARENAS);
            cmdman->RemoveCommand("blinkstrike", cmd_blinkstrike, ALLARENAS);

            // Packets
            net->RemovePacket(C2S_DIE, pkt_death);
            net->RemovePacket(C2S_POSITION, pkt_position);

            // Callbacks
            mm->UnregCallback(CB_ITEMS_CHANGED, cb_itemschanged, ALLARENAS);
			mm->UnregCallback(CB_SHIPSET_CHANGED, cb_shipsetchanged, ALLARENAS);
            mm->UnregCallback(CB_SHIPFREQCHANGE, cb_shipfreqchange, ALLARENAS);
            mm->UnregCallback(CB_PLAYERACTION, cb_playeraction, ALLARENAS);
            mm->UnregCallback(CB_ARENAACTION, cb_arenaaction, ALLARENAS);

            // Arena/Player Data
            pd->FreePlayerData(KBStatusKey);
            aman->FreeArenaData(KBArenaKey);


            // Release interfaces...
            mm->ReleaseInterface(aman);
            mm->ReleaseInterface(cfg);
            mm->ReleaseInterface(chat);
            mm->ReleaseInterface(cmdman);
            mm->ReleaseInterface(items);
			mm->ReleaseInterface(database);
            mm->ReleaseInterface(lm);
            mm->ReleaseInterface(ml);
            mm->ReleaseInterface(map);
            mm->ReleaseInterface(net);
            mm->ReleaseInterface(obj);
            mm->ReleaseInterface(pd);
            mm->ReleaseInterface(sp);


            return MM_OK;

    //////////////////////////////////////////////////

        case MM_ATTACH:
            // Do nothing!

            return MM_OK;

    //////////////////////////////////////////////////

        case MM_DETACH:
            // Do nothing again!

            return MM_OK;

    //////////////////////////////////////////////////

        //default:
            // watf?
    }

	return MM_FAIL;
}
