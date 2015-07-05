#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"

#ifdef USE_AKD_LAG
#include "akd_lag.h"
#endif

// Interfaces
local Imodman *mm;
local Ihscoreitems *items;
local Iconfig *cfg;
local Icmdman *cmd;
local Ichat *chat;
local Igame *game;
local Ilagquery *lagq;
local Inet *net;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iobjects *objs;
local Imapdata *map;
local Ihscoredatabase *db;
#ifdef USE_AKD_LAG
local Iakd_lag *akd_lag;
#endif

//Structs and stuff
typedef struct Coordinates
{
    int x;
    int y;
} Coordinates;

typedef struct ArenaData
{
    bool attached;
    int currentID;

    struct
    {
        int startID;
        int endID;
        int xOffset;
        int yOffset;
    } lvzConfig;

    struct
    {
        int radius;
    } shipconfig[8];
} ArenaData;
local int adkey = -1;

local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

#define PlayerData PlayerData_
typedef struct PlayerData
{
    //Blink
    bool canBlink;
    int blinkDelay;
    int blinkDistance;
    int blinkMin;
    int blinkAWEffect;
    int blinkRadius;
    bool blinkIgnoreWalls;

    ticks_t lastBlink;
    bool blinkActivated; //If 'true', boost player on next position packet and set to 'false'
    int blinkDirection; //If boostActivated is true, this is the direction to boost on next position packet
} PlayerData;
local int pdkey = -1;

local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Prototypes of -everything-, because I'm cool like that
local void Cblink(const char *cmd, const char *params, Player *p, const Target *target);

local void PPKCB(Player *p, struct C2SPosition *pos);
local void arenaActionCB(Arena *arena, int action);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local int getNumberOfAwers(Player *p);
local void updatePlayerData(Player *p, ShipHull *hull, bool lock);
local bool fitsOnMap(Arena *arena, int x_, int y_, int radius);
local Coordinates isClearPath(Arena *arena, int x1, int y1, int x2, int y2, int radius);
local void readConfig(Arena *arena);
local void WarpPlayerWithFlash(Player *p, int dest_x, int dest_y, int v_x, int v_y, int rotation, int delta_t);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Commands
local void Cblink(const char *cmd, const char *params, Player *p, const Target *target)
{
    if (target->type != T_ARENA)
        return; //Ignore non-pub commands

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->canBlink)
    {
        chat->SendMessage(p, "You do not have an item capable of blink on your ship.");
        return;
    }

    if (TICK_DIFF(current_ticks(), pdata->lastBlink) < pdata->blinkDelay)
    {
        chat->SendMessage(p, "Blink is currently recharging.");
        return;
    }

    pdata->blinkActivated = true;

    if (!params || !*params)
        pdata->blinkDirection = rand()%360;
    else
        pdata->blinkDirection = atoi(params);
}

//Callbacks
local void PPKCB(Player *p, struct C2SPosition *pos)
{
    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

    if (p->flags.is_dead) //Reset dead players' blink and return
    {
        pdata->blinkActivated = false;
        pdata->blinkDirection = 0;
        return;
    }

    if (pdata->blinkActivated == true)
    {
        double theta;
        theta = (((40 - (pos->rotation + 30) % 40) * 9) + pdata->blinkDirection) * (M_PI / 180);

        //Reset variables
        pdata->blinkActivated = false;
        pdata->blinkDirection = 0;

        int numAwers = getNumberOfAwers(p);
        int dist = pdata->blinkDistance + numAwers * pdata->blinkAWEffect;

        if (dist < pdata->blinkMin)
            dist = pdata->blinkMin;

        int x = pos->x + dist * cos(theta);
        int y = pos->y + dist * -sin(theta);

        if (pdata->blinkIgnoreWalls == false)
        {
            Coordinates pathEnd = isClearPath(p->arena, pos->x, pos->y, x, y, (adata->shipconfig[p->p_ship].radius + pdata->blinkRadius));
            x = pathEnd.x;
            y = pathEnd.y;

            if (x == pos->x && y == pos->y) //Useless blink, don't do it.
            {
                chat->SendMessage(p, "Unable to blink. Try moving into open space.");
                return;
            }
        }

        //'restart' blink delay and warp player
        pdata->lastBlink = current_ticks();
        WarpPlayerWithFlash(p, x, y, pos->xspeed, pos->yspeed, pos->rotation, 0);

        //Play LVZ at start location
        Target t;
        t.type = T_ARENA;
        t.u.arena = p->arena;

        objs->Move(&t, adata->currentID, pos->x + adata->lvzConfig.xOffset,
                                   pos->y + adata->lvzConfig.yOffset, 0, 0);
        objs->Toggle(&t,adata->currentID,1);

        adata->currentID++;
        if (adata->currentID > adata->lvzConfig.endID)
            adata->currentID = adata->lvzConfig.startID;

        //Play LVZ at end location
        objs->Move(&t, adata->currentID, x + adata->lvzConfig.xOffset,
                                        y + adata->lvzConfig.yOffset, 0, 0);
        objs->Toggle(&t,adata->currentID,1);

        adata->currentID++;
        if (adata->currentID > adata->lvzConfig.endID)
            adata->currentID = adata->lvzConfig.startID;
    }
}

local void arenaActionCB(Arena *arena, int action)
{
    if (action == AA_CONFCHANGED)
        readConfig(arena);
}

local void itemsChangedCB(Player *p, ShipHull *hull)
{
    if (db->getPlayerCurrentHull(p) == hull)
        updatePlayerData(p, hull, false);
}

local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    //Reset delays
    PlayerData *pdata = getPlayerData(p);
    pdata->lastBlink = current_ticks();

    updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
}

//Misc/Utilities
local int getNumberOfAwers(Player *p)
{
    LinkedList awers;
    int numAwers = 0;

    LLInit(&awers);

    game->IsAntiwarped(p, &awers);
    numAwers = LLCount(&awers);

    LLEmpty(&awers);

    return numAwers;
}

local void updatePlayerData(Player *p, ShipHull *hull, bool lock)
{
    PlayerData *pdata = getPlayerData(p);

    if (!hull)
        return;

    if(hull->ship == SHIP_SPEC)
    {
        pdata->canBlink = false;
        pdata->blinkActivated = false;
        return;
    }


    pdata->blinkActivated = false;

    if (lock == true)
        db->lock();

    //Blink variables
    pdata->canBlink = (items->getPropertySumOnHull(p, hull, "blink", 0) > 0);
    pdata->blinkDelay = items->getPropertySumOnHull(p, hull, "blinkdelay", 0);
    pdata->blinkDistance = items->getPropertySumOnHull(p, hull, "blinkdistance", 0);
    pdata->blinkMin = items->getPropertySumOnHull(p, hull, "blinkmin", 0);
    pdata->blinkAWEffect = items->getPropertySumOnHull(p, hull, "blinkaweffect", 0);
    pdata->blinkRadius = items->getPropertySumOnHull(p, hull, "blinkradius", 0);
    pdata->blinkIgnoreWalls = (items->getPropertySumOnHull(p, hull, "blinkignorewalls", 0) > 0);

    if (lock == true)
        db->unlock();
}

local bool fitsOnMap(Arena *arena, int x_, int y_, int radius)
{
    int startTileX, endTileX;
    int startTileY, endTileY;
    enum map_tile_t tile;
    int x, y;

    startTileX = (x_ - radius) >> 4;
    endTileX = (x_ + radius) >> 4;

    startTileY = (y_ - radius) >> 4;
    endTileY = (y_ + radius) >> 4;

    for (x = startTileX; x <= endTileX; x++)
    {
        for (y = startTileY; y <= endTileY; y++)
        {
            tile = map->GetTile(arena, x, y);

            // @todo:
            // Redo this mess. Team bricks count as obstructing blocks, as do open doors.

            if ((tile >= 1 && tile <= 169) || // normal solid tiles, doors
                (tile >= 191 && tile <= 252) || // special solid tiles or anything that warps ship or removes weapons
                tile == 171 // safe zone
                )
            {
                return false;
            }
        }
    }

    return true;
}

// Lower is more precise but takes longer to calculate
#define PATHCLEAR_INCREASE (8)
/* http://www.gamedev.net/reference/articles/article767.asp */
// Returns a struct with the last unobstructed coordinates (destination if non-obstructed)
local Coordinates isClearPath(Arena *arena, int x1, int y1, int x2, int y2, int radius)
{
    Coordinates result = {x1, y1};

    int i, dx, dy, numpixels;
    int d, dinc1, dinc2;
    int x, xinc1, xinc2;
    int y, yinc1, yinc2;

    /* calculate deltax and deltay for initialisation */
    dx = x2 - x1;
    dy = y2 - y1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    /* initialize all vars based on which is the independent variable */
    if (dx > dy)
    {
        /* x is independent variable */
        numpixels = dx + 1;
        d = (2 * dy) - dx;
        dinc1 = dy << 1;
        dinc2 = (dy - dx) << 1;
        xinc1 = 1;
        xinc2 = 1;
        yinc1 = 0;
        yinc2 = 1;
    }
    else
    {
        /* y is independent variable */
        numpixels = dy + 1;
        d = (2 * dx) - dy;
        dinc1 = dx << 1;
        dinc2 = (dx - dy) << 1;
        xinc1 = 0;
        xinc2 = 1;
        yinc1 = 1;
        yinc2 = 1;
    }

    /* make sure x and y move in the right directions */
    if (x1 > x2)
    {
        xinc1 = - xinc1;
        xinc2 = - xinc2;
    }
    if (y1 > y2)
    {
        yinc1 = - yinc1;
        yinc2 = - yinc2;
    }

    dinc1 *= PATHCLEAR_INCREASE;
    dinc2 *= PATHCLEAR_INCREASE;
    xinc1 *= PATHCLEAR_INCREASE;
    xinc2 *= PATHCLEAR_INCREASE;
    yinc1 *= PATHCLEAR_INCREASE;
    yinc2 *= PATHCLEAR_INCREASE;

    /* start drawing at */
    x = x1;
    y = y1;

    /* trace the line */
    for(i = 1; i < numpixels; i+=PATHCLEAR_INCREASE)
    {
        if (!fitsOnMap(arena, x, y, radius))
            return result;

        /* bresenham stuff */
        if (d < 0)
        {
            d = d + dinc1;
            x = x + xinc1;
            y = y + yinc1;
        }
        else
        {
            d = d + dinc2;
            x = x + xinc2;
            y = y + yinc2;
        }

        result.x = x;
        result.y = y;
    }

    result.x = x2;
    result.y = y2;

    return result;
}

local void readConfig(Arena *arena)
{
    ArenaData *adata = getArenaData(arena);
    ConfigHandle ch = arena->cfg;

    adata->lvzConfig.startID = cfg->GetInt(ch, "Kojima", "lvzStartID", 0);
    adata->lvzConfig.endID = cfg->GetInt(ch, "Kojima", "lvzEndID", 0);
    adata->lvzConfig.xOffset = cfg->GetInt(ch, "Kojima", "lvzXOffset", 0);
    adata->lvzConfig.yOffset = cfg->GetInt(ch, "Kojima", "lvzYOffset", 0);
    adata->currentID = adata->lvzConfig.startID;

    int i;
    for (i = 0; i < 8; i++)
        adata->shipconfig[i].radius = cfg->GetInt(ch, cfg->SHIP_NAMES[i], "Radius", 0);
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
  //{
        if (lagq)
        {
            struct PingSummary pping;
            lagq->QueryPPing(p, &pping);
            lag = pping.avg;
        }
  //}

    // round to nearest tick
    if (lag % 10 >= 5)
        lag = lag/10 + 1;
    else
        lag = lag/10;

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
            p->pid, v_x, 0, p->position.status | STATUS_FLASH, 0,
            dest_y, p->position.bounty
    };

    send_warp_packet(p, delta_t, &packet);
}

//Interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
    cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
    chat = mm->GetInterface(I_CHAT, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    lagq = mm->GetInterface(I_LAGQUERY, ALLARENAS);
    net = mm->GetInterface(I_NET, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    objs = mm->GetInterface(I_OBJECTS, ALLARENAS);
    map = mm->GetInterface(I_MAPDATA, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

    #ifdef USE_AKD_LAG
        akd_lag = mm->GetInterface(I_AKD_LAG, ALLARENAS);
    #endif
}
local bool checkInterfaces()
{
    if (items && cfg && cmd && chat && game && net && pd
       && aman && objs && map && db)
    {
        #ifdef USE_AKD_LAG
            if (!akd_lag)
                return false;
        #endif

        return true;
    }
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(cmd);
    mm->ReleaseInterface(chat);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(lagq);
    mm->ReleaseInterface(net);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(objs);
    mm->ReleaseInterface(map);
    mm->ReleaseInterface(db);

    #ifdef USE_AKD_LAG
        mm->ReleaseInterface(akd_lag);
    #endif
}

EXPORT const char info_hs_kojima[] = "hs_blink revision: 16 by Spidernl\n";

local helptext_t Cblink_help =
        "Targets: none\n"
        "Arguments: <direction (degrees)> or none\n"
        "Instantly warps you in the specified direction,"
        " or in a random one if no direction is specified.";

EXPORT int MM_hs_blink(int action, Imodman *mm_, Arena *arena)
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
                pd->FreePlayerData (pdkey);

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
        if (adata->attached)
            return MM_FAIL;

        readConfig(arena);

        //Add Command(s)
        cmd->AddCommand("blink", Cblink, arena, Cblink_help);

        //Register Callback(s)
        mm->RegCallback(CB_PPK, PPKCB, arena);
        mm->RegCallback(CB_ARENAACTION, arenaActionCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);

        adata->attached = true;
        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);
        if (!adata->attached)
            return MM_FAIL;

        //Remove Command(s)
        cmd->RemoveCommand("blink", Cblink, arena);

        //Unregister Callback(s)
        mm->UnregCallback(CB_PPK, PPKCB, arena);
        mm->UnregCallback(CB_ARENAACTION, arenaActionCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);

        adata->attached = false;
        return MM_OK;
    }

    return MM_FAIL;
}