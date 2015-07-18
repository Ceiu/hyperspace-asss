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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "packets/ppk.h"

#include "domination.h"



////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////

#define DOM_MODULE_NAME "domination"

#define DOM_MIN(x, y) ((x) < (y) ? (x) : (y))
#define DOM_MAX(x, y) ((x) > (y) ? (x) : (y))
#define DOM_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

#define DOM_NEUTRAL_FREQ -1

struct DomArena {
  Arena *arena;

  HashTable teams;
  short teams_loaded;

  HashTable regions;
  short regions_loaded;

  HashTable flags;
  short flags_loaded;

  DomGameState game_state;
  u_int32_t game_time_remaining;


  u_int32_t cfg_team_count;
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

  u_int32_t cfg_team_freq;
  const char *team_name;
};

struct DomRegion {
  Arena *arena;

  DomRegionState state;
  Region *region;
  const char *region_name;

  HashTable flags;
  short flags_loaded;

  /* Amount of control points provided by this region */
  u_int32_t cfg_region_value;

  /* Amount of influence required to control this region */
  u_int32_t cfg_required_influence;

  int controller_freq;
  u_int32_t controller_influence;
};

struct DomFlag {
  Arena *arena;
  int flag_id;

  DomFlagState state;

  HashTable regions;
  short regions_loaded;

  /* Amount of requisition provided by this flag */
  u_int32_t cfg_flag_requisition;

  /* Controlling freq and their current influence */
  int controller_freq;
  u_int32_t influence;

  /* The freq that controls the physical flag; used for state management */
  int flag_freq;
};







// Interfaces
static Imodman *mm;

static Iarenaman *arenaman;
static Iconfig *cfg;
static Ichat *chat;
static Iflag *flagcore;
static Ilogman *lm;
static Imainloop *mainloop;
static Imapdata *mapdata;
static Iplayerdata *pd;

// Global resource identifiers
static int adkey;
static int pdkey;


////////////////////////////////////////////////////////////////////////////////////////////////////


static void InitArenaData(Arena *arena) {
  ArenaGameData *adata = P_ARENA_DATA(arena, adkey);

  HashInit(&adata->teams);
  HashInit(&adata->regions);
  HashInit(&adata->flags);

  adata->teams_loaded = 0;
  adata->regions_loaded = 0;
  adata->flags_loaded = 0;

  adata->game_state = DOM_GAME_STATE_INACTIVE;
  adata->game_time_remaining = 0;
}

static void ReadArenaConfig(Arena *arena) {
  ArenaGameData *adata = P_ARENA_DATA(arena, adkey);

  /* cfghelp: Domination:TeamCount, arena, int, def: 3
   * The number of teams allowed to participate in the domination game. If this value is less
   * than two, two teams will be used. */
  adata->cfg_team_count = DOM_MAX(cfg->getInt(arena->cfg, "Domination", "TeamCount", 3), 2);

  /* cfghelp: Domination:MinimumPlayers, arena, int, def: 3
   * The minimum number of active players in the arena required to start a new game. If set to zero,
   * no global minimum will be enforced. */
  adata->cfg_min_players = DOM_MAX(cfg-getInt(arena->cfg, "Domination", "MinimumPlayers", 3), 0);

  /* cfghelp: Domination:MinPlayersPerTeam, arena, int, def: 1
   * The minimum number of active players that must be on each team to start a new game. If set to
   * zero, no per-team minimum will be enforced. */
  adata->cfg_min_players_per_team = DOM_MAX(cfg->getInt(arena->cfg, "Domination", "MinPlayersPerTeam", 1), 0);

  /* cfghelp: Domination:GameDuration, arena, int, def: 60
   * The length of a domination game in minutes. If this value is less than one, the game will
   * not have a time limit, ending only upon domination. */
  adata->cfg_game_duration = cfg->getInt(arena->cfg, "Domination", "GameDuration", 60);
  /* cfghelp: Domination:GameCooldown, arena, int, def: 15
   * The amount of time between games, in minutes. If set to zero, a new game will start
   * immediately upon the conclusion of the previous. */
  adata->cfg_game_cooldown = cfg->getInt(arena->cfg, "Domination", "GameCooldown", 15);
  /* cfghelp: Domination:GameCooldownRandom, arena, int, def: 5
   * The amount of time randomly added to the game cooldown. Ignored if GameCooldown is set to
   * zero. */
  adata->cfg_game_cooldown_random = cfg->getInt(arena->cfg, "Domination", "GameCooldownRandom", 5);

  /* cfghelp: Domination:FlagCaptureTime, arena, int, def: 180
   * The amount of time needed to fully capture a flag, in seconds. */
  adata->cfg_flag_capture_time = cfg->getInt(arena->cfg, "Domination", "FlagCaptureTime", 180);
  /* cfghelp: Domination:FlagContestTime, arena, int, def: 10
   * The amount of time in seconds after a flag is touched to transition it to the contested or
   * controlled state. */
  adata->cfg_flag_contest_time = cfg->getInt(arena->cfg, "Domination", "FlagContestTime", 10);
  /* cfghelp: Domination:DefenseEventFrequency, arena, int, def: 90
   * The frequency, in seconds, at which to fire defense events for players for defending
   * controlled regions. If set to zero, defense events will be disabled. */
  adata->cfg_defense_reward_frequency = cfg->getInt(arena->cfg, "Domination", "DefenseEventFrequency", 90);
  /* cfghelp: Domination:DefenseEventRadius, arena, int, def: 500
   * The maximum distance, in pixels, the center of a player's ship can be from the center of a
   * region to fire a defense event. */
  adata->cfg_defense_reward_radius = cfg->getInt(arena->cfg, "Domination", "DefenseEventRadius", 500);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static DomFlag* GetDomFlag(int flag_id) {

}

static DomTeam* GetDomTeam(int freq) {

}

static DomRegion* GetDomRegion(const char *region) {

}

static int GetFlagProvidedRequisition(DomFlag *flag) {

}

static int GetFlagRequiredInfluence(DomFlag *flag) {
  // This is a constant at the moment
  return 100;
}

static int GetFlagAcquiredInfluence(DomFlag *flag, DomTeam *team) {
  UpdateFlagAcquiredInfluence(flag);

  return flag->controlling_freq == team->freq ? flag->influence : 0;
}

static DomTeam* GetFlagControllingTeam(DomFlag *flag) {

}

static void SetFlagState(DomFlag *flag, DomFlagState state, DomTeam *team, int influence) {
  UpdateFlagAcquiredInfluence(flag);
}

static int GetRegionRequisition(DomRegion *region, DomTeam *team) {

}

static int GetRegionProvidedControlPoints(DomRegion *region) {

}

static int GetRegionRequiredInfluence(DomRegion *flag) {

}

static int GetRegionAcquiredInfluence(DomRegion *region, DomTeam *team) {
  UpdateRegionAcquiredInfluence(region);
}

static DomTeam* getRegionControllingTeam(DomRegion *region) {

}

static void SetRegionState(DomRegion *region, DomRegionState state, DomTeam *team, int influence) {
  UpdateRegionAcquiredInfluence(region);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void UpdateFlagAcquiredInfluence(DomFlag *flag) {

}

static void UpdateRegionAcquiredInfluence(DomRegion *region) {

}

static void ClearFlagTimers(DomFlag *flag) {
  mainloop->ClearTimer(OnFlagContestTimer, dflag);
  mainloop->ClearTimer(OnFlagCaptureTimer, dflag);
}

static void ClearRegionTimers(DomRegion *region) {

}

static void ClearGameTimers(Arena *arena) {

}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void OnArenaAttach(Arena *arena) {
  // Initialize arena data





}

static void OnArenaDetach(Arena *arena) {
  // Free arena data

}

static void OnArenaAction(Arena *arena, int action) {
  switch (action) {
    case AA_CREATE:


    case AA_CONFCHANGED:

      break;
  }
}

static void OnFlagGameInit(Arena *arena) {
  flagcore->SetCarryMode(arena, CARRY_NONE);
}






static void OnFlagTouch(Arena *arena, Player *player, int flag_id) {
  DomArena *adata = P_ARENA_DATA(arena, adkey);
  DomFlag *dflag = getDomFlag(flag_id);
  DomTeam *dteam = getDomTeam(player->freq);
  FlagInfo info;

  if (dflag) {
    if (dteam && arena_data->game_state == DOM_GAME_STATE_ACTIVE) {
      // A player is contesting a flag
      dflag->flag_freq = player->freq;

      ClearFlagTimers(dflag);
      SetFlagState(dflag, DOM_FLAG_STATE_CONTESTED, dflag->controlling_freq, dflag->controlling_req);
      mainloop->SetTimer(OnFlagContestTimer, adata->cfg_flag_contest_time * 100, 0, dflag, dflag);
    } else {
      // Touched by someone outside of the domination game or the game is not active. Restore the
      // flag state.
      info = (FlagInfo) { FI_ONMAP, NULL, -1, -1, dflag->flag_freq };
      flagcore->SetFlags(arena, flag_id, &info, 1);
    }
  } else {
    // We don't care about this flag
  }
}

static void OnFlagContestTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);

  ClearFlagTimers(dflag);
  int req_influence = GetFlagRequiredInfluence(dflag);

  if (dflag->flag_freq != dflag->controlling_freq || dflag->influence < req_influence) {
    // Flag has not yet been controlled. Give it to the new team
    if (dflag->controlling_freq == DOM_NEUTRAL_FREQ) {
      dflag->controlling_freq = dflag->flag_freq;
      dflag->influence = 0;
    }

    SetFlagState(dflag, DOM_FLAG_STATE_CAPTURING, dflag->controlling_freq, dflag->influence);

    // Determine amount of time remaining until capture
    float pct_time = dflag->influence ? ((float) dflag->influence / (float) req_influence) : 1.0;

    if (dflag->flag_freq == dflag->controlling_freq) {
      pct_time = 1 - pct_time;
    }

    mainloop->SetTimer(OnFlagCaptureTimer, 100 * ((float) adata->cfg_flag_capture_time * pct_time), 0, dflag, dflag);
  } else {
    // Sanity assignment
    dflag->influence = req_influence;

    SetFlagState(dflag, DOM_FLAG_STATE_CONTROLLED, dflag->controlling_freq, req_influence);
  }
}

static void OnFlagCaptureTimer(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  DomArena *adata = P_ARENA_DATA(dflag->arena, adkey);

  ClearFlagTimers(dflag);
  int req_influence = GetFlagRequiredInfluence(dflag);

  if (dflag->flag_freq != dflag->controlling_freq) {
    // Flag has been "flipped" to a new freq
    dflag->controlling_freq = dflag->flag_freq;
    dflag->influence = 0;

    mainloop->SetTimer(OnFlagCaptureTimer, 100 * ((float) adata->cfg_flag_capture_time * pct_time), 0, dflag, dflag);
  } else {
    // Flag has been fully controlled
    dflag->influence = req_influence;
    SetFlagState(dflag, DOM_FLAG_STATE_CONTROLLED, dflag->controlling_freq, req_influence);
  }
}

static void OnFlagStateChange(DomFlag *dflag, DomFlagState state, DomTeam *team, int influence) {
  chat->SendArenaMessage(dflag->arena, "Flag state change! Flag ID: %d, state: %d, team: %d, influence: %d", dflag->flag_id, state, team->freq, influence);

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

static void OnRegionStateChange(DomRegion *region, DomRegionState state, DomTeam *team, int influence) {
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











static void OnPlayerAction(Player *player, int action, Arena *arena)
{
  switch (action) {
    case PA_ENTERARENA:
      break;

    case PA_LEAVEARENA:
      break;
  }
}

static void OnPlayerDeath(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{

}

static void OnPlayerSpawn(Player *player, int reason)
{

}

static void OnPlayerFreqShipChange(Player *player, int newship, int oldship, int newfreq, int oldfreq)
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////

static Iflaggame flagcore_interface = {
  INTERFACE_HEAD_INIT(I_FLAGGAME, "domination_flaggame")
  OnFlagGameInit,
  OnFlagTouch,
  OnFlagCleanup
}



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
    mainloop  = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

    return mm && (arenaman && cfg && chat && flagcore && lm && mainloop && pd);
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
      adkey = arenaman->AllocateArenaData(sizeof(ArenaGameData));
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
      OnArenaAttach(arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      OnArenaDetach(arena);
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
