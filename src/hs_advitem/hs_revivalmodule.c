#include <stdio.h>
#include <stdlib.h>

#include "asss.h"
#include "hscore.h"

#define MODULE_NAME "revival_module"

typedef struct {
  u_int32_t lvz_image_height;
  u_int32_t lvz_image_width;
  u_int32_t lvz_object_count;
  u_int32_t lvz_object_offset;
  u_int32_t lvz_object_last;
} RMArenaData;

typedef struct {
  u_int32_t revival_chance;
  ticks_t revival_warmup;
  ticks_t revival_cooldown;
  ticks_t last_spawn;
  ticks_t last_activation;
} RMPlayerData;

#pragma pack(push,1)
typedef struct {
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

static void ReadRevivalItemStats(Player *player);
static void RevivePlayer(Player *player);
static void DisplayLVZ(Arena *arena, int obj_id, int x, int y);
static void OnArenaAttach(Arena *arena);
static void OnArenaDetach(Arena *arena);
static void OnArenaAction(Arena *arena, int action);
static void OnPlayerDeath(Arena *arena, Player **killer, Player **killed, int *bounty);

// Interfaces
static Imodman *mm;

static Iarenaman *arenaman;
static Iconfig *cfg;
static Ichat *chat;
static Iflagcore *flagcore;
static Igame *game;
static Ihscoreitems *items;
static Ilogman *lm;
static Inet *net;
static Iplayerdata *pd;
static Iprng *prng;

// Global resource identifiers
static int adkey;
static int pdkey;

// Advisor to check for activation
static Akill kill_advisor = {
  ADVISER_HEAD_INIT(A_KILL)

  NULL,
  OnPlayerDeath
};

////////////////////////////////////////////////////////////////////////////////////////////////////

static void ReadRevivalItemStats(Player *player) {
  RMPlayerData *pdata = PPDATA(player, pdkey);

  if (player->p_ship != SHIP_SPEC) {
    pdata->revival_chance = items->getPropertySum(player, player->p_ship, "revival_chance", 0);
    pdata->revival_warmup = items->getPropertySum(player, player->p_ship, "revival_warmup", 0);
    pdata->revival_cooldown = items->getPropertySum(player, player->p_ship, "revival_cooldown", 0);
  }
  else {
    pdata->revival_chance = 0;
    pdata->revival_warmup = 0;
    pdata->revival_cooldown = 0;
  }
}

static void RevivePlayer(Player *player) {
  RMArenaData *adata = P_ARENA_DATA(player->arena, adkey);

  Target target;
  target.type = T_PLAYER;
  target.u.p = player;

  game->ShipReset(&target);

  // Display LVZ if we have one...
  if (adata->lvz_object_count) {
    int x = player->position.x - (adata->lvz_image_width >> 1);
    int y = player->position.y - (adata->lvz_image_height >> 1);

    DisplayLVZ(player->arena, adata->lvz_object_offset + adata->lvz_object_last, x, y);
    adata->lvz_object_last = (adata->lvz_object_last + 1) % adata->lvz_object_count;
  }
}

static void DisplayLVZ(Arena *arena, int obj_id, int x, int y) {
  LVZCluster cluster;

  cluster.packet_header = 0x00;
  cluster.packet_id = 0x0E;
  cluster.packet_size_1 = 12;

  cluster.lvzmod_pkt_id = 0x36;
  cluster.lvzmod_flags = 0x03;
  cluster.lvzmod_obj_map = 0;
  cluster.lvzmod_obj_id = obj_id;
  cluster.lvzmod_pos_x = x;
  cluster.lvzmod_pos_y = y;
  cluster.lvzmod_img_id = 0;
  cluster.lvzmod_img_layer = 0;    // We don't use this...
  cluster.lvzmod_display_time = 0; // or this.

  cluster.packet_size_2 = 3;

  cluster.lvztgl_pkt_id = 0x35;
  cluster.lvztgl_obj_id = obj_id;
  cluster.lvztgl_obj_show = 0;

  net->SendToArena(arena, NULL, (byte*)&cluster, sizeof(LVZCluster), NET_RELIABLE);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void OnArenaAttach(Arena *arena) {
  RMArenaData *adata = P_ARENA_DATA(arena, adkey);

  adata->lvz_object_count = 0;
  adata->lvz_object_offset = 0;
  adata->lvz_object_last = 0;
}

static void OnArenaDetach(Arena *arena) {

}

static void OnArenaAction(Arena *arena, int action) {
  RMArenaData *adata = P_ARENA_DATA(arena, adkey);

  switch (action) {
    case AA_CREATE:
    case AA_CONFCHANGED:
      /* cfghelp: RevivalModule:LVZImageWidth, arena, int, def: 0
       * The width of the image, in pixels, to be displayed when a player is revived by this
       * module. */
      adata->lvz_image_width = cfg->GetInt(arena->cfg, "RevivalModule", "LVZImageWidth", 0);

      /* cfghelp: RevivalModule:LVZImageHeight, arena, int, def: 0
       * The height of the image, in pixels, to be displayed when a player is revived by this
       * module. */
      adata->lvz_image_height = cfg->GetInt(arena->cfg, "RevivalModule", "LVZImageHeight", 0);

      /* cfghelp: RevivalModule:LVZObjectCount, arena, int, def: 0
       * The number of LVZ object IDs available for the revival module. If set to 0, no LVZs will
       * be triggered for revival module functionality. */
      adata->lvz_object_count = cfg->GetInt(arena->cfg, "RevivalModule", "LVZObjectCount", 0);

      /* cfghelp: RevivalModule:LVZObjectOffset, arena, int, def: 0
       * The first LVZ object ID to use when displaying LVZs for the revival module. */
      adata->lvz_object_offset = cfg->GetInt(arena->cfg, "RevivalModule", "LVZObjectOffset", 0);
      break;
  }
}

static void OnPlayerDeath(Arena *arena, Player **killer, Player **killed, int *bounty) {
  RMPlayerData *pdata = PPDATA(*killed, pdkey);

  if (pdata->revival_chance) {
    ticks_t now = current_ticks();

    // Check if we've passed the warmup duration
    if (pdata->revival_warmup && TICK_DIFF(now, pdata->last_spawn) < pdata->revival_warmup) {
      return;
    }

    // Check if we're still in a cooldown
    if (pdata->revival_cooldown && pdata->last_activation && TICK_DIFF(now, pdata->last_activation) < pdata->revival_cooldown) {
      return;
    }

    // Make sure they're not carrying flags, as we can't restore flag state
    if (flagcore->CountPlayerFlags(*killed)) {
      return;
    }

    // Check if we trigger...
    int roll = prng->Rand() % 100;
    if (roll < pdata->revival_chance) {
      // Negate the death packet
      *killer = NULL;

      // Revive the player & restore flags and whatever other magic we need to do...
      RevivePlayer(*killed);

      // Update timers...
      pdata->last_activation = now;
    }
  }
}

static void OnPlayerSpawn(Player *player, int reason) {
  if (reason != SPAWN_SHIPRESET) {
    RMPlayerData *pdata = PPDATA(player, pdkey);
    pdata->last_spawn = current_ticks();
  }
}

static void OnPlayerFreqShipChange(Player *player, int newship, int oldship, int newfreq, int oldfreq) {
  ReadRevivalItemStats(player);
}

static void OnPlayerShipsetChange(Player *player, int oldshipset, int newshipset) {
  ReadRevivalItemStats(player);
}

static void OnPlayerItemCountChange(Player *player, ShipHull *hull, Item *item, InventoryEntry *entry, int newCount, int oldCount) {
  ReadRevivalItemStats(player);
}

static void OnFlagLoss(Arena *a, Player *p, int fid, int how) {
  lm->Log(L_INFO, "FLAG LOST! Player: %s, Flag: %d, reason; %d", p->name, fid, how);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Attempts to get the interfaces required by this module. Will not retrieve interfaces twice.
 *
 * @param *modman
 *  The module manager; necessary to get other interfaces.
 *
 * @param *arena
 *  The arena for which the interfaces should be retrieved.
 *
 * @return
 *  True if all of the required interfaces were retrieved; false otherwise.
 */
static int GetInterfaces(Imodman *modman, Arena *arena)
{
  if (modman && !mm) {
    mm = modman;

    arenaman  = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    cfg       = mm->GetInterface(I_CONFIG, ALLARENAS);
    chat      = mm->GetInterface(I_CHAT, ALLARENAS);
    flagcore  = mm->GetInterface(I_FLAGCORE, ALLARENAS);
    game      = mm->GetInterface(I_GAME, ALLARENAS);
    items     = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    net       = mm->GetInterface(I_NET, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    prng      = mm->GetInterface(I_PRNG, ALLARENAS);

    return mm &&
      (arenaman && cfg && chat && flagcore && game && items && lm && net && pd && prng);
  }

  return 0;
}

/**
 * Releases any allocated interfaces. If the interfaces have already been released, this function
 * does nothing.
 */
static void ReleaseInterfaces()
{
  if (mm) {
    mm->ReleaseInterface(arenaman);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(chat);
    mm->ReleaseInterface(flagcore);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(net);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(prng);

    mm = NULL;
  }
}

EXPORT const char info_hs_revivalmodule[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hs_revivalmodule(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate object data
      adkey = arenaman->AllocateArenaData(sizeof(RMArenaData));
      if (adkey == -1) {
        printf("<%s> Unable to allocate per-arena data.\n", MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-arena data.", MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      pdkey = pd->AllocatePlayerData(sizeof(RMPlayerData));
      if (pdkey == -1) {
        printf("<%s> Unable to allocate per-player data.\n", MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);
      mm->RegCallback(CB_SHIPFREQCHANGE, OnPlayerFreqShipChange, arena);
      mm->RegCallback(CB_SPAWN, OnPlayerSpawn, arena);
      mm->RegCallback(CB_SHIPSET_CHANGED, OnPlayerShipsetChange, arena);
      mm->RegCallback(CB_ITEM_COUNT_CHANGED, OnPlayerItemCountChange, arena);
      mm->RegCallback(CB_FLAGLOST, OnFlagLoss, arena);

      mm->RegAdviser(&kill_advisor, arena);
      OnArenaAttach(arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      OnArenaDetach(arena);
      mm->UnregAdviser(&kill_advisor, arena);

      mm->UnregCallback(CB_FLAGLOST, OnFlagLoss, arena);
      mm->UnregCallback(CB_ITEM_COUNT_CHANGED, OnPlayerItemCountChange, arena);
      mm->UnregCallback(CB_SHIPSET_CHANGED, OnPlayerShipsetChange, arena);
      mm->UnregCallback(CB_SHIPFREQCHANGE, OnPlayerFreqShipChange, arena);
      mm->UnregCallback(CB_SPAWN, OnPlayerSpawn, arena);
      mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_UNLOAD:
      pd->FreePlayerData(pdkey);
      arenaman->FreeArenaData(adkey);

      ReleaseInterfaces();
      return MM_OK;
  }

  return MM_FAIL;
}