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
 *  - A player touches a flag to put the flag in the "touched" state
 *  - After a short duration, if the flag has not been touched by an enemy, the flag becomes
 *    "contested" by the player's team
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

////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////

#define DOM_MODULE_NAME "domination"

#define DOM_MIN(x, y) ((x) < (y) ? (x) : (y))
#define DOM_MAX(x, y) ((x) > (y) ? (x) : (y))
#define DOM_CLAMP(val, min, max) DOM_MIN(DOM_MAX((val), (min)), (max))



struct DomArena {
  Arena *arena;

  HashTable teams;
  short teams_initialized;

  HashTable regions;
  short regions_initialized;

  HashTable flags;
  short flags_initialized;

  DomGameState game_state;
  u_int32_t game_time_remaining;

  char updating_flag_status;

  u_int32_t cfg_team_count;
  u_int32_t cfg_region_count;
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
  Arena *arena;

  HashTable players;
  int players_initialized;

  const char *team_name;

  u_int32_t cfg_team_freq;
};

struct DomRegion {
  Arena *arena;
  int region_id;
  Region *region;

  DomRegionState state;

  HashTable flags;
  short flags_initialized;

  int controlling_freq;
  u_int32_t controller_influence;
  ticks_t last_influence_update;

  /* The name of the region */
  const char *cfg_region_name;

  /* Amount of control points provided by this region */
  u_int32_t cfg_region_value;

  /* Amount of influence required to control this region */
  u_int32_t cfg_required_influence;
};

struct DomFlag {
  Arena *arena;
  int flag_id;
  int x, y;

  DomFlagState state;

  HashTable regions;
  short regions_initialized;

  /* Controlling freq and their current influence */
  int controlling_freq;
  u_int32_t controller_influence;
  ticks_t last_influence_update;

  /* The freq that controls the physical flag; used for state management */
  int flag_freq;

  /* Amount of requisition provided by this flag */
  u_int32_t cfg_flag_requisition;
};

struct DomPlayer {
  u_int32_t playtime;
  ticks_t last_join_time
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
static int pdkey;



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
static int GetFlagProvidedRequisition(DomFlag *dflag);
static int GetFlagRequiredInfluence(DomFlag *dflag);
static int GetFlagAcquiredInfluence(DomFlag *dflag, DomTeam *dteam);
static DomTeam* GetFlagControllingTeam(DomFlag *dflag);
static void SetFlagState(DomFlag *dflag, DomFlagState state, DomTeam *controller, int influence, DomTeam *contesting);
static int GetRegionRequisition(DomRegion *dregion, DomTeam *dteam);
static int GetRegionProvidedControlPoints(DomRegion *dregion);
static int GetRegionRequiredInfluence(DomRegion *flag);
static int GetRegionAcquiredInfluence(DomRegion *dregion, DomTeam *dteam);
static DomTeam* getRegionControllingTeam(DomRegion *dregion);
static void SetRegionState(DomRegion *dregion, DomRegionState state, DomTeam *dteam, int influence);
static void UpdateFlagAcquiredInfluence(DomFlag *dflag);
static void UpdateRegionAcquiredInfluence(DomRegion *dregion);
static void ClearFlagTimers(DomFlag *dflag);
static void ClearRegionTimers(DomRegion *dregion);
static void ClearGameTimers(Arena *arena);
static void UpdateFlagStateTimer(void *param);
static void UpdateFlagState(Arena *arena);
static void OnArenaAttach(Arena *arena);
static void OnArenaDetach(Arena *arena);
static void OnArenaAction(Arena *arena, int action);
static void OnFlagGameInit(Arena *arena);
static void OnFlagTouch(Arena *arena, Player *player, int flag_id);
static void OnFlagContestTimer(void *param);
static void OnFlagCaptureTimer(void *param);
static void OnFlagStateChange(DomFlag *dflag, DomFlagState state, DomTeam *dteam, int influence);
static void OnRegionInfluenceTickTimer(void *param);
static void OnRegionStateChange(DomRegion *dregion, DomRegionState state, DomTeam *dteam, int influence);
static void OnFlagCleanup(Arena *arena, int flag_id, int reason, Player *carrier, int freq);
static void OnFlagReset(Arena *arena, int freq, int points);
static void OnGameStateChange(Arena *arena, DomGameState old_state, DomGameState new_state);
static void OnDominationTimer(void *param);
static void OnGameEndTimer(void *param);
static void OnPlayerAction(Player *player, int action, Arena *arena);
static void OnPlayerDeath(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
static void OnPlayerSpawn(Player *player, int reason);
static void OnPlayerFreqShipChange(Player *player, int newship, int oldship, int newfreq, int oldfreq);

////////////////////////////////////////////////////////////////////////////////////////////////////

static void SetErrorState(Arena *arena, const char* message, ...) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  adata->game_state = DOM_GAME_STATE_ERROR;
  ClearGameTimers(arena);

  FreeRegionData(arena);
  FreeFlagData(arena);
  FreeTeamData(arena);


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

    dregion->flags_initialized = 0;

    dregion->cfg_region_name = NULL;
    dregion->cfg_region_value = 0;
    dregion->cfg_required_influence = 0;

    dregion->controlling_freq = DOM_NEUTRAL_FREQ;
    dregion->controller_influence = 0;
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

    int region_id = 0;
    char cfg_key[255];

    for (int i = 0; i < adata->cfg_region_count; ++i) {
      if (!(dregion = AllocRegion(arena))) {
        return 0;
      }

      dregion->region_id = region_id++;

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

      HashReplace(&adata->regions, dregion->cfg_region_name, dregion);
    }
  }

  if (!adata->regions.ents) {
    SetErrorState(arena, "ERROR: No regions were configured in this arena.");
    return 0;
  }

  // If we've already loaded flag data, make sure we update flag-region linking
  if (adata->flags_initialized) {
    LinkedList *keys;
    Link *link;
    char *key;

    keys = HashGetKeys(&adata->flags);
    FOR_EACH(keys, key, link) {
      DomFlag *dflag = HashGetOne(&adata->flags, key);

      if (dflag) {
        // Sanity check. This probably shouldn't ever happen...
        if (dflag->regions_initialized) {
          SetErrorState(arena, "ERROR: Flag regions initialized before regions are loaded.");
          return 0;
        }

        if (MapFlagToRegions(dflag) < 1) {
          FreeFlag(dflag);
        }
      }
    }
  }

  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "Loaded data for %d regions", adata->regions.ents);

  return 1;
}

static void FreeRegion(DomRegion *dregion) {
  if (dregion->flags_initialized) {
    HashDeinit(&dregion->flags);
    dregion->flags_initialized = 0;
  }

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

      if (dflag && dflag->regions_initialized) {
        HashDeinit(&dflag->regions);
        dflag->regions_initialized = 0;
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

    dflag->regions_initialized = 0;

    dflag->controlling_freq = DOM_NEUTRAL_FREQ;
    dflag->controller_influence = 0;
    dflag->last_influence_update = 0;

    dflag->flag_freq = DOM_NEUTRAL_FREQ;

    dflag->cfg_flag_requisition = 0;
  } else {
    SetErrorState(arena, "ERROR: Unable to allocate memory for a new DomFlag instance");
  }

  return dflag;
}

static int LoadFlagData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomFlag *dflag;

  LinkedList *keys;
  Link *link;
  char *key;

  if (!adata->regions_initialized) {
    if (!LoadRegionData(arena)) {
      // We'll already be in an error state at this point -- no need to do it again.
      return 0;
    }
  }

  int flag_id = 0;
  FlagInfo flag_info = { FI_ONMAP, NULL, -1, -1, DOM_NEUTRAL_FREQ };

  HashInit(&adata->flags);
  adata->flags_initialized = 1;

  for (int y = 0; y < 1023; ++y) {
    for (int x = 0; x < 1023; ++x) {
      if (mapdata->GetTile(arena, x, y) == TILE_TURF_FLAG) {
        if (!(dflag = AllocFlag(arena))) {
          return 0;
        }

        char cfg_key[255];

        dflag->flag_id = flag_id++;
        dflag->x = x;
        dflag->y = y;

        // Sanity check
        if (dflag->flag_id > 255) {
          SetErrorState(arena, "ERROR: Map contains more than 255 flags.");
          return 0;
        }

        /* cfghelp: Domination:Flag1-Requisition, arena, int,
         * The amount of requisition this flag is worth. */
        sprintf(cfg_key, "Flag%d-%s", (dflag->flag_id + 1), "Requisition");
        dflag->cfg_flag_requisition = cfg->GetInt(arena->cfg, "Domination", cfg_key, 1);

        // Add regions containing this flag
        // If we didn't have any regions, it's not a flag we'll track; we can free this DomFlag
        // and move on to the next one
        if (MapFlagToRegions(dflag) > 0) {
          flagcore->SetFlags(arena, dflag->flag_id, &flag_info, 1);
          HashReplace(&adata->flags, ((char*) &dflag->flag_id), dflag);
        } else {
          FreeFlag(dflag);
        }
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

static int MapFlagToRegions(DomFlag *dflag) {
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);

  if (!dflag->regions_initialized) {
    HashInit(&dflag->regions);
    dflag->regions_initialized = 1;

    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion && mapdata->Contains(dregion->region, x, y)) {
        HashReplace(&dflag->regions, key, dregion);
      }
    }

    return dflag->regions.ents;
  }

  return -1;
}

static void FreeFlag(DomFlag *dflag) {
  if (dflag->regions_initialized) {
    HashDeinit(&dflag->regions);
    dflag->regions_initialized = 0;
  }

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

    dteam->players_initialized = 0;

    dteam->team_name = NULL;

    dteam->cfg_team_freq= DOM_NEUTRAL_FREQ;
  } else {
    SetErrorState(arena, "ERROR: Unable to allocate memory for a new DomTeam instance");
  }

  return dteam;
}

static int LoadTeamData(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomTeam *dteam;

  LinkedList *keys;
  Link *link;
  char *key;

  int team_id = 0;
  char cfg_key[255];

  if (!adata->teams_initialized) {
    HashInit(&adata->teams);
    adata->teams_initialized = 1;

    for (int i = 0; i < adata->cfg_team_count; ++i) {
      if (!(dteam = AllocTeam(arena))) {
        return 0;
      }

      HashInit(&dteam->players);
      dteam->players_initialized = 1;

      /* cfghelp: Domination:Team1-Freq, arena, int, def: 0
       * The frequency players on this team must be on to capture flags and regions. */
      sprintf(cfg_key, "Team%d-%s", (i + 1), "Freq");
      dteam->cfg_team_freq = cfg->GetInt(arena->cfg, "Domination", cfg_key, 0);

      if (HashGetOne(&adata->teams, ((char*) &dteam->cfg_team_freq))) {
        SetErrorState(arena, "ERROR: Multiple teams configured to use the same freq");
        return 0;
      }

      HashReplace(&adata->teams, ((char*) &dteam->cfg_team_freq), dteam);
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

  if (dteam->players_initialized) {
    HashDeinit(&dteam->players);
    dteam->players_initialized = 0;
  }

  if (dteam->team_name) {
    free(dteam->team_name);
  }

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
      DomFlag *dteam = HashGetOne(&adata->teams, key);
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
  LinkedList *keys;
  Link *link;
  char *key;

  /* cfghelp: Domination:TeamCount, arena, int, def: 3
   * The number of teams allowed to participate in the domination game. If this value is less
   * than two, two teams will be used. */
  adata->cfg_team_count = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "TeamCount", 3), 2);

  /* cfghelp: Domination:RegionCount, arena, int, range:1-255, def: 1
   * The number of regions to contested as part of the game. Each region will need to be configured
   * with the Domination.Region#-* settings. */
  adata->cfg_region_count = DOM_CLAMP(cfg->GetInt(arena->cfg, "Domination", "RegionCount", 1), 1, 255);

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

  if (adata->teams_initialized) {
    keys = HashGetKeys(&adata->teams);

    FOR_EACH(keys, key, link) {
      DomTeam *dteam = HashGetOne(&adata->teams, key);
      if (dteam) {
        // TODO: Load team config
      }
    }
  }

  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);

    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion) {
        // TODO: Load region config
      }
    }
  }

  if (adata->flags_initialized) {
    keys = HashGetKeys(&adata->flags);

    FOR_EACH(keys, key, link) {
      DomFlag *dflag = HashGetOne(&adata->flags, key);
      if (dflag) {
        // TODO: Load flag config
      }
    }
  }

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

  if (adata->flags_initialized && flag_id >= 0 && flag_id <= 255) {
    dflag = HashGetOne(&adata->flags, ((char*) &flag_id));
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

  if (adata->teams_initialized && freq >= 0 && freq <= 255) {
    dteam = HashGetOne(&adata->teams, ((char*) &freq));
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

static int GetFlagProvidedRequisition(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagProvidedRequisition called with a null dflag parameter", DOM_MODULE_NAME);
    return -1;
  }

  return dflag->cfg_flag_requisition;
}

static int GetFlagRequiredInfluence(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagRequiredInfluence called with a null dflag parameter", DOM_MODULE_NAME);
    return -1;
  }

  // Convert seconds to milliseconds and treat millis as influence.
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);
  return adata->cfg_flag_capture_time * 10;
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

  return dflag->controlling_freq == dteam->cfg_team_freq ? dflag->controller_influence : 0;
}

static DomTeam* GetFlagControllingTeam(DomFlag *dflag) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: GetFlagControllingTeam called with a null dflag parameter", DOM_MODULE_NAME);
    return NULL;
  }

  return GetDomTeam(dflag->arena, dflag->controlling_freq);
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
 * @param *controller
 *  The DomTeam that controls the logical flag
 *  Note: This is not the controller of the actual in-game turf flag entity
 *
 * @param influence
 *  The amount of influence the controlling team has over the flag
 *
 * @param *flag_entity_controller
 *  The DomTeam that controls the physical turf flag entity
 *  Note: The flag entity controller is not the actual logical domination controller
 */
static void SetFlagState(DomFlag *dflag, DomFlagState state, DomTeam *controller, int influence, DomTeam *flag_entity_controller) {
  if (!dflag) {
    lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a null dflag parameter", DOM_MODULE_NAME);
    return;
  }

  // Some sanity checks...
  switch (state) {
    case DOM_FLAG_STATE_NEUTRAL:
      controller = NULL;
      flag_entity_controller = NULL;
      influence = 0;
      break;

    case DOM_FLAG_STATE_CONTESTED:
      if (!flag_entity_controller) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CONTESTED state and no flag entity controller", DOM_MODULE_NAME);
        return;
      }
      break;

    case DOM_FLAG_STATE_CAPTURING:
      if (!flag_entity_controller) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CAPTURING state and no flag entity controller", DOM_MODULE_NAME);
        return;
      }
      break;

    case DOM_FLAG_STATE_CONTROLLED:
      if (!controller) {
        lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with a CONTROLLED state and no controller", DOM_MODULE_NAME);
        return;
      }

      flag_entity_controller = controller;
      break;

    default:
      lm->Log(L_ERROR, "<%s> ERROR: SetFlagState called with an invalid flag state: %d", DOM_MODULE_NAME, state);
      return;
  }

  int controlling_freq = (controller ? controller->cfg_team_freq : DOM_NEUTRAL_FREQ);
  int flag_entity_freq = (flag_entity_controller ? flag_entity_controller->cfg_team_freq : DOM_NEUTRAL_FREQ);

  int change = (dflag->state != state) ||
    (dflag->controlling_freq != controlling_freq) ||
    (dflag->controller_influence != influence) ||
    (dflag->flag_freq != flag_entity_freq);

  dflag->state = state;
  dflag->controlling_freq = controlling_freq;
  dflag->controller_influence = influence;
  dflag->flag_freq = flag_entity_freq;

  FlagInfo info = { FI_ONMAP, NULL, -1, -1, dflag->flag_freq };
  flagcore->SetFlags(dflag->arena, dflag->flag_id, &info, 1);
  UpdateFlagState(dflag->arena);

  if (change) {
    dflag->last_influence_update = current_millis();

    chat->SendArenaMessage(dflag->arena, "Flag state change! Flag ID: %d, state: %d, team: %d, influence: %d, entity team: %d", dflag->flag_id, state, controlling_freq, influence, flag_entity_freq);
  }
}

static int GetRegionRequisition(DomRegion *dregion, DomTeam *dteam) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionRequisition called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionRequisition called with a null dteam parameter", DOM_MODULE_NAME);
    return -1;
  }

}

static int GetRegionProvidedControlPoints(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionProvidedControlPoints called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

}

static int GetRegionRequiredInfluence(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionRequiredInfluence called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  return dregion->cfg_required_influence;
}

static int GetRegionAcquiredInfluence(DomRegion *dregion, DomTeam *dteam) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionAcquiredInfluence called with a null dregion parameter", DOM_MODULE_NAME);
    return -1;
  }

  if (!dteam) {
    lm->Log(L_ERROR, "<%s> ERROR: GetRegionAcquiredInfluence called with a null dteam parameter", DOM_MODULE_NAME);
    return -1;
  }

  UpdateRegionAcquiredInfluence(dregion);

  return dregion->controlling_freq == dteam->cfg_team_freq ? dregion->controller_influence : 0;
}

static DomTeam* getRegionControllingTeam(DomRegion *dregion) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: getRegionControllingTeam called with a null dregion parameter", DOM_MODULE_NAME);
    return DOM_NEUTRAL_FREQ;
  }

  return dregion->controlling_freq;
}

static void SetRegionState(DomRegion *dregion, DomRegionState state, DomTeam *dteam, int influence) {
  if (!dregion) {
    lm->Log(L_ERROR, "<%s> ERROR: SetRegionState called with a null dregion parameter", DOM_MODULE_NAME);
    return;
  }

  UpdateRegionAcquiredInfluence(dregion);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void UpdateFlagAcquiredInfluence(DomFlag *dflag) {
  if (dflag->last_influence_update) {
    ticks_t ctime = current_millis();
    ticks_t elapsed = ctime - dflag->last_influence_update;
    int required_influence = GetFlagRequiredInfluence(dflag);

    switch (dflag->state) {
      case DOM_FLAG_STATE_CAPTURING:
        if (dflag->controlling_freq == dflag->flag_freq) {
          chat->SendArenaMessage(dflag->arena, "UPDATING FLAG INFLUENCE -- Flag owned by controller; adding %d influence", elapsed);
          // Add influence up to required influence
          dflag->controller_influence += elapsed;

          if (dflag->controller_influence > required_influence) {
            dflag->controller_influence = required_influence;
          }

        } else {
          chat->SendArenaMessage(dflag->arena, "UPDATING FLAG INFLUENCE -- Flag owned by enemy; subtracting %d influence", elapsed);
          // Subtract influence down to zero
          dflag->controller_influence -= elapsed;

          if (dflag->controller_influence < 0) {
            dflag->controller_influence = 0;
          }
        }

        chat->SendArenaMessage(dflag->arena, "UPDATED FLAG INFLUENCE: flag %d, inf: %d", dflag->flag_id, dflag->controller_influence);

      default:
        // Influence does not accumulate in these states. Just update the timer.
        dflag->last_influence_update = ctime;
    }
  }
}

static void UpdateRegionAcquiredInfluence(DomRegion *dregion) {
  if (dregion->last_influence_update) {
    ticks_t ctime = current_millis();
    ticks_t elapsed = ctime - dregion->last_influence_update;

    // Things we need:
    // Freq with most req
    // Req per freq

    switch (dregion->state) {
      case DOM_REGION_STATE_CAPTURING:
        if (dflag->controlling_freq == dflag->flag_freq) {
          chat->SendArenaMessage(dflag->arena, "UPDATING FLAG INFLUENCE -- Flag owned by controller; adding %d influence", elapsed);
          // Add influence up to required influence
          dflag->controller_influence += elapsed;

          if (dflag->controller_influence > required_influence) {
            dflag->controller_influence = required_influence;
          }

        } else {
          chat->SendArenaMessage(dflag->arena, "UPDATING FLAG INFLUENCE -- Flag owned by enemy; subtracting %d influence", elapsed);
          // Subtract influence down to zero
          dflag->controller_influence -= elapsed;

          if (dflag->controller_influence < 0) {
            dflag->controller_influence = 0;
          }
        }

        chat->SendArenaMessage(dflag->arena, "UPDATED FLAG INFLUENCE: flag %d, inf: %d", dflag->flag_id, dflag->controller_influence);

      default:
        // Influence does not accumulate in these states. Just update the timer.
        dflag->last_influence_update = ctime;
    }
  }
}

static void ClearFlagTimers(DomFlag *dflag) {
  mainloop->ClearTimer(OnFlagContestTimer, dflag);
  mainloop->ClearTimer(OnFlagCaptureTimer, dflag);
}

static void ClearRegionTimers(DomRegion *dregion) {

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
        mainloop->ClearTimer(OnFlagContestTimer, dflag);
        mainloop->ClearTimer(OnFlagCaptureTimer, dflag);
      }
    }
  }

  if (adata->regions_initialized) {
    keys = HashGetKeys(&adata->regions);
    FOR_EACH(keys, key, link) {
      DomRegion *dregion = HashGetOne(&adata->regions, key);
      if (dregion) {
        // Clear region timers
      }
    }
  }

  // Clear region timers
}

static void UpdateFlagStateTimer(void *param) {
  DomArena *adata = P_ARENA_DATA((Arena*) param, adkey);

  flagcore->SendTurfStatus((Arena*) param);
  mainloop->ClearTimer(UpdateFlagStateTimer, param);
  adata->updating_flag_status = 0;
}

static void UpdateFlagState(Arena *arena) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);

  if (!adata->updating_flag_status) {
    adata->updating_flag_status = 1;
    mainloop->SetTimer(UpdateFlagStateTimer, 100, 0, arena, arena);
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void OnArenaAttach(Arena *arena) {
  // Initialize arena data
  // ReadArenaConfig(arena);
  InitArenaData(arena);

  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "OnArenaAttach");



}

static void OnArenaDetach(Arena *arena) {
  // Free arena data

}

static void OnArenaAction(Arena *arena, int action) {
  switch (action) {
    case AA_CREATE:

    case AA_CONFCHANGED:
      ReadArenaConfig(arena);
      break;
  }
}

static void OnFlagGameInit(Arena *arena) {
  lm->LogA(L_INFO, DOM_MODULE_NAME, arena, "OnFlagGameInit");

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

  ReadArenaConfig(arena) && LoadTeamData(arena) && LoadRegionData(arena) && LoadFlagData(arena);
}






static void OnFlagTouch(Arena *arena, Player *player, int flag_id) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomFlag *dflag = GetDomFlag(arena, flag_id);
  DomTeam *dteam = GetDomTeam(arena, player->p_freq);

  if (dflag) {
    UpdateFlagAcquiredInfluence(dflag);

    if (dteam && adata->game_state == DOM_GAME_STATE_ACTIVE) {
      // A player is contesting a flag
      ClearFlagTimers(dflag);
      SetFlagState(dflag, DOM_FLAG_STATE_CONTESTED, GetDomTeam(arena, dflag->controlling_freq), dflag->controller_influence, dteam);
      mainloop->SetTimer(OnFlagContestTimer, adata->cfg_flag_contest_time, 0, dflag, dflag);
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

static void OnFlagContestTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);
  DomTeam *dteam;

  ClearFlagTimers(dflag);
  int req_influence = GetFlagRequiredInfluence(dflag);
  int remaining_time;

  if (adata->game_state == DOM_GAME_STATE_ACTIVE) {
    UpdateFlagAcquiredInfluence(dflag);
    chat->SendArenaMessage(dflag->arena, "Running flag capture timer handler");

    if (dflag->flag_freq != dflag->controlling_freq || dflag->controller_influence < req_influence) {
      if (dflag->controlling_freq == DOM_NEUTRAL_FREQ) {
        // Flag has not yet been controlled. Give it to the new team
        dteam = GetDomTeam(dflag->arena, dflag->flag_freq);
        remaining_time = req_influence / 10;

        chat->SendArenaMessage(dflag->arena, "Flag is neutral. Beginning immediate capture for team %d", dteam->cfg_team_freq);
        SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, dteam, 0, dteam);
      } else {
        dteam = GetDomTeam(dflag->arena, dflag->controlling_freq);
        remaining_time = (dflag->flag_freq == dflag->controlling_freq ? (req_influence - dflag->controller_influence) : dflag->controller_influence) / 10;

        chat->SendArenaMessage(dflag->arena, "Flag is controlled. Beginning flag %s for team %d (controller: %d)", dflag->flag_freq == dflag->controlling_freq ? "capture" : "decapture", dflag->flag_freq, dflag->controlling_freq);
        SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, dteam, dflag->controller_influence, GetDomTeam(dflag->arena, dflag->flag_freq));
      }

      chat->SendArenaMessage(dflag->arena, "Setting capture timer to run in %d ticks (out of %d). influence: %d, req inf: %d", remaining_time, req_influence / 10, dflag->controller_influence, req_influence);
      mainloop->SetTimer(OnFlagCaptureTimer, remaining_time, 0, dflag, dflag);
    } else {
      chat->SendArenaMessage(dflag->arena, "Flag is controlled and controlling team didn't lose any influence. Set state to CONTROLLED for team %d", dflag->controlling_freq);
      dteam = GetDomTeam(dflag->arena, dflag->controlling_freq);
      SetFlagState(dflag, DOM_FLAG_STATE_CONTROLLED, dteam, req_influence, dteam);
    }
  }
}

static void OnFlagCaptureTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);
  DomTeam *dteam;

  ClearFlagTimers(dflag);
  int req_influence = GetFlagRequiredInfluence(dflag);

  if (adata->game_state == DOM_GAME_STATE_ACTIVE) {
    UpdateFlagAcquiredInfluence(dflag);

    chat->SendArenaMessage(dflag->arena, "Running flag capture timer handler");

    if (dflag->flag_freq != dflag->controlling_freq) {
      // Flag has been "flipped" to a new freq
      dteam = GetDomTeam(dflag->arena, dflag->flag_freq);
      chat->SendArenaMessage(dflag->arena, "Flag is controlled by a team other than the team controlling the flag. Flag must have been controlled and is flipping. New team: %d, old team: %d", dflag->flag_freq, dflag->controlling_freq);

      SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, dteam, 0, dteam);
      mainloop->SetTimer(OnFlagCaptureTimer, (req_influence / 10), 0, dflag, dflag);
    } else {
      // Flag has been fully controlled
      dteam = GetDomTeam(dflag->arena, dflag->controlling_freq);
      chat->SendArenaMessage(dflag->arena, "Flag is fully controlled by the new team. Team: %d", dflag->controlling_freq);

      SetFlagState(dflag, DOM_FLAG_STATE_CONTROLLED, dteam, req_influence, dteam);
    }
  }
}

static void OnFlagStateChange(DomFlag *dflag, DomFlagState state, DomTeam *dteam, int influence) {
  chat->SendArenaMessage(dflag->arena, "Flag state change! Flag ID: %d, state: %d, team: %d, influence: %d", dflag->flag_id, state, dteam->cfg_team_freq, influence);

  // for each region:
  //  for each flag in region:
  //    if flag state == CONTROLLED
  //      add flag's influence value to team's total region influence

  // determine team with the most influence (iteam)
  // if iteam's influence == 0
  //  set region state to NEUTRAL
  // elseif multiple teams are tied for most influence:
  //  set region state to CONTESTED
  // elseif region's controlling team != iteam or controlling team's
  // total requisition is lower than the required req for the region:
  //  set region capture timer
  //  set region state to CAPTURING
  // else
  //  set region state to CONTROLLED
}

static void OnRegionInfluenceTickTimer(void *param) {
  // Update influence for all regions

  // For each region:
  //  For each team:
  //    team influence += max(team req - sum of enemy req, 0)
  //
  //
}

static void OnRegionStateChange(DomRegion *dregion, DomRegionState state, DomTeam *dteam, int influence) {
  // If region state == controlled and team controls all other regions:
  //  set domination timer
  // else
  //  clear domination timer
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
  // Not sure what to do here quite yet
}






static void OnDominationTimer(void *param) {
  // Dominating team wins
  // Set game state to FINISHED
}

static void OnGameEndTimer(void *param) {
  // Game ended via time
  // Set game state to FINISHED
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
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);

      mm->RegInterface(&flagcore_flaggame_interface, arena);

      OnArenaAttach(arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      OnArenaDetach(arena);

      mm->UnregInterface(&flagcore_flaggame_interface, arena);

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
