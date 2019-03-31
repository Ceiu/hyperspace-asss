/**
 * domination_lvz.c
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

#define DOM_MODULE_NAME "domination_lvz"

#define DOM_MIN(x, y) ((x) < (y) ? (x) : (y))
#define DOM_MAX(x, y) ((x) > (y) ? (x) : (y))
#define DOM_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

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



typedef struct {
  u_int32_t team_count;

  unsigned char flag_image_offset;
  u_int32_t flag_lvz_offset;

  unsigned char flag_contest_progress_image_offset;
  unsigned char flag_contest_progress_image_count;
  u_int32_t flag_contest_image_size_x;
  u_int32_t flag_contest_image_size_y;
  unsigned char flag_capture_progress_image_offset;
  unsigned char flag_capture_progress_image_count;
  u_int32_t flag_capture_image_size_x;
  u_int32_t flag_capture_image_size_y;
  u_int32_t flag_state_lvz_offset;

  u_int32_t flag_state_update_interval;

  unsigned char timer_one_hour_image_offset;
  unsigned char timer_ten_minute_image_offset;
  unsigned char timer_one_minute_image_offset;
  unsigned char timer_ten_second_image_offset;
  unsigned char timer_one_second_image_offset;

  // TODO:
  // Add region capture states, map HUD (for showing regions at all), region on-map background
  // images and maybe HUD warning/alert text
} ArenaConfig;

struct ArenaData {

  ArenaConfig config;

  HashTable flags;
  char flags_initialized;

  pthread_mutexattr_t lvz_mutex_attr;
  pthread_mutex_t lvz_mutex;


};




// Interfaces
static Imodman *mm;

static Iarenaman *arenaman;
static Iconfig *cfg;
static Ichat *chat;
static Idomination *dom;
static Ilogman *lm;
static Imainloop *mainloop;
static Iplayerdata *pd;

// Global resource identifiers
static int adkey;
static int pdkey;


////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddFlagToUpdateSet(Arena *arena, DomFlag *dflag) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  pthread_mutex_lock(&adata->lvz_mutex);

  char *key = dom->GetFlagKey(dflag);
  HashReplace(&adata->flags, key, dflag);

  pthread_mutex_unlock(&adata->lvz_mutex);
}

static void RemoveFlagFromUpdateSet(Arena *arena, DomFlag *dflag) {
  pthread_mutex_lock(&adata->lvz_mutex);

  char *key = dom->GetFlagKey(dflag);
  HashRemoveAny(&adata->flags, key, dflag);

  pthread_mutex_unlock(&adata->lvz_mutex);
}

static int OnUpdateLVZsInterval(void *param) {
  ArenaData *adata = P_ARENA_DATA((Arena*) param, adkey);
  int result = 0;



  pthread_mutex_lock(&adata->lvz_mutex);

  // Step through each flag, retrieving the current
  if (adata->flags_initialized) {



  }

  pthread_mutex_unlock(&adata->lvz_mutex);

  return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////

static void ReadArenaConfig(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  // cfg help provided by the domination module
  adata->config.team_count = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "TeamCount", 3), 2);

  adata->config.flag_image_offset = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagImageOffset", 0), 0, 255);
  adata->config.flag_lvz_offset = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagLVZOffset", 0), 0, 65535);

  adata->config.flag_contest_progress_offset = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagContestProgressImageOffset", 0), 0, 255);
  adata->config.flag_contest_progress_count = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagContestProgressImageCount", 0), 0, 100);
  adata->config.flag_contest_image_size_x = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "FlagContestProgressImageSizeX", 0), 0);
  adata->config.flag_contest_image_size_y = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "FlagContestProgressImageSizeY", 0), 0);
  adata->config.flag_capture_progress_offset = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagCaptureProgressImageOffset", 0), 0, 255);
  adata->config.flag_capture_progress_count = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagCaptureProgressImageCount", 0), 0, 100);
  adata->config.flag_capture_image_size_x = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "FlagCaptureProgressImageSizeX", 0), 0);
  adata->config.flag_capture_image_size_y = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "FlagCaptureProgressImageSizeY", 0), 0);
  adata->config.flag_state_lvz_offset = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagStateLVZOffset", 0), 0, 65535);

  adata->config.flag_state_update_interval = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "FlagStateLVZUpdateInterval", 100), 0, 500);

  // adata->config.timer_one_hour_image_offset =
  // adata->config.timer_ten_minute_image_offset =
  // adata->config.timer_one_minute_image_offset =
  // adata->config.timer_ten_second_image_offset =
  // adata->config.timer_one_second_image_offset =

  // TODO:
  // Add region capture states, map HUD (for showing regions at all), region on-map background
  // images and maybe HUD warning/alert text
}

static void OnDomFlagStateChanged(Arena *arena, DomFlag *dflag, DomFlagState prev_state, DomFlagState new_state) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  if (dom->GetGameState(arena) == DOM_GAME_STATE_ACTIVE && adata->config.flag_state_update_interval) {
    switch (new_state) {
      case DOM_FLAG_STATE_NEUTRAL:
        // Clear flag state, contest and capture progress LVZs
        RemoveFlagFromUpdateSet(arena, dflag);
        break;

      case DOM_FLAG_STATE_CONTESTED:
        // Set contest progress to 0%
        // Show contest progress LVZ
        AddFlagToUpdateSet(arena, dflag);
        break;

      case DOM_FLAG_STATE_CAPTURING:
        // Clear contest progress LVZ
        // Show capture progress LVZ
        AddFlagToUpdateSet(arena, dflag);
        break;

      case DOM_FLAG_STATE_CONTROLLED:
        // Clear contest and capture progress LVZs
        // Set flag state to the team that now controls it
        RemoveFlagFromUpdateSet(arena, dflag);
        break;

      default:
        lm->LogA(L_ERROR, DOM_MODULE_NAME, arena, "Flag state updated with an unexpected state: %d", new_state);
        // Clear contest and capture progress LVZs
        RemoveFlagFromUpdateSet(arena, dflag);
    }
  }

}

static void OnArenaAttach(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  HashInit(&adata->flags);
  adata->flags_initialized = 1;

  ReadArenaConfig(arena);
}

static void OnArenaDetach(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);



  // Deinit our hash table so we don't leak memory...
  pthread_mutex_lock(&adata->lvz_mutex);
  HashDeinit(&adata->flags);
  adata->flags_initialized = 0;
  pthread_mutex_unlock(&adata->lvz_mutex);
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
    database  = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    dom       = mm->GetInterface(I_DOMINATION, ALLARENAS);
    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    mainloop  = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

    return mm &&
      (arenaman && cfg && chat && database && dom && lm && mainloop && pd);
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
    mm->ReleaseInterface(database);
    mm->ReleaseInterface(dom);
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(mainloop);
    mm->ReleaseInterface(pd);

    mm = NULL;
  }
}


EXPORT const char info_domination_lvz[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hs_domination_lvz(int action, Imodman *modman, Arena *arena)
{
  ArenaData *adata;

  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", HS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate object data
      adkey = arenaman->AllocateArenaData(sizeof(ArenaData));
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
      adata = P_ARENA_DATA(arena, adkey);

      if (!pthread_mutexattr_init(adata->lvz_mutex_attr)) {
        pthread_mutexattr_settype(&adata->lvz_mutex_attr, PTHREAD_MUTEX_RECURSIVE);

        if (pthread_mutex_init(&adata->lvz_mutex, &adata->lvz_mutex_attr)) {
          mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);
          mm->RegCallback(CB_DOM_FLAG_STATE_CHANGED, OnDomFlagStateChanged, arena);

          OnArenaAttach(arena);
          return MM_OK;
        }
        else {
          pthread_mutexattr_destroy(&adata->lvz_mutex_attr);
        }
      }

      return MM_FAIL;

    //////////////////////////////////////////////////

    case MM_DETACH:
      adata = P_ARENA_DATA(arena, adkey);

      OnArenaDetach(arena);

      mm->UnregCallback(CB_DOM_FLAG_STATE_CHANGED, OnDomFlagStateChanged, arena);
      mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);

      if (pthread_mutex_destroy(&adata->lvz_mutex) || pthread_mutexattr_destroy(&adata->lvz_mutex_attr)) {
        // Uh oh...
        lm->LogA(L_ERROR, DOM_MODULE_NAME, arena, "Unable to destroy arena LVZ mutex and/or mutex attributes");
        return MM_FAIL;
      }

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
