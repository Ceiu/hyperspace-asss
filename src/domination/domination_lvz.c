/**
 * hs_domination_lvz.c
 * A lvz-based graphics controller for domination games.
 *
 * @author Chris "Ceiu" Rog <ceiu@cericlabs.com>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "packets/ppk.h"

#include "hscore.h"
#include "hscore_database.h"



////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////

#define HS_MODULE_NAME "hs_domination_lvz"

#define HS_MIN(x, y) ((x) < (y) ? (x) : (y))
#define HS_MAX(x, y) ((x) > (y) ? (x) : (y))
#define HS_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

/*
Necessary lvz images:
 10 - Static numbers (percentages; scoreboard)
 63 - Capture states (21 static images per team)
 11 - Contest states
 62 - Timer states (h:mm:ss states, 10 static states, static colons & hyphens)
  3 - Team area control background
  3 - Team flag control graphic
  9 - Team safe/spawn graphics
 ?? - Map
*/

typedef struct ArenaData ArenaData;


struct ArenaData {
  u_int32_t cfg_capture_reward_radius;
  u_int32_t cfg_capture_reward_money;
  u_int32_t cfg_capture_reward_exp;
  u_int32_t cfg_capture_tick_reward_money;
  u_int32_t cfg_capture_tick_reward_exp;
  u_int32_t cfg_contest_reward_money;
  u_int32_t cfg_contest_reward_exp;
  u_int32_t cfg_defense_reward_radius;
  u_int32_t cfg_defense_reward_money;
  u_int32_t cfg_defense_reward_exp;
  u_int32_t cfg_domination_reward_money;
  u_int32_t cfg_domination_reward_exp;
};




// Interfaces
static Imodman *mm;

static Iarenaman *arenaman;
static Iconfig *cfg;
static Ichat *chat;
static Iflag *flagcore;
static Ilogman *lm;
static Iplayerdata *pd;

static Ihscoredatabase *database;

// Global resource identifiers
static int adkey;
static int pdkey;


////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////

static void ReadArenaConfig(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  // adata->cfg_contest_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);

  // adata->cfg_timer_one_hour_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_ten_minute_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_one_minute_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_ten_second_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_one_second_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_static_image_offset = cfg->getInt(arena->cfg, "Domination", "", 0);

  // adata->cfg_timer_one_hour_lvz = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_ten_minute_lvz = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_one_minute_lvz = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_ten_second_lvz = cfg->getInt(arena->cfg, "Domination", "", 0);
  // adata->cfg_timer_one_second_lvz = cfg->getInt(arena->cfg, "Domination", "", 0);
}


static void OnArenaAttach(Arena *arena) {
  ReadArenaConfig(arena);
}

static void OnArenaDetach(Arena *arena) {
  // Free arena data

}

static void OnArenaAction(Arena *arena, int action) {
  if (action == AA_CONFCHANGED) {
    ReadArenaConfig(arena);
  }
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
    flagcore  = mm->GetInterface(I_FLAG, ALLARENAS);
    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

    database  = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

    return mm && (lm && cfg && pd && chat && flagcore) && (database);
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
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(pd);

    mm->ReleaseInterface(database);

    mm = NULL;
  }
}


EXPORT const char info_hs_domination_lvz[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hs_domination_lvz(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", HS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate object data
      adkey = arenaman->AllocateArenaData(sizeof(ArenaGameData));
      if (adkey == -1) {
        printf("<%s> Unable to allocate per-arena data.\n", HS_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-arena data.", HS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // pdkey = pd->AllocatePlayerData(sizeof(PlayerStreakData));
      // if (pdkey == -1) {
      //   printf("<%s> Unable to allocate per-player data.\n", HS_MODULE_NAME);
      //   lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", HS_MODULE_NAME);
      //   ReleaseInterfaces();
      //   break;
      // }
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_UNLOAD:
      arenaman->FreeArenaData(adkey);
      // pd->FreePlayerData(pdkey);

      ReleaseInterfaces();
      return MM_OK;
  }

  return MM_FAIL;
}
