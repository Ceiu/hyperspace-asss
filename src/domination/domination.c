/**
 * domination.c
 *
 * The domination module implements a variation of turf flagging loosely based on the territory
 * capture modes from games like its namesake, Unreal Tournament, and Planetside.
 *
 * The map consists of regions which are controlled by controlling the flags it contains. The game
 * ends when time runs out (if timed) or a team dominates all available regions. A team wins the
 * game by controlling the most territory when the game ends.
 *
 * The general flow of the game is as follows:
 *  - Players spawn with all regions and flags in a neutral state
 *  - A player touches a flag to put the flag in the "contested" state
 *  - After a short duration, if the flag has not been touched by an enemy, the flag enters the
 *    "capturing" state.
 *  - The player (and teammates) must defend the flag for a bit to control the flag. Once
 *    controlled, the flag provides "influence" for all containing regions.
 *  - Every game tick, teams gain or lose influence in a region based on the number of flags
 *    they control within the region. When a team has 100% influence in a region, they control the
 *    region.
 *  - Players must control flags which provide a total majority influence to maintain control of a
 *    region. Attacking players will attempt to take flags and gain more influence over a region.
 *  - Regions may be worth more or less control points (RCP), based entirely on the arena
 *    configuration.
 *  - The game ends with a team controls all regions for a duration or when the game time runs out.
 *    The team with the most region control points wins.
 *
 *
 * Notes:
 *  - The term "influence" is incredibly overloaded in this module, which may make following various
 *    states difficult. Influence is a stat which is both provided and accumulated by flags, and
 *    accumulated by regions. For instance, a flag requires acquiring influence to control. Once
 *    controlled, the flag provides influence toward a region. The flag's provided influence helps
 *    a team control a region.
 *    - Influence on a flag represents how "controlled" it is. When acquired influence > required
 *      influence, the flag flips to the controlled state.
 *    - Controlled flags provide influence on a per-tick basis to their region for the controlling
 *      team. Once the team's acquired region influence > required region influence, the region
 *      flips to the controlled state
 *
 *  - This really comes up when trying to examine a team's region influence (per tick) vs their
 *    acquired influence (total). When calling interface methods to examine these values, be sure
 *    to note the usage of terms like "acquired" and "region influence" vs simply "influence."
 *
 *
 * @author Chris "Ceiu" Rog <ceiu@cericlabs.com>
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asss.h"
#include "packets/ppk.h"

#include "domination.h"

// TODO:
// - Deal with synchronization issues. Lots of timers and junk here, which likely means lots of
//   threads. Need locks all over the place in here.

// Immediate TODO:
// - Finish the game end stuff (game state change, clear game timers, do callbacks, prepare next game)
// - Do game start condition checks
// - Do game abort events (player counts)

////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////

#define DOM_MODULE_NAME "domination"

#define DOM_MIN(x, y) ((x) < (y) ? (x) : (y))
#define DOM_MAX(x, y) ((x) > (y) ? (x) : (y))
#define DOM_CLAMP(val, min, max) DOM_MIN(DOM_MAX((val), (min)), (max))

#define DOM_TICK_DURATION 100

#define DOM_DEBUG L_ERROR



struct DomArena {
  Arena *arena;

  HashTable teams;
  char teams_initialized;

  HashTable regions;
  char regions_initialized;

  HashTable flags;
  char flags_initialized;

  DomGameState game_state;
  u_int32_t game_time_remaining;

  char updating_flag_status;

  u_int32_t cfg_team_count;
  u_int32_t cfg_region_count;
  u_int32_t cfg_domination_countdown;
  u_int32_t cfg_min_players;
  u_int32_t cfg_min_players_per_team;
  u_int32_t cfg_game_duration;
  u_int32_t cfg_game_cooldown;
  u_int32_t cfg_game_cooldown_random;
  u_int32_t cfg_flag_capture_time;
  u_int32_t cfg_flag_contest_time;
  u_int32_t cfg_defense_reward_frequency;
  u_int32_t cfg_defense_reward_radius;
};

struct DomTeam {
  char key[16];

  Arena *arena;
  int team_id;

  HashTable players;

  char team_name[33]; // 32+null

  u_int32_t cfg_team_freq;
};

struct DomRegion {
  char key[16];

  Arena *arena;
  int region_id;
  Region *region;

  DomRegionState state;

  HashTable flags;

  DomTeam *controlling_team;
  u_int32_t acquired_influence;
  ticks_t last_influence_update;

  /* The name of the region */
  const char *cfg_region_name;

  /* Amount of control points provided by this region */
  u_int32_t cfg_region_value;

  /* Amount of influence required to control this region */
  u_int32_t cfg_required_influence;
};

struct DomFlag {
  char key[16];

  Arena *arena;
  int flag_id;
  int x, y;

  DomFlagState state;

  HashTable regions;

  /* Controlling freq and their current influence */
  DomTeam *controlling_team;
  u_int32_t acquired_influence;
  ticks_t last_influence_update;

  /* The freq that controls the physical flag; used for state management */
  DomTeam *flag_team;

  /* Amount of influence provided by this flag */
  u_int32_t cfg_flag_influence;
};

struct DomPlayer {
  u_int32_t playtime;
  ticks_t last_join_time;
};


// Interfaces
static Imodman *mm;

static Iarenaman *arenaman;
static Iconfig *cfg;
static Ichat *chat;
static Iflagcore *flagcore;
static Ilogman *lm;
static Imainloop *mainloop;
static Imapdata *mapdata;
static Iplayerdata *pd;

// Global resource identifiers
static int adkey;
// static int pdkey;



////////////////////////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////////////////////////

static void SetErrorState(Arena *arena, const char* message, ...);
static void InitArenaData(Arena *arena);
static DomRegion* AllocRegion(Arena *arena);
static int LoadRegionData(Arena *arena);
static void FreeRegion(DomRegion *dregion);
static void FreeRegionData(Arena *arena);
static DomFlag* AllocFlag(Arena *arena);
static int LoadFlagData(Arena *arena);
static void FreeFlag(DomFlag *dflag);
static void FreeFlagData(Arena *arena);
static DomTeam* AllocTeam(Arena *arena);
static int LoadTeamData(Arena *arena);
static void FreeTeam(DomTeam *dteam);
static void FreeTeamData(Arena *arena);
static int ReadArenaConfig(Arena *arena);

static DomFlag* GetDomFlag(Arena *arena, int flag_id);
static DomTeam* GetDomTeam(Arena *arena, int freq);
static DomRegion* GetDomRegion(Arena *arena, const char *region_name);
static int GetFlagProvidedInfluence(DomFlag *dflag);
static int GetFlagContestTime(DomFlag *dflag);
static int GetFlagCaptureTime(DomFlag *dflag);
static int GetFlagAcquiredInfluence(DomFlag *dflag, DomTeam *dteam);
static DomTeam* GetFlagControllingTeam(DomFlag *dflag);
static DomTeam* GetFlagEntityControllingTeam(DomFlag *dflag);
static DomFlagState GetFlagState(DomFlag *dflag);
static void SetFlagState(DomFlag *dflag, DomFlagState state, DomTeam *controlling_team, int acquired_influence, DomTeam *flag_entity_team);
static char* GetTeamName(DomTeam *dteam);
static int GetTeamRegionInfluence(DomTeam *dteam, DomRegion *dregion);
static int GetTeamAcquiredRegionInfluence(DomTeam *dteam, DomRegion *dregion);
static int GetTeamAcquiredControlPoints(DomTeam *dteam);
static int GetRegionProvidedControlPoints(DomRegion *dregion);
static int GetRegionRequiredInfluence(DomRegion *dregion);
static int GetRegionPotentialInfluence(DomRegion *dregion);
static int GetRegionMinimumInfluence(DomRegion *dregion);
static DomTeam* GetRegionControllingTeam(DomRegion *dregion);
static DomTeam* GetRegionInfluentialTeam(DomRegion *dregion);
static DomRegionState GetRegionState(DomRegion *dregion);
static void SetRegionState(DomRegion *dregion, DomRegionState state, DomTeam *dteam, int influence);
static DomTeam* GetDominatingTeam(Arena *arena);
static DomGameState GetGameState(Arena *arena);
static void SetGameState(Arena *arena, DomGameState state);
static void SetAlertState(Arena *arena, DomAlertType type, char enabled, ticks_t duration);

static void UpdateFlagAcquiredInfluence(DomFlag *dflag);
static void UpdateRegionAcquiredInfluence(DomRegion *dregion);
static void ClearFlagTimers(DomFlag *dflag);
static void ClearRegionTimers(DomRegion *dregion);
static void ClearGameTimers(Arena *arena);
static int UpdateFlagStateTimer(void *param);
static void UpdateFlagState(Arena *arena);
static void UpdateDominationState(Arena *arena);

static void OnArenaAttach(Arena *arena);
static void OnArenaDetach(Arena *arena);
static void OnArenaAction(Arena *arena, int action);
static void OnFlagGameInit(Arena *arena);
static void OnFlagTouch(Arena *arena, Player *player, int flag_id);
static int OnFlagContestTimer(void *param);
static int OnFlagHalfCaptureTimer(void *param);
static int OnFlagFullCaptureTimer(void *param);
static void OnFlagStateChange(Arena *arena, DomFlag *dflag, DomFlagState prev_state, DomFlagState new_state);
static int OnRegionHalfCaptureTimer(void *param);
static int OnRegionFullCaptureTimer(void *param);
static int OnRegionNeutralizeTimer(void *param);
static void OnRegionStateChange(Arena *arena, DomRegion *dregion, DomRegionState prev_state, DomRegionState new_state);
static void OnFlagCleanup(Arena *arena, int flag_id, int reason, Player *carrier, int freq);
static void OnFlagReset(Arena *arena, int freq, int points);
static void OnGameStateChange(Arena *arena, DomGameState old_state, DomGameState new_state);
static int OnDominationTimer(void *param);
static int OnGameStartTimer(void *param);
static int OnGameEndTimer(void *param);
static void OnGameAlert(Arena *arena, DomAlertType type, DomAlertState state, ticks_t duration);
static void OnPlayerAction(Player *player, int action, Arena *arena);
static void OnPlayerDeath(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
static void OnPlayerSpawn(Player *player, int reason);
static void OnPlayerFreqShipChange(Player *player, int newship, int oldship, int newfreq, int oldfreq);

////////////////////////////////////////////////////////////////////////////////////////////////////

static void SetErrorState(Arena *arena, const char* message, ...) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  SetGameState(arena, DOM_GAME_STATE_ERROR);

  Link *link;
  Player *player;
  LinkedList set;

  LLInit(&set);
  FOR_EACH_PLAYER(player) {
    if (player->arena == arena) {
      LLAdd(&set, player);
    }
  }

  va_list args;
  va_start(args, message);
  lm->LogA(L_ERROR, DOM_MODULE_NAME, arena, message, args);
  chat->SendAnyMessage(&set, MSG_SYSOPWARNING, SOUND_BEEP2, NULL, message, args);
  va_end(args);

  LLEmpty(&set);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void InitArenaData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  adata->teams_initialized = 0;
  adata->regions_initialized = 0;
  adata->flags_initialized = 0;

  adata->game_state = DOM_GAME_STATE_ACTIVE;
  adata->game_time_remaining = 0;

  adata->updating_flag_status = 0;
}

static DomRegion* AllocRegion(Arena *arena) {
  DomRegion *dregion = malloc(sizeof(DomRegion));

  if (dregion) {
    dregion->arena = arena;
    dregion->region_id = -1;
    dregion->region = NULL;

    dregion->state = DOM_REGION_STATE_NEUTRAL;

    dregion->cfg_region_name = NULL;
    dregion->cfg_region_value = 0;
    dregion->cfg_required_influence = 0;

    dregion->controlling_team = NULL;
    dregion->acquired_influence = 0;

    HashInit(&dregion->flags);
  } else {
    SetErrorState(arena, "ERROR: Unable to allocate memory for a new DomRegion instance");
  }

  return dregion;
}

static int LoadRegionData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomRegion *dregion;

  if (!adata->regions_initialized) {
    HashInit(&adata->regions);
    adata->regions_initialized = 1;
  }

  int region_id = 0;
  char cfg_key[255];
  char hash_key[16];

  LinkedList *keys;
  Link *link;
  char *key;

  for (region_id = 0; region_id < adata->cfg_region_count; ++region_id) {
    sprintf(hash_key, "region-%d", region_id);

    dregion = (DomRegion*) HashGetOne(&adata->regions, hash_key);
    if (!dregion) {
      if (!(dregion = AllocRegion(arena))) {
        return 0;
      }

      strcpy(dregion->key, hash_key);
      dregion->region_id = region_id;
    }
    else {
      HashDeinit(&dregion->flags);
      HashInit(&dregion->flags);
    }

    /* cfghelp: Domination:Region1-MapRegionName, arena, string
     * The name of the map region to use for this domination region. If omitted or invalid, an
     * error will be raised and the game will not start.
     * Note: this setting is case-sensitive. */
    sprintf(cfg_key, "Region%d-%s", (dregion->region_id + 1), "MapRegionName");
    dregion->cfg_region_name = cfg->GetStr(arena->cfg, "Domination", cfg_key);
    dregion->region = mapdata->FindRegionByName(arena, dregion->cfg_region_name);

    /* cfghelp: Domination:Region1-Value, arena, int, default: 1
     * The amount of control points this region is worth when controlled. If set to a value lower
     * than 1, the value will be set to 1. */
    sprintf(cfg_key, "Region%d-%s", (dregion->region_id + 1), "Value");
    dregion->cfg_region_value = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", cfg_key, 1), 1);

    /* cfghelp: Domination:Region1-RequiredInfluence, arena, int, default: 100
     * The required amount of influence to control this region. If set to a value lower than 1,
     * the region will require 1 influence. */
    sprintf(cfg_key, "Region%d-%s", (dregion->region_id + 1), "RequiredInfluence");
    dregion->cfg_required_influence = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", cfg_key, 300), 1);

    if (!dregion->region) {
      SetErrorState(arena, "ERROR: Invalid value defined for RegionName for region %d: %s", (dregion->region_id + 1), dregion->cfg_region_name);
      return 0;
    }

    // Map flags to region, if they've already been loaded...
    if (adata->flags_initialized) {
      keys = HashGetKeys(&adata->flags);
      FOR_EACH(keys, key, link) {
        DomFlag *dflag = HashGetOne(&adata->flags, key);

        if (dflag && mapdata->Contains(dregion->region, dflag->x, dflag->y)) {
          HashReplace(&dregion->flags, dflag->key, dflag);
          HashReplace(&dflag->regions, dregion->key, dregion);
        }
      }
    }

    HashReplace(&adata->regions, dregion->key, dregion);
  }

  // Check if we need to prune some regions...
  if (adata->regions.ents > region_id) {
    for (int i = region_id; adata->regions.ents > region_id; ++i) {
      sprintf(hash_key, "region-%d", i);

      dregion = (DomRegion*) HashRemoveAny(&adata->regions, hash_key);
      if (dregion) {
        FreeRegion(dregion);
      }
    }
  }

  if (!adata->regions.ents) {
    SetErrorState(arena, "ERROR: No regions were configured in this arena.");
    return 0;
  }

  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "Loaded data for %d regions", adata->regions.ents);
  return 1;
}

static void FreeRegion(DomRegion *dregion) {
  DomArena *adata = P_ARENA_DATA(dregion->arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  // Remove region from any flags that map to it...
  if (adata->flags_initialized) {
    keys = HashGetKeys(&adata->flags);
    FOR_EACH(keys, key, link) {
      DomFlag *dflag = HashGetOne(&adata->flags, key);
      if (dflag) {
        HashRemoveAny(&dflag->regions, dregion->key);
      }
    }
  }

  HashDeinit(&dregion->flags);

  free(dregion);
}

static void FreeRegionData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  if (adata->flags_initialized) {
    // We need to free region data associated with flags as well, or we risk segfaulting
    keys = HashGetKeys(&adata->flags);
    FOR_EACH(keys, key, link) {
      DomFlag *dflag = HashGetOne(&adata->flags, key);

      if (dflag) {
        HashDeinit(&dflag->regions);
        HashInit(&dflag->regions);
      }
    }
  }

  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion) {
        FreeRegion(dregion);
      }
    }

    HashDeinit(&adata->regions);
    adata->regions_initialized = 0;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static DomFlag* AllocFlag(Arena *arena) {
  DomFlag *dflag = malloc(sizeof(DomFlag));

  if (dflag) {
    dflag->arena = arena;
    dflag->flag_id = -1;
    dflag->x = -1;
    dflag->y = -1;

    dflag->state = DOM_FLAG_STATE_NEUTRAL;

    dflag->controlling_team = NULL;
    dflag->acquired_influence = 0;
    dflag->last_influence_update = 0;

    dflag->flag_team = NULL;

    dflag->cfg_flag_influence = 0;

    HashInit(&dflag->regions);
  } else {
    SetErrorState(arena, "ERROR: Unable to allocate memory for a new DomFlag instance");
  }

  return dflag;
}

static int LoadFlagData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomFlag *dflag;

  if (!adata->flags_initialized) {
    HashInit(&adata->flags);
    adata->flags_initialized = 1;
  }

  int flag_id = 0;
  char hash_key[16];
  char cfg_key[255];

  LinkedList *keys;
  Link *link;
  char *key;

  for (int y = 0; y < 1023; ++y) {
    for (int x = 0; x < 1023; ++x) {
      if (mapdata->GetTile(arena, x, y) == TILE_TURF_FLAG) {
        sprintf(hash_key, "flag-%d", flag_id);

        dflag = HashGetOne(&adata->flags, hash_key);
        if (!dflag) {
          if (!(dflag = AllocFlag(arena))) {
            return 0;
          }

          dflag->flag_id = flag_id++;
          strcpy(dflag->key, hash_key);
        }
        else {
          ++flag_id;
          HashDeinit(&dflag->regions);
          HashInit(&dflag->regions);
        }

        dflag->x = x;
        dflag->y = y;

        /* cfghelp: Domination:Flag1-Influence, arena, int,
         * The amount of influence flag provides to its containing regions */
        sprintf(cfg_key, "Flag%d-%s", (dflag->flag_id + 1), "Influence");
        dflag->cfg_flag_influence = cfg->GetInt(arena->cfg, "Domination", cfg_key, 1);

        // Map flag to regions...
        if (adata->regions_initialized) {
          keys = HashGetKeys(&adata->regions);
          FOR_EACH(keys, key, link) {
            DomRegion *dregion = HashGetOne(&adata->regions, key);
            if (dregion && mapdata->Contains(dregion->region, dflag->x, dflag->y)) {
              HashReplace(&dflag->regions, dregion->key, dregion);
              HashReplace(&dregion->flags, dflag->key, dflag);
            }
          }
        }

        HashReplace(&adata->flags, dflag->key, dflag);
      }
    }
  }

  // Prune extra flags (this probably won't ever happen)...
  if (adata->flags.ents > flag_id) {
    for (int i = flag_id; adata->flags.ents > flag_id; ++i) {
      sprintf(hash_key, "flag-%d", i);

      dflag = (DomFlag*) HashRemoveAny(&adata->flags, hash_key);
      if (dflag) {
        FreeFlag(dflag);
      }
    }
  }

  if (!adata->flags.ents) {
    SetErrorState(arena, "ERROR: No map flags found were within configured regions.");
    return 0;
  }

  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "Loaded data for %d flags", adata->flags.ents);
  return 1;
}

static void FreeFlag(DomFlag *dflag) {
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  // Remove flag from any regions that map to it...
  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion) {
        HashRemoveAny(&dregion->flags, dflag->key);
      }
    }
  }

  HashDeinit(&dflag->regions);

  free(dflag);
}

static void FreeFlagData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  if (adata->flags_initialized) {
    keys = HashGetKeys(&adata->flags);
    FOR_EACH(keys, key, link) {
      DomFlag *dflag = HashGetOne(&adata->flags, key);
      if (dflag) {
        FreeFlag(dflag);
      }
    }

    HashDeinit(&adata->flags);
    adata->flags_initialized = 0;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static DomTeam* AllocTeam(Arena *arena) {
  DomTeam *dteam = malloc(sizeof(DomTeam));

  if (dteam) {
    dteam->arena = arena;

    dteam->cfg_team_freq= DOM_NEUTRAL_FREQ;

    HashInit(&dteam->players);
  } else {
    SetErrorState(arena, "ERROR: Unable to allocate memory for a new DomTeam instance");
  }

  return dteam;
}

static int LoadTeamData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomTeam *dteam, *cteam;

  int team_id = 0;
  char hash_key[16];
  char cfg_key[255];

  if (!adata->teams_initialized) {
    HashInit(&adata->teams);
    adata->teams_initialized = 1;
  }

  for (team_id = 0; team_id < adata->cfg_team_count; ++team_id) {
    sprintf(hash_key, "team-%d", team_id);

    dteam = HashGetOne(&adata->teams, hash_key);
    if (!dteam) {
      if (!(dteam = AllocTeam(arena))) {
        return 0;
      }

      dteam->team_id = team_id;
      strcpy(dteam->key, hash_key);
    }
    else {
      HashDeinit(&dteam->players);
      HashInit(&dteam->players);
    }

    /* cfghelp: Domination:Team1-Freq, arena, int, def: 0
     * The frequency players on this team must be on to capture flags and regions. */
    sprintf(cfg_key, "Team%d-%s", (team_id + 1), "Freq");
    dteam->cfg_team_freq = cfg->GetInt(arena->cfg, "Domination", cfg_key, 0);

    // TODO:
    // Maybe allow this to be configured in the future. Probably not, but maybe...
    sprintf(dteam->team_name, "Team-%d", (team_id + 1));

    // Check previous teams to ensure we don't have multiple teams configured to use the same freq
    for (int i = 0; i < team_id; ++i) {
      sprintf(hash_key, "team-%d", i);

      cteam = (DomTeam*) HashGetOne(&adata->teams, hash_key);
      if (cteam && cteam->cfg_team_freq == dteam->cfg_team_freq) {
        SetErrorState(arena, "ERROR: Multiple teams configured to use the same freq");
        return 0;
      }
    }

    HashReplace(&adata->teams, dteam->key, dteam);
  }

  // Prune excess teams...
  if (adata->teams.ents > team_id) {
    for (int i = team_id; adata->teams.ents > team_id; ++i) {
      sprintf(hash_key, "team-%d", i);

      dteam = (DomTeam*) HashRemoveAny(&adata->teams, hash_key);
      if (dteam) {
        FreeTeam(dteam);
      }
    }
  }

  if (!adata->teams.ents) {
    SetErrorState(arena, "ERROR: No teams were configured in this arena.");
    return 0;
  }

  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "Loaded data for %d teams", adata->teams.ents);
  return 1;
}

static void FreeTeam(DomTeam *dteam) {
  // Clear player data

  HashDeinit(&dteam->players);

  free(dteam);
}

static void FreeTeamData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  if (adata->teams_initialized) {
    keys = HashGetKeys(&adata->teams);
    FOR_EACH(keys, key, link) {
      DomTeam *dteam = HashGetOne(&adata->teams, key);
      if (dteam) {
        FreeTeam(dteam);
      }
    }

    HashDeinit(&adata->teams);
    adata->teams_initialized = 0;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static int ReadArenaConfig(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  /* cfghelp: Domination:TeamCount, arena, int, def: 3
   * The number of teams allowed to participate in the domination game. If this value is less
   * than two, two teams will be used. */
  adata->cfg_team_count = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "TeamCount", 3), 2);

  /* cfghelp: Domination:RegionCount, arena, int, range:1-255, def: 1
   * The number of regions to contested as part of the game. Each region will need to be configured
   * with the Domination.Region#-* settings. */
  adata->cfg_region_count = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "RegionCount", 1), 1, 255);

  /* cfghelp: Domination:DominationCountdown, arena, int, def: 600
   * The amount of time in centiseconds a team must control all regions to complete the domination
   * win condition. If set to zero or a negative value, the game will end as soon as a team controls
   * the final region. */
  adata->cfg_domination_countdown = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "DominationCountdown", 600), 0);

  /* cfghelp: Domination:MinimumPlayers, arena, int, def: 3
   * The minimum number of active players in the arena required to start a new game. If set to zero,
   * no global minimum will be enforced. */
  adata->cfg_min_players = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "MinimumPlayers", 3), 0);

  /* cfghelp: Domination:MinPlayersPerTeam, arena, int, def: 1
   * The minimum number of active players that must be on each team to start a new game. If set to
   * zero, no per-team minimum will be enforced. */
  adata->cfg_min_players_per_team = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "MinPlayersPerTeam", 1), 0);

  /* cfghelp: Domination:GameDuration, arena, int, def: 60
   * The length of a domination game in minutes. If this value is less than one, the game will
   * not have a time limit, ending only upon domination. */
  adata->cfg_game_duration = cfg->GetInt(arena->cfg, "Domination", "GameDuration", 60);
  /* cfghelp: Domination:GameCooldown, arena, int, def: 15
   * The amount of time between games, in minutes. If set to zero, a new game will start
   * immediately upon the conclusion of the previous. */
  adata->cfg_game_cooldown = cfg->GetInt(arena->cfg, "Domination", "GameCooldown", 15);
  /* cfghelp: Domination:GameCooldownRandom, arena, int, def: 5
   * The amount of time randomly added to the game cooldown. Ignored if GameCooldown is set to
   * zero. */
  adata->cfg_game_cooldown_random = cfg->GetInt(arena->cfg, "Domination", "GameCooldownRandom", 5);

  /* cfghelp: Domination:FlagCaptureTime, arena, int, def: 9000
   * The amount of time needed to capture a flag, in centiseconds. A flag which is controlled by
   * another team requires twice this time. */
  adata->cfg_flag_capture_time = cfg->GetInt(arena->cfg, "Domination", "FlagCaptureTime", 9000);
  /* cfghelp: Domination:FlagContestTime, arena, int, def: 1000
   * The amount of time in centiseconds after a flag is touched to transition it to the contested or
   * controlled state. */
  adata->cfg_flag_contest_time = cfg->GetInt(arena->cfg, "Domination", "FlagContestTime", 1000);
  /* cfghelp: Domination:DefenseEventFrequency, arena, int, def: 9000
   * The frequency, in centiseconds, at which to fire defense events for players for defending
   * controlled regions. If set to zero, defense events will be disabled. */
  adata->cfg_defense_reward_frequency = cfg->GetInt(arena->cfg, "Domination", "DefenseEventFrequency", 9000);
  /* cfghelp: Domination:DefenseEventRadius, arena, int, def: 500
   * The maximum distance, in pixels, the center of a player's ship can be from the center of a
   * region to fire a defense event. */
  adata->cfg_defense_reward_radius = cfg->GetInt(arena->cfg, "Domination", "DefenseEventRadius", 500);

  LoadTeamData(arena);
  LoadRegionData(arena);
  LoadFlagData(arena);

  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "Arena configuration loaded");
  return 1;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static DomFlag* GetDomFlag(Arena *arena, int flag_id) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: GetDomFlag called with a null arena parameter", DOM_MODULE_NAME);
    return NULL;
  }

  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomFlag *dflag = NULL;
  char hash_key[16];

  if (adata->flags_initialized && flag_id >= 0 && flag_id <= 10000) {
    sprintf(hash_key, "flag-%d", flag_id);
    dflag = HashGetOne(&adata->flags, hash_key);
  }

  return dflag;
}

static DomTeam* GetDomTeam(Arena *arena, int freq) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: GetDomTeam called with a null arena parameter", DOM_MODULE_NAME);
    return NULL;
  }

  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomTeam *dteam = NULL;

  LinkedList *keys;
  Link *link;
  char *key;

  if (adata->teams_initialized) {
    keys = HashGetKeys(&adata->teams);
    FOR_EACH(keys, key, link) {
      DomTeam *cteam = HashGetOne(&adata->teams, key);
      if (cteam && cteam->cfg_team_freq == freq) {
        dteam = cteam;
        break;
      }
    }
  }

  return dteam;
}

static DomRegion* GetDomRegion(Arena *arena, const char *region_name) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: GetDomRegion called with a null arena parameter", DOM_MODULE_NAME);
    return NULL;
  }

  if (!region_name) {
    lm->Log(L_ERROR, "<%s> ERROR: GetDomRegion called with a null region_name parameter", DOM_MODULE_NAME);
    return NULL;
  }

  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomRegion *dregion = NULL;

  if (adata->regions_initialized) {
    dregion = HashGetOne(&adata->regions, region_name);
  }

  return dregion;
}

static int GetFlagProvidedInfluence(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagProvidedInfluence called with a null dflag parameter", DOM_MODULE_NAME);
    return -1;
  }

  return dflag->cfg_flag_influence;
}

static int GetFlagContestTime(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagContestTime called with a null dflag parameter", DOM_MODULE_NAME);
    return -1;
  }

  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);
  return adata->cfg_flag_contest_time;
}

static int GetFlagCaptureTime(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagCaptureTime called with a null dflag parameter", DOM_MODULE_NAME);
    return -1;
  }

  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);
  return adata->cfg_flag_capture_time;
}

static int GetFlagAcquiredInfluence(DomFlag *dflag, DomTeam *dteam) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagAcquiredInfluence called with a null dflag parameter", DOM_MODULE_NAME);
    return -1;
  }

  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagAcquiredInfluence called with a null dteam parameter", DOM_MODULE_NAME);
    return -1;
  }

  UpdateFlagAcquiredInfluence(dflag);

  return dflag->controlling_team == dteam ? dflag->acquired_influence : 0;
}

static DomTeam* GetFlagControllingTeam(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagControllingTeam called with a null dflag parameter", DOM_MODULE_NAME);
    return NULL;
  }

  return dflag->controlling_team;
}

static DomTeam* GetFlagEntityControllingTeam(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagEntityControllingTeam called with a null dflag parameter", DOM_MODULE_NAME);
    return NULL;
  }

  return dflag->flag_team;
}

static DomFlagState GetFlagState(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a null dflag parameter", DOM_MODULE_NAME);
    return DOM_FLAG_STATE_UNKNOWN;
  }

  return dflag->state;
}

/**
 * Sets the state of the specified flag
 *
 * @param *dflag
 *  The flag to update
 *
 * @param state
 *  The base state to assign to the flag
 *
 * @param *controlling_team
 *  The DomTeam that controls the logical flag
 *  Note: This is not the controller of the actual in-game turf flag entity
 *
 * @param acquired_influence
 *  The amount of influence the controlling team has over the flag
 *
 * @param *flag_entity_team
 *  The DomTeam that controls the physical turf flag entity
 *  Note: The flag entity controller is not the actual logical domination controller
 */
static void SetFlagState(DomFlag *dflag, DomFlagState state, DomTeam *controlling_team, int acquired_influence, DomTeam *flag_entity_team) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a null dflag parameter", DOM_MODULE_NAME);
    return;
  }

  // Some sanity checks...
  int required_influence = GetFlagCaptureTime(dflag);

  switch (state) {
    case DOM_FLAG_STATE_NEUTRAL:
      controlling_team = NULL;
      flag_entity_team = NULL;
      acquired_influence = 0;
      break;

    case DOM_FLAG_STATE_CONTESTED:
      if (!flag_entity_team) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CONTESTED state and no flag entity controller", DOM_MODULE_NAME);
        return;
      }
      break;

    case DOM_FLAG_STATE_CAPTURING:
      if (!controlling_team) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CAPTURING state and no controlling team", DOM_MODULE_NAME);
        return;
      }

      if (!flag_entity_team) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CAPTURING state and no flag entity controller", DOM_MODULE_NAME);
        return;
      }
      break;

    case DOM_FLAG_STATE_CONTROLLED:
      if (!controlling_team) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CONTROLLED state and no controlling team", DOM_MODULE_NAME);
        return;
      }

      flag_entity_team = controlling_team;
      acquired_influence = required_influence;
      break;

    default:
      lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with an invalid flag state: %d", DOM_MODULE_NAME, state);
      return;
  }

  DomFlagState pstate = dflag->state;
  acquired_influence = DOM_CLAMP(acquired_influence, 0, required_influence);

  int change = (dflag->state != state) ||
    (dflag->controlling_team != controlling_team) ||
    (dflag->acquired_influence != acquired_influence) ||
    (dflag->flag_team != flag_entity_team);

  dflag->state = state;
  dflag->controlling_team = controlling_team;
  dflag->acquired_influence = acquired_influence;
  dflag->flag_team = flag_entity_team;

  FlagInfo info = { FI_ONMAP, NULL, -1, -1, (dflag->flag_team ? dflag->flag_team->cfg_team_freq : DOM_NEUTRAL_FREQ) };
  flagcore->SetFlags(dflag->arena, dflag->flag_id, &info, 1);
  UpdateFlagState(dflag->arena);


  chat->SendArenaMessage(dflag->arena, "%d: Flag state update! Flag: %d, State: %d, team: %d, influence: %d, flag owner: %d",
    current_ticks() / 100, dflag->flag_id, dflag->state, (dflag->controlling_team ? dflag->controlling_team->cfg_team_freq : -1), dflag->acquired_influence, (dflag->flag_team ? dflag->flag_team->cfg_team_freq : -1)
  );


  if (change) {
    // Cancel existing flag timers for the given flag
    ClearFlagTimers(dflag);

    // If the flag is now in a transitional state (contested/capturing), set a new timer to trigger
    // the next state change
    switch (dflag->state) {
      case DOM_FLAG_STATE_CONTESTED:
        mainloop->SetTimer(OnFlagContestTimer, GetFlagContestTime(dflag), 0, dflag, dflag);

      default:
        dflag->last_influence_update = 0;
        break;

      case DOM_FLAG_STATE_CAPTURING:
        dflag->last_influence_update = current_ticks();
        if (dflag->controlling_team == dflag->flag_team) {
          // Begin capture for flag team; end in CONTROLLED state with max influence
          mainloop->SetTimer(OnFlagFullCaptureTimer, (required_influence - dflag->acquired_influence), 0, dflag, dflag);
        }
        else {
          // Begin capture for flag team; end in CAPTURING state with 0 influence
          mainloop->SetTimer(OnFlagHalfCaptureTimer, dflag->acquired_influence, 0, dflag, dflag);
        }
        break;
    }

    // Do callbacks. Note that we always call ours last to give plugins a chance to process the
    // event before we go on changing other stuff (regions)
    DO_CBS(CB_DOM_FLAG_STATE_CHANGED, dflag->arena, DomFlagStateChangedFunc, (dflag->arena, dflag, pstate, dflag->state));
    OnFlagStateChange(dflag->arena, dflag, pstate, dflag->state);
  }
}

static char* GetTeamName(DomTeam *dteam) {
  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetTeamName called with a null dteam parameter", DOM_MODULE_NAME);
    return NULL;
  }

  return dteam->team_name;
}

static int GetTeamRegionInfluence(DomTeam *dteam, DomRegion *dregion) {
  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetTeamRegionInfluence called with a null dteam parameter", DOM_MODULE_NAME);
    return -1;
  }

  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetTeamRegionInfluence called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  int influence = 0;

  LinkedList *keys;
  Link *link;
  char *key;

  keys = HashGetKeys(&dregion->flags);
  FOR_EACH(keys, key, link) {
    DomFlag *dflag = HashGetOne(&dregion->flags, key);

    if (dflag && GetFlagState(dflag) == DOM_FLAG_STATE_CONTROLLED) {

      DomTeam *cteam = GetFlagControllingTeam(dflag);
      if (dteam == cteam) {
        influence += GetFlagProvidedInfluence(dflag);
      }
      else if (cteam) {
        influence -= GetFlagProvidedInfluence(dflag);
      }
    }
  }

  return influence;
}

static int GetTeamAcquiredRegionInfluence(DomTeam *dteam, DomRegion *dregion) {
  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetTeamAcquiredRegionInfluence called with a null dteam parameter", DOM_MODULE_NAME);
    return -1;
  }

  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetTeamAcquiredRegionInfluence called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  UpdateRegionAcquiredInfluence(dregion);

  return dregion->controlling_team == dteam ? dregion->acquired_influence : 0;
}

static int GetTeamAcquiredControlPoints(DomTeam *dteam) {
  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetTeamAcquiredRegionInfluence called with a null dteam parameter", DOM_MODULE_NAME);
    return -1;
  }

  DomArena *adata = P_ARENA_DATA(dteam->arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  int control_points = 0;

  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion && GetRegionState(dregion) == DOM_REGION_STATE_CONTROLLED && GetRegionControllingTeam(dregion) == dteam) {
        control_points = GetRegionProvidedControlPoints(dregion);
      }
    }
  }

  return control_points;
}

static int GetRegionProvidedControlPoints(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionProvidedControlPoints called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  return dregion->cfg_region_value;
}

static int GetRegionRequiredInfluence(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionRequiredInfluence called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  return dregion->cfg_required_influence;
}

static int GetRegionPotentialInfluence(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionRequiredInfluence called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  LinkedList *keys;
  Link *link;
  char *key;

  int potential_influence = 0;

  keys = HashGetKeys(&dregion->flags);
  FOR_EACH(keys, key, link) {
    DomFlag *dflag = HashGetOne(&dregion->flags, key);
    if (dflag) {
      potential_influence += GetFlagProvidedInfluence(dflag);
    }
  }

  return potential_influence;
}

static DomTeam* GetRegionControllingTeam(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionControllingTeam called with a null dregion parameter", DOM_MODULE_NAME);
    return NULL;
  }

  return dregion->controlling_team;
}

static DomTeam* GetRegionInfluentialTeam(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionInfluentialTeam called with a null dregion parameter", DOM_MODULE_NAME);
    return NULL;
  }

  DomArena *adata = P_ARENA_DATA(dregion->arena, adkey);

  HashTable teams;
  LinkedList *keys;
  Link *link;
  char *key;

  DomTeam *iteam = NULL;
  int iteam_influence = 0;

  HashInit(&teams);

  if (adata->teams_initialized) {
    keys = HashGetKeys(&adata->teams);
    FOR_EACH(keys, key, link) {
      DomTeam *dteam = HashGetOne(&adata->teams, key);
      if (dteam) {
        int influence = GetTeamRegionInfluence(dteam, dregion);
        if (influence > iteam_influence) {
          iteam = dteam;
          iteam_influence = influence;
        }
      }
    }
  }

  HashDeinit(&teams);

  return iteam;
}

static DomRegionState GetRegionState(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: SetRegionState called with a null dregion parameter", DOM_MODULE_NAME);
    return DOM_REGION_STATE_UNKNOWN;
  }

  return dregion->state;
}

static void SetRegionState(DomRegion *dregion, DomRegionState state, DomTeam *controlling_team, int acquired_influence) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: SetRegionState called with a null dregion parameter", DOM_MODULE_NAME);
    return;
  }

  // Some sanity checks...
  // DomArena *adata = P_ARENA_DATA(dregion->arena, adkey);
  int required_influence = GetRegionRequiredInfluence(dregion);

  switch (state) {
    case DOM_REGION_STATE_NEUTRAL:
      controlling_team = NULL;
      acquired_influence = 0;
      break;

    case DOM_REGION_STATE_CAPTURING:
      if (!controlling_team) {
        lm->Log(L_ERROR, "<%s> ERROR: SetRegionState called with a CAPTURING state and no controlling team", DOM_MODULE_NAME);
        return;
      }
      break;

    case DOM_REGION_STATE_CONTROLLED:
      if (!controlling_team) {
        lm->Log(L_ERROR, "<%s> ERROR: SetRegionState called with a CONTROLLED state and no controlling team", DOM_MODULE_NAME);
        return;
      }

      acquired_influence = required_influence;
      break;

    default:
      lm->Log(L_ERROR, "<%s> ERROR: SetRegionState called with an invalid region state: %d", DOM_MODULE_NAME, state);
      return;
  }

  DomRegionState pstate = dregion->state;
  acquired_influence = DOM_CLAMP(acquired_influence, 0, required_influence);

  dregion->state = state;
  dregion->controlling_team = controlling_team;
  dregion->acquired_influence = acquired_influence;


  DomTeam *iteam = GetRegionInfluentialTeam(dregion);
  chat->SendArenaMessage(dregion->arena, "=== %d: Region state update! Region: %d, State: %d, team: %d, influence: %d, iteam: %d",
    current_ticks() / 100, dregion->region_id, dregion->state, (dregion->controlling_team ? dregion->controlling_team->cfg_team_freq : -1), dregion->acquired_influence, (iteam ? iteam->cfg_team_freq : -1)
  );

  // Cancel existing flag timers for the given flag
  ClearRegionTimers(dregion);
  DomTeam *influential_team = GetRegionInfluentialTeam(dregion);

  // If the flag is now in a transitional state (contested/capturing), set a new timer to trigger
  // the next state change
  switch (dregion->state) {
    case DOM_REGION_STATE_CAPTURING:
      dregion->last_influence_update = current_ticks();
      if (dregion->controlling_team && dregion->controlling_team == influential_team) {
        // Begin capture for controlling team; end in CONTROLLED state with max influence
        int remaining_influence = (required_influence - dregion->acquired_influence);
        int region_influence = GetTeamRegionInfluence(dregion->controlling_team, dregion);

        int ticks = remaining_influence / region_influence;
        if (remaining_influence % region_influence) {
          ++ticks;
        }

        mainloop->SetTimer(OnRegionFullCaptureTimer, ticks * DOM_TICK_DURATION, 0, dregion, dregion);
      }
      else if (influential_team) {
        int region_influence = GetTeamRegionInfluence(influential_team, dregion);

        int ticks = dregion->acquired_influence / region_influence;
        if (dregion->acquired_influence % region_influence) {
          ++ticks;
        }

        // Begin capture for influential team; end in CAPTURING state with 0 influence
        mainloop->SetTimer(OnRegionHalfCaptureTimer, ticks * DOM_TICK_DURATION, 0, dregion, dregion);
      }
      else {
        int region_influence = GetRegionPotentialInfluence(dregion);

        int ticks = dregion->acquired_influence / region_influence;
        if (dregion->acquired_influence % region_influence) {
          ++ticks;
        }

        // Begin reverting region to neutral; end in NEUTRAL state with 0 influence
        mainloop->SetTimer(OnRegionNeutralizeTimer, ticks * DOM_TICK_DURATION, 0, dregion, dregion);
      }

      break;

    default:
      dregion->last_influence_update = 0;
      break;
  }

  // Do callbacks. Note that we always call ours last to give plugins a chance to process the
  // event before we go on changing other stuff (regions)
  DO_CBS(CB_DOM_REGION_STATE_CHANGED, dregion->arena, DomRegionStateChangedFunc, (dregion->arena, dregion, pstate, dregion->state));
  OnRegionStateChange(dregion->arena, dregion, pstate, dregion->state);
}

static DomTeam* GetDominatingTeam(Arena *arena) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: GetDominatingTeam called with a null arena parameter", DOM_MODULE_NAME);
    return NULL;
  }

  DomArena *adata = P_ARENA_DATA(dregion->arena, adkey);

  DomTeam *dteam = NULL;
  LinkedList *keys;
  Link *link;
  char *key;

  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion) {
        if (GetRegionState(dregion) != DOM_REGION_STATE_CONTROLLED) {
          return NULL;
        }

        DomTeam *cteam = GetRegionControllingTeam(dregion);
        if (!cteam || (dteam != null && dteam != cteam)) {
          return NULL;
        }

        dteam = cteam;
      }
    }
  }

  return dteam;
}

static DomGameState GetGameState(Arena *arena) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: GetGameState called with a null arena parameter", DOM_MODULE_NAME);
    return DOM_GAME_STATE_UNKNOWN;
  }

  DomArena *adata = P_ARENA_DATA(arena, adkey);
  return adata->game_state;
}

static void SetGameState(Arena *arena, DomGameState state) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: SetGameState called with a null arena parameter", DOM_MODULE_NAME);
    return;
  }

  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomGameState prev_state = adata->game_state;

  LinkedList *keys;
  Link *link;
  char *key;


  switch (state) {
    case DOM_GAME_STATE_INACTIVE:
      // Stop all game timers
      ClearGameTimers(arena);

      // re-read arena settings
      ReadArenaConfig(arena);

      // Reset all flag and region states to neutral
      if (adata->regions_initialized) {
        keys = HashGetKeys(&adata->regions);
        FOR_EACH(keys, key, link) {
          DomRegion *dregion = HashGetOne(&adata->regions, key);
          if (dregion) {
            SetRegionState(dregion, DOM_REGION_STATE_NEUTRAL, NULL, 0);
          }
        }
      }

      if (adata->flags_initialized) {
        keys = HashGetKeys(&adata->flags);
        FOR_EACH(keys, key, link) {
          DomFlag *dflag = HashGetOne(&adata->flags, key);
          if (dflag) {
            SetFlagState(dflag, DOM_FLAG_STATE_NEUTRAL, NULL, 0, NULL);
          }
        }
      }

      break;

    case DOM_GAME_STATE_ACTIVE:
      if (prev_state != DOM_GAME_STATE_INACTIVE && prev_state != DOM_GAME_STATE_PAUSED) {
        lm->LogA(L_DRIVEL, DOM_MODULE_NAME, arena, "ERROR: Invalid game state transition: %d => %d", prev_state, state);
        return;
      }

      // Clear game-start timer
      // TODO: Warp/reship players?
      break;

    case DOM_GAME_STATE_PAUSED:
      if (prev_state != DOM_GAME_STATE_ACTIVE) {
        lm->LogA(L_DRIVEL, DOM_MODULE_NAME, arena, "ERROR: Invalid game state transition: %d => %d", prev_state, state);
        return;
      }

      lm->Log(L_ERROR, "<%s> ERROR: Pausing is currently unsupported", DOM_MODULE_NAME);
      return; // break;

    case DOM_GAME_STATE_FINISHED:
      if (prev_state != DOM_GAME_STATE_ACTIVE) {
        lm->LogA(L_DRIVEL, DOM_MODULE_NAME, arena, "ERROR: Invalid game state transition: %d => %d", prev_state, state);
        return;
      }

      // Clear game timers
      ClearGameTimers(arena);

      // We need to leave the rest of the game state as-is so handlers can process the final game
      // state and do stuff as necessary. Our handler will transition us back to the inactive state.
      break;

    case DOM_GAME_STATE_ERROR:
      // Clear our game timers...
      ClearGameTimers(arena);

      // Free data, since it's probably corrupt anyway
      FreeRegionData(arena);
      FreeFlagData(arena);
      FreeTeamData(arena);
      break;

    default:
      lm->LogA(L_DRIVEL, DOM_MODULE_NAME, arena, "ERROR: Invalid game state: %d", state);
      return;
  }

  adata->game_state = state;

  // Do callbacks. Note that we always call ours last to give plugins a chance to process the
  // event before we go on changing other stuff
  DO_CBS(CB_DOM_GAME_STATE_CHANGED, arena, DomGameStateChangedFunc, (arena, prev_state, adata->game_state));
  OnGameStateChange(arena, prev_state, adata->game_state);
}

static void SetAlertState(Arena *arena, DomAlertType type, DomAlertState state, ticks_t duration) {
  if (!arena) {
    lm->Log(L_ERROR, "<%s> ERROR: SetAlertState called with a null arena parameter", DOM_MODULE_NAME);
    return NULL;
  }

  // Do we need to do anything fancy here...?
  DO_CBS(CB_DOM_ALERT, arena, DomAlertFunc, (arena, type, enabled, duration));
  OnGameAlert(arena, type, state, duration);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void UpdateFlagAcquiredInfluence(DomFlag *dflag) {
  if (dflag->last_influence_update) {
    ticks_t ctime = current_ticks();
    ticks_t elapsed = TICK_DIFF(ctime, dflag->last_influence_update);
    int required_influence = GetFlagCaptureTime(dflag);

    switch (dflag->state) {
      case DOM_FLAG_STATE_CONTROLLED:
        dflag->acquired_influence = required_influence;

      default:
        dflag->last_influence_update = 0;
        break;

      case DOM_FLAG_STATE_CAPTURING:
        if (dflag->controlling_team == dflag->flag_team) {
          dflag->acquired_influence += elapsed;
        }
        else {
          dflag->acquired_influence -= elapsed;
        }

        // Clamp the value
        dflag->acquired_influence = DOM_CLAMP(dflag->acquired_influence, 0, required_influence);
        dflag->last_influence_update = ctime;
        break;
    }
  }
}

static void UpdateRegionAcquiredInfluence(DomRegion *dregion) {
  DomArena *adata = P_ARENA_DATA(dregion->arena, adkey);

  if (dregion->last_influence_update) {
    ticks_t ctime = current_ticks();
    ticks_t elapsed = TICK_DIFF(ctime, dregion->last_influence_update);
    u_int32_t ticks = elapsed / DOM_TICK_DURATION;
    ctime -= elapsed % DOM_TICK_DURATION;

    int required_influence = GetRegionRequiredInfluence(dregion);

    if (adata->teams_initialized) {
      DomTeam *cteam = GetRegionControllingTeam(dregion);
      int influence = cteam ? GetTeamRegionInfluence(cteam, dregion) : -1 * GetRegionPotentialInfluence(dregion);

      switch (dregion->state) {
        case DOM_REGION_STATE_CAPTURING:
          dregion->acquired_influence += (influence * ticks);

          // Clamp the value
          dregion->acquired_influence = DOM_CLAMP(dregion->acquired_influence, 0, required_influence);
          dregion->last_influence_update = ctime;
          break;

        case DOM_REGION_STATE_CONTROLLED:
          dregion->acquired_influence = required_influence;

        default:
          dregion->last_influence_update = 0;
          break;
      }
    }
  }
}

static void ClearFlagTimers(DomFlag *dflag) {
  mainloop->ClearTimer(OnFlagContestTimer, dflag);
  mainloop->ClearTimer(OnFlagHalfCaptureTimer, dflag);
  mainloop->ClearTimer(OnFlagFullCaptureTimer, dflag);
}

static void ClearRegionTimers(DomRegion *dregion) {
  mainloop->ClearTimer(OnRegionHalfCaptureTimer, dregion);
  mainloop->ClearTimer(OnRegionFullCaptureTimer, dregion);
  mainloop->ClearTimer(OnRegionNeutralizeTimer, dregion);
}

static void ClearGameTimers(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  LinkedList *keys;
  Link *link;
  char *key;

  if (adata->flags_initialized) {
    keys = HashGetKeys(&adata->flags);
    FOR_EACH(keys, key, link) {
      DomFlag *dflag = HashGetOne(&adata->flags, key);
      if (dflag) {
        ClearFlagTimers(dflag);
      }
    }
  }

  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion) {
        ClearRegionTimers(dregion);
      }
    }
  }

  // Clear any global game timers (domination timer...?)
  mainloop->ClearTimer(OnDominationTimer, adata);
}

static int UpdateFlagStateTimer(void *param) {
  DomArena *adata = P_ARENA_DATA((Arena*) param, adkey);

  flagcore->SendTurfStatus((Arena*) param);
  mainloop->ClearTimer(UpdateFlagStateTimer, param);
  adata->updating_flag_status = 0;

  return 0;
}

static void UpdateFlagState(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  if (!adata->updating_flag_status) {
    adata->updating_flag_status = 1;
    mainloop->SetTimer(UpdateFlagStateTimer, 100, 0, arena, arena);
  }
}

static void UpdateDominationState(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  if (GetDominatingTeam(arena)) {
    SetAlertState(arena, DOM_ALERT_DOMINATING, 1, adata->cfg_domination_countdown);
    mainloop->SetTimer(OnDominationTimer, adata->cfg_domination_countdown, 0, adata, adata);
  }
  else {
    SetAlertState(arena, DOM_ALERT_DOMINATING, 0, 0);
    mainloop->ClearTimer(OnDominationTimer, adata);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void OnArenaAttach(Arena *arena) {
  // Initialize arena data
  InitArenaData(arena);
}

static void OnArenaDetach(Arena *arena) {
  // Free arena data

}

static void OnArenaAction(Arena *arena, int action) {
  // We don't do this during games anymore.

  // switch (action) {
  //   case AA_CONFCHANGED:
  //     ReadArenaConfig(arena);
  //     break;
  // }
}

static void OnFlagGameInit(Arena *arena) {
  flagcore->SetCarryMode(arena, CARRY_NONE);
  lm->LogA(L_DRIVEL, DOM_MODULE_NAME, arena, "Flag carry mode set to CARRY_NONE");

  int flag_count = mapdata->GetFlagCount(arena);
  flagcore->ReserveFlags(arena, flag_count);
  lm->LogA(L_DRIVEL, DOM_MODULE_NAME, arena, "Reserved space for %d flags", flag_count);

  // Set dummy flag states so we don't trigger assertions straight away...
  FlagInfo info = (FlagInfo) { FI_ONMAP, NULL, -1, -1, DOM_NEUTRAL_FREQ };

  for (int i = 0; i < flag_count; ++i) {
    flagcore->SetFlags(arena, i, &info, 1);
  }

  ReadArenaConfig(arena);
}



static void OnFlagTouch(Arena *arena, Player *player, int flag_id) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomFlag *dflag = GetDomFlag(arena, flag_id);
  DomTeam *dteam = GetDomTeam(arena, player->p_freq);

  if (dflag && dflag->regions.ents) {
    if (dteam && adata->game_state == DOM_GAME_STATE_ACTIVE) {
      // A player is contesting a flag
      DomTeam *cteam = GetFlagControllingTeam(dflag);
      int acquired_influence = cteam ? GetFlagAcquiredInfluence(dflag, cteam) : 0;

      SetFlagState(dflag, DOM_FLAG_STATE_CONTESTED, cteam, acquired_influence, dteam);
    } else {
      // Touched by someone outside of the domination game or the game is not active. Restore the
      // flag state.
      UpdateFlagState(arena);

      if (dteam) {
        lm->LogP(L_DRIVEL, DOM_MODULE_NAME, player, "Managed turf flag touched before game is active. Reverting action.");
      } else {
        lm->LogP(L_DRIVEL, DOM_MODULE_NAME, player, "Managed turf flag touched by a player not participating in the game. Reverting action.");
      }
    }
  } else {
    // We don't care about this flag
    lm->LogP(L_DRIVEL, DOM_MODULE_NAME, player, "Unmanaged turf flag touched");
  }
}

static int OnFlagContestTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomTeam *cteam = GetFlagControllingTeam(dflag);
  DomTeam *dteam = GetFlagEntityControllingTeam(dflag);

  if (!cteam) {
    SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, dteam, 0, dteam);
  }
  else {
    SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, cteam, GetFlagAcquiredInfluence(dflag, cteam), dteam);
  }

  return 0;
}

static int OnFlagHalfCaptureTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomTeam *dteam = GetFlagEntityControllingTeam(dflag);

  if (dteam) {
    SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, dteam, 0, dteam);
  }
  else {
    SetFlagState(dflag, DOM_FLAG_STATE_NEUTRAL, NULL, 0, NULL);
  }

  return 0;
}

static int OnFlagFullCaptureTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomTeam *dteam = GetFlagEntityControllingTeam(dflag);
  int required_influence = GetFlagCaptureTime(dflag);

  SetFlagState(dflag, DOM_FLAG_STATE_CONTROLLED, dteam, required_influence, dteam);

  return 0;
}

static void OnFlagStateChange(Arena *arena, DomFlag *dflag, DomFlagState prev_state, DomFlagState new_state) {
  DomRegion *dregion;
  LinkedList *keys;
  Link *link;
  char *key;

  if (prev_state == DOM_FLAG_STATE_CONTROLLED || new_state == DOM_FLAG_STATE_CONTROLLED) {
    // In any case where a flag goes to or from controlled, we need to recalculate the team with
    // the most influence and start capturing the region
    keys = HashGetKeys(&dflag->regions);
    FOR_EACH(keys, key, link) {
      dregion = HashGetOne(&dflag->regions, key);
      if (dregion) {
        DomTeam *cteam = GetRegionControllingTeam(dregion);
        DomTeam *iteam = GetRegionInfluentialTeam(dregion);
        int acquired_influence = cteam ? GetTeamAcquiredRegionInfluence(cteam, dregion) : 0;
        int required_influence = GetRegionRequiredInfluence(dregion);

        if (cteam) {
          if (cteam == iteam) {
            if (acquired_influence >= required_influence) {
              // Region is fully controlled by the team -- set it to the CONTROLLED state and
              // correct the influence so it's not over max
              SetRegionState(dregion, DOM_REGION_STATE_CONTROLLED, cteam, required_influence);
            }
            else {
              // Region is being captured, but isn't there yet. Set it to the CAPTURING state
              SetRegionState(dregion, DOM_REGION_STATE_CAPTURING, cteam, acquired_influence);
            }
          }
          else {
            // Team is losing control of the region
            if (acquired_influence <= 0) {
              if (iteam) {
                // Region switches to iteam in the capturing state
                SetRegionState(dregion, DOM_REGION_STATE_CAPTURING, iteam, 0);
              }
              else {
                // Region becomes neutral -- all teams are contesting the crap out of it
                SetRegionState(dregion, DOM_REGION_STATE_NEUTRAL, NULL, 0);
              }
            }
            else {
              // Region is still under cteam's control; but they're losing influence. If the region
              // is not in the CAPTURING state, put it there
              SetRegionState(dregion, DOM_REGION_STATE_CAPTURING, cteam, acquired_influence);
            }
          }
        }
        else {
          if (iteam) {
            // Neutral region without a controller -- give it to the new team in the CAPTURING state
            SetRegionState(dregion, DOM_REGION_STATE_CAPTURING, iteam, 0);
          }
          else {
            // Umm... what? No cteam, no item. Region enters the NEUTRAL state.
            SetRegionState(dregion, DOM_REGION_STATE_NEUTRAL, NULL, 0);
          }
        }
      }
    }
  }
}

static int OnRegionHalfCaptureTimer(void *param) {
  DomRegion *dregion = (DomRegion*) param;
  DomTeam *dteam = GetRegionInfluentialTeam(dregion);

  if (dteam) {
    SetRegionState(dregion, DOM_REGION_STATE_CAPTURING, dteam, 0);
  }
  else {
    SetRegionState(dregion, DOM_REGION_STATE_NEUTRAL, NULL, 0);
  }

  return 0;
}

static int OnRegionFullCaptureTimer(void *param) {
  DomRegion *dregion = (DomRegion*) param;
  DomTeam *dteam = GetRegionControllingTeam(dregion);
  int required_influence = GetRegionRequiredInfluence(dregion);

  SetRegionState(dregion, DOM_REGION_STATE_CONTROLLED, dteam, required_influence);

  return 0;
}

static int OnRegionNeutralizeTimer(void *param) {
  DomRegion *dregion = (DomRegion*) param;

  SetRegionState(dregion, DOM_REGION_STATE_NEUTRAL, NULL, 0);

  return 0;
}

static void OnRegionStateChange(Arena *arena, DomRegion *dregion, DomRegionState prev_state, DomRegionState new_state) {
  // We don't care if
  if (old_state != new_state) {
    // Check if we need to set the domination timer
    if (new_state == DOM_REGION_STATE_CONTROLLED) {
      // Set timer/alert
      UpdateDominationState(arena);
    }
    else if (old_state == DOM_REGION_STATE_CONTROLLED) {
      // Clear timer/alert
      UpdateDominationState(arena);
    }
  }
}

static void OnFlagCleanup(Arena *arena, int flag_id, int reason, Player *carrier, int freq) {
  chat->SendArenaMessage(arena, "Flag cleanup! Flag: %d, reason: %d, Freq: %d Player: %s", flag_id, reason, freq, carrier->name);
}

static void OnFlagReset(Arena *arena, int freq, int points) {
  // This should only happen on ?flagreset; in which case we should probably reset the game and
  // announce to the arena which jerk moderator reset the game.

  chat->SendArenaMessage(arena, "Flag game reset. We don't like this at all.");
}

static void OnGameStateChange(Arena *arena, DomGameState old_state, DomGameState new_state) {
  if (new_state == DOM_GAME_STATE_FINISHED) {
    // we've finished handling the end-of-game event; transition back to INACTIVE to prepare for
    // the next game
    SetGameState(arena, DOM_GAME_STATE_INACTIVE);
  }
}

static int OnDominationTimer(void *param) {
  DomArena *adata = (DomArena*) param;

  // Dominating team wins
  DomTeam *dteam = GetDominatingTeam(adata->arena);
  if (dteam) {
    SetGameState(arena, DOM_GAME_STATE_FINISHED);
  }
  else {
    lm->LogA(L_ERROR, DOM_MODULE_NAME, arena, "Domination event triggered, but no team appears to be dominating");
  }

  return 0;
}

static int OnGameStartTimer(void *param) {
  // Game started

  // If we're still inactive


  return 0;
}

static int OnGameEndTimer(void *param) {
  // Game ended via time; set game state to FINISHED
  SetGameState(arena, DOM_GAME_STATE_INACTIVE);
  return 0;
}







static void OnPlayerAction(Player *player, int action, Arena *arena) {
  switch (action) {
    case PA_ENTERARENA:
      break;

    case PA_LEAVEARENA:
      break;
  }
}

static void OnPlayerDeath(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green) {

}

static void OnPlayerSpawn(Player *player, int reason) {

}

static void OnPlayerFreqShipChange(Player *player, int newship, int oldship, int newfreq, int oldfreq) {

}

////////////////////////////////////////////////////////////////////////////////////////////////////

static Iflaggame flagcore_flaggame_interface = {
  INTERFACE_HEAD_INIT(I_FLAGGAME, DOM_MODULE_NAME "-flaggame")

  OnFlagGameInit,
  OnFlagTouch,
  OnFlagCleanup
};

static Idomination domination_interface = {
  INTERFACE_HEAD_INIT(I_DOMINATION, DOM_MODULE_NAME "-iface")

  GetDomFlag,
  GetDomTeam,
  GetDomRegion,
  GetFlagProvidedInfluence,
  GetFlagContestTime,
  GetFlagCaptureTime,
  GetFlagAcquiredInfluence,
  GetFlagControllingTeam,
  GetFlagEntityControllingTeam,
  GetFlagState,
  SetFlagState,
  GetTeamName,
  GetTeamRegionInfluence,
  GetTeamAcquiredRegionInfluence,
  GetTeamAcquiredControlPoints,
  GetRegionProvidedControlPoints,
  GetRegionRequiredInfluence,
  GetRegionPotentialInfluence,
  GetRegionMinimumInfluence,
  GetRegionControllingTeam,
  GetRegionInfluentialTeam,
  GetRegionState,
  SetRegionState,
  GetDominatingTeam,
  GetGameState,
  SetGameState,
  SetAlertState
};

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
    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    mainloop  = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    mapdata   = mm->GetInterface(I_MAPDATA, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

    return mm &&
      (arenaman && cfg && chat && flagcore && lm && mainloop && mapdata && pd);
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
    mm->ReleaseInterface(mainloop);
    mm->ReleaseInterface(mapdata);
    mm->ReleaseInterface(pd);

    mm = NULL;
  }
}

EXPORT const char info_domination[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_domination(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", DOM_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate object data
      adkey = arenaman->AllocateArenaData(sizeof(DomArena));
      if (adkey == -1) {
        printf("<%s> Unable to allocate per-arena data.\n", DOM_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-arena data.", DOM_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // pdkey = pd->AllocatePlayerData(sizeof(PlayerStreakData));
      // if (pdkey == -1) {
      //   printf("<%s> Unable to allocate per-player data.\n", DOM_MODULE_NAME);
      //   lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", DOM_MODULE_NAME);
      //   ReleaseInterfaces();
      //   break;
      // }

      mm->RegInterface(&domination_interface, ALLARENAS);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);
      mm->RegCallback(CB_FLAGRESET, OnFlagReset, arena);

      mm->RegInterface(&flagcore_flaggame_interface, arena);

      OnArenaAttach(arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      OnArenaDetach(arena);

      mm->UnregInterface(&flagcore_flaggame_interface, arena);

      mm->UnregCallback(CB_FLAGRESET, OnFlagReset, arena);
      mm->UnregCallback(CB_ARENAACTION, OnArenaAction, arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_UNLOAD:
      if (mm->UnregInterface(&domination_interface, ALLARENAS)) {
        return MM_FAIL;
      }

      arenaman->FreeArenaData(adkey);
      // pd->FreePlayerData(pdkey);

      ReleaseInterfaces();
      return MM_OK;
  }

  return MM_FAIL;
}
