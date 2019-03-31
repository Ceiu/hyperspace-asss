/**
 * hs_domination_rewards.c
 * Provides hyperspace-specific player rewards for domination.
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
#include "formula.h"
#include "domination/domination.h"


////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////

#define DOM_REWARDS_MODULE_NAME "hs_domination_rewards"

#define DOM_MIN(x, y) ((x) < (y) ? (x) : (y))
#define DOM_MAX(x, y) ((x) > (y) ? (x) : (y))
#define DOM_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

#define DOM_EVALUATE_FORMULA(result, adata, formula, formula_vars, ebuffer, cfg_section, cfg_key) \
  ebuffer[0] = 0; \
  result = formulas->EvaluateFormula(formula, formula_vars, NULL, ebuffer, sizeof(ebuffer)); \
  \
  if (ebuffer[0] != 0) { \
    adata->active = 0; \
    lm->LogA(L_ERROR, DOM_REWARDS_MODULE_NAME, arena, "Unable to evaluate formula \"%s.%s\": %s", cfg_section, cfg_key, ebuffer); \
    return; \
  } \

#define DOM_EVALUATE_FORMULA_TICK(result, adata, formula, formula_vars, ebuffer, cfg_section, cfg_key) \
  ebuffer[0] = 0; \
  result = formulas->EvaluateFormula(formula, formula_vars, NULL, ebuffer, sizeof(ebuffer)); \
  \
  if (ebuffer[0] != 0) { \
    adata->active = 0; \
    lm->LogA(L_ERROR, DOM_REWARDS_MODULE_NAME, arena, "Unable to evaluate formula \"%s.%s\": %s", cfg_section, cfg_key, ebuffer); \
    return 0; \
  } \


typedef struct ArenaData ArenaData;


struct ArenaData {
  u_int32_t cfg_flag_reward_radius;
  u_int32_t cfg_capture_tick_delay;

  Formula *cfg_contest_reward_hsd_formula;
  Formula *cfg_contest_reward_exp_formula;
  Formula *cfg_capture_tick_reward_hsd_formula;
  Formula *cfg_capture_tick_reward_exp_formula;
  Formula *cfg_capture_reward_hsd_formula;
  Formula *cfg_capture_reward_exp_formula;
  Formula *cfg_region_capture_hsd_formula;
  Formula *cfg_region_capture_exp_formula;
  Formula *cfg_defense_reward_hsd_formula;
  Formula *cfg_defense_reward_exp_formula;
  Formula *cfg_domination_win_reward_hsd_formula;
  Formula *cfg_domination_win_reward_exp_formula;
  Formula *cfg_domination_loss_reward_hsd_formula;
  Formula *cfg_domination_loss_reward_exp_formula;
  Formula *cfg_domination_tie_reward_hsd_formula;
  Formula *cfg_domination_tie_reward_exp_formula;

  char regions_initialized;
  HashTable region_owners;

  char flags_initialized;
  HashTable flag_owners;

  char active;
};




// Interfaces
static Imodman *mm;

static Iarenaman *arenaman;
static Iconfig *cfg;
static Ichat *chat;
static Idomination *dom;
static Iformula *formulas;
static Ihscoredatabase *database;
static Ihscoreitems *items;
static Ilogman *lm;
static Imainloop *mainloop;
static Iplayerdata *pd;


// Global resource identifiers
static int adkey;
static int pdkey;

////////////////////////////////////////////////////////////////////////////////////////////////////

static void InitArenaConfig(Arena *arena);
static void ReadArenaConfig(Arena *arena);
static void AwardPlayer(Player *player, int hsd_reward, int exp_reward, MoneyType reward_type, char *reason_msg);
static void ResetArenaData(Arena *arena);
static void OnArenaAttach(Arena *arena);
static void OnArenaDetach(Arena *arena);
static void OnArenaAction(Arena *arena, int action);
static void OnDomGameStateChanged(Arena *arena, DomGameState prev_state, DomGameState new_state);
static void OnDomRegionStateChanged(Arena *arena, DomRegion *dregion, DomRegionState prev_state, DomRegionState new_state);
static void OnDomFlagStateChanged(Arena *arena, DomFlag *dflag, DomFlagState prev_state, DomFlagState new_state);
static int OnFlagCaptureRewardTick(void *param);
static void OnDomAlert(Arena *arena, DomAlertType type, DomAlertState state, ticks_t duration);
static void OnDomFlagDefense(Arena *arena, DomFlag *dflag, LinkedList *players);

////////////////////////////////////////////////////////////////////////////////////////////////////

static void InitArenaConfig(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  adata->cfg_flag_reward_radius = 0;
  adata->cfg_capture_tick_delay = 0;

  adata->cfg_contest_reward_hsd_formula = NULL;
  adata->cfg_contest_reward_exp_formula = NULL;
  adata->cfg_capture_tick_reward_hsd_formula = NULL;
  adata->cfg_capture_tick_reward_exp_formula = NULL;
  adata->cfg_capture_reward_hsd_formula = NULL;
  adata->cfg_capture_reward_exp_formula = NULL;
  adata->cfg_region_capture_hsd_formula = NULL;
  adata->cfg_region_capture_exp_formula = NULL;
  adata->cfg_defense_reward_hsd_formula = NULL;
  adata->cfg_defense_reward_exp_formula = NULL;
  adata->cfg_domination_win_reward_hsd_formula = NULL;
  adata->cfg_domination_win_reward_exp_formula = NULL;
  adata->cfg_domination_loss_reward_hsd_formula = NULL;
  adata->cfg_domination_loss_reward_exp_formula = NULL;
  adata->cfg_domination_tie_reward_hsd_formula = NULL;
  adata->cfg_domination_tie_reward_exp_formula = NULL;

  adata->regions_initialized = 0;
  adata->flags_initialized = 0;

  adata->active = 0;
}

static void ReadArenaConfig(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  const char *fbuffer;
  char ebuffer[2048];

  // Clear the active flag -- only set it if we finish processing everything
  adata->active = 0;

  /* cfghelp: Domination:FlagRewardRadius, arena, int, def: 500
   * The maximum distance, in pixels, the center of a player's ship can be from the center of a
   * region to receive any flag-specific rewards (contest, capture, control, defend). If set to
   * zero, flag rewards will be disabled. */
  adata->cfg_flag_reward_radius = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "FlagRewardRadius", 500), 0);
  adata->cfg_flag_reward_radius *= adata->cfg_flag_reward_radius;

  /* cfghelp: Domination:CaptureRewardTickDelay, arena, int, def: 3000
   * The delay between periodic flag capture rewards. If set to zero, tick-based capture rewards
   * will be disabled. */
  adata->cfg_capture_tick_delay = DOM_MAX(cfg->GetInt(arena->cfg, "Domination", "CaptureRewardTickDelay", 3000), 0);

#define DOM_PARSE_CONFIG_FORMULA(formula_var, cfg_section, cfg_key, default_formula) \
  formulas->FreeFormula(formula_var); \
  fbuffer = cfg->GetStr(arena->cfg, cfg_section, cfg_key); \
  if (!fbuffer) { \
    fbuffer = default_formula; \
  } \
  \
  if (fbuffer) { \
    formula_var = formulas->ParseFormula(fbuffer, ebuffer, sizeof(ebuffer)); \
    if (!formula_var) { \
      lm->LogA(L_ERROR, DOM_REWARDS_MODULE_NAME, arena, "Unable to parse formula \"%s.%s\": %s", cfg_section, cfg_key, ebuffer); \
      return; \
    } \
  }

  /* cfghelp: Domination:ContestRewardHSDFormula, arena, string, def: 1
   * Formula to use to determine the HSD reward for contesting a flag. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_contest_reward_hsd_formula, "Domination", "ContestRewardHSDFormula", "1");

  /* cfghelp: Domination:ContestRewardHSDFormula, arena, string, def: 1
   * Formula to use to determine the EXP reward for contesting a flag. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_contest_reward_exp_formula, "Domination", "ContestRewardEXPFormula", "1");

  /* cfghelp: Domination:CaptureTickRewardHSDFormula, arena, string, def: 1
   * Formula to use to determine the HSD reward for guarding a flag during a capture. Players must
   * be near the flag to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_capture_tick_reward_hsd_formula, "Domination", "CaptureTickRewardHSDFormula", "1");

  /* cfghelp: Domination:CaptureTickRewardEXPFormula, arena, string, def: 1
   * Formula to use to determine the EXP reward for guarding a flag during a capture. Players must
   * be near the flag to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_capture_tick_reward_exp_formula, "Domination", "CaptureTickRewardEXPFormula", "1");

  /* cfghelp: Domination:CaptureRewardHSDFormula, arena, string, def: 1
   * Formula to use to determine the HSD reward for capturing a flag. Players must be near the flag
   * to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_capture_reward_hsd_formula, "Domination", "CaptureRewardHSDFormula", "1");

  /* cfghelp: Domination:CaptureRewardEXPFormula, arena, string, def: 1
   * Formula to use to determine the EXP reward for capturing a flag. Players must be near the flag
   * to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_capture_reward_exp_formula, "Domination", "CaptureRewardEXPFormula", "1");

  /* cfghelp: Domination:RegionCaptureHSDFormula, arena, string, def: 1
   * Formula to use to determine the HSD reward for capturing a region. Players must be in the
   * region to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_region_capture_hsd_formula, "Domination", "RegionCaptureHSDFormula", "1");

  /* cfghelp: Domination:RegionCaptureEXPFormula, arena, string, def: 1
   * Formula to use to determine the EXP reward for capturing a region. Players must be in the
   * region to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_region_capture_exp_formula, "Domination", "RegionCaptureEXPFormula", "1");

  /* cfghelp: Domination:DefenseRewardHSDFormula, arena, string, def: 1
   * Formula to use to determine the HSD reward for defending controlled flags. Players must be near
   * a controlled flag to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_defense_reward_hsd_formula, "Domination", "DefenseRewardHSDFormula", "1");

  /* cfghelp: Domination:DefenseRewardEXPFormula, arena, string, def: 1
   * Formula to use to determine the EXP reward for defending controlled flags. Players must be near
   * a controlled flag to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_defense_reward_exp_formula, "Domination", "DefenseRewardEXPFormula", "1");

  /* cfghelp: Domination:DominationWinRewardHSDFormula, arena, string, def: 1
   * Formula used to determine the final HSD reward for a domination victory. Players must be on the
   * winning team to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_domination_win_reward_hsd_formula, "Domination", "DominationWinRewardHSDFormula", "1");

  /* cfghelp: Domination:DominationWinRewardEXPFormula, arena, string, def: 1
   * Formula used to determine the final EXP reward for a domination victory. Players must be on the
   * winning team to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_domination_win_reward_exp_formula, "Domination", "DominationWinRewardEXPFormula", "1");

  /* cfghelp: Domination:DominationLossRewardHSDFormula, arena, string, def: 1
   * Formula used to determine the final HSD reward for a domination loss. Players must be on the
   * losing team to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_domination_loss_reward_hsd_formula, "Domination", "DominationLossRewardHSDFormula", "1");

  /* cfghelp: Domination:DominationLossRewardEXPFormula, arena, string, def: 1
   * Formula used to determine the final EXP reward for a domination loss. Players must be on the
   * losing team to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_domination_loss_reward_exp_formula, "Domination", "DominationLossRewardEXPFormula", "1");

  /* cfghelp: Domination:DominationTieRewardHSDFormula, arena, string, def: 1
   * Formula used to determine the final HSD reward for a domination tie. Players must be on one of
   * the teams tied for the lead to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_domination_tie_reward_hsd_formula, "Domination", "DominationTieRewardHSDFormula", "1");

  /* cfghelp: Domination:DominationTieRewardEXPFormula, arena, string, def: 1
   * Formula used to determine the final EXP reward for a domination tie. Players must be on one of
   * the teams tied for the lead to receive this reward. */
  DOM_PARSE_CONFIG_FORMULA(adata->cfg_domination_tie_reward_exp_formula, "Domination", "DominationTieRewardEXPFormula", "1");

#undef DOM_PARSE_CONFIG_FORMULA

  lm->Log(L_ERROR, "MADE IT HERE");

  adata->active = 1;
}

static void AwardPlayer(Player *player, int hsd_reward, int exp_reward, MoneyType reward_type, char *reason_msg) {
  if (hsd_reward > 0 && exp_reward > 0) {
    database->addExp(player, exp_reward);
    database->addMoney(player, reward_type, hsd_reward);

    chat->SendMessage(player, "You received $%d and %d exp for %s", hsd_reward, exp_reward, reason_msg);
  }
  else if (hsd_reward > 0) {
    database->addMoney(player, reward_type, hsd_reward);

    chat->SendMessage(player, "You received $%d for %s", hsd_reward, reason_msg);
  }
  else if (exp_reward > 0) {
    database->addExp(player, exp_reward);

    chat->SendMessage(player, "You received %d exp for %s", exp_reward, reason_msg);
  }
}

static void ResetArenaData(Arena *arena) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  LinkedList list = LL_INITIALIZER;
  Link *link;


  // Reset stored data
  if (adata->regions_initialized) {
    HashDeinit(&adata->region_owners);
  }

  if (adata->flags_initialized) {
    HashDeinit(&adata->flag_owners);
  }

  HashInit(&adata->region_owners);
  adata->regions_initialized = 1;

  HashInit(&adata->flag_owners);
  adata->flags_initialized = 1;



  // Clear all flag timers
  LLEmpty(&list);
  if (dom->GetDomFlags(arena, &list) > 0) {
    DomFlag *dflag;
    FOR_EACH(&list, dflag, link) {
      // Add timers here
      mainloop->ClearTimer(OnFlagCaptureRewardTick, dflag);
    }
  }

  // TODO: Add team timer clearing block here
  // TODO: Add region timer clearing block here

  LLEmpty(&list);
}

static void OnArenaAttach(Arena *arena) {
  // Initialize arena data...
  InitArenaConfig(arena);

  // Read arena config...
  ReadArenaConfig(arena);
}

static void OnArenaDetach(Arena *arena) {
  // Free arena data
  ResetArenaData(arena);
}

static void OnArenaAction(Arena *arena, int action) {
  if (action == AA_CONFCHANGED) {
    ReadArenaConfig(arena);
  }
}

static void OnDomGameStateChanged(Arena *arena, DomGameState prev_state, DomGameState new_state) {

  DomTeam *dteam;

  switch (new_state) {
    case DOM_GAME_STATE_FINISHED:
      // If the game didn't end in a domination, reward teams based on territory control
      dteam = dom->GetDominatingTeam(arena);

      if (dteam) {
        // Dominated by team. We have one winner and many losers

      }
      else {
        // No one dominated, we may have a tie here (groan...)

      }

      break;

    case DOM_GAME_STATE_UNKNOWN:
    case DOM_GAME_STATE_INACTIVE:
    case DOM_GAME_STATE_COOLDOWN:
    case DOM_GAME_STATE_ERROR:
      ResetArenaData(arena);

    default:;
  }

}

static void OnDomRegionStateChanged(Arena *arena, DomRegion *dregion, DomRegionState prev_state, DomRegionState new_state) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  // If region fully changes hands, it's a capture bonus
  // If a region doesn't change hands, it's a defense bonus. A defense should only happen if there's
  // actual competition. Losing the state for a few seconds isn't reasonable here. Probably changing
  // hands and then being taken back.

  if (dom->GetGameState(arena) == DOM_GAME_STATE_ACTIVE) {
    if (new_state != DOM_REGION_STATE_CONTROLLED && prev_state == DOM_REGION_STATE_CONTROLLED) {
      DomTeam *dteam = dom->GetRegionControllingTeam(dregion);
      char *key = dom->GetRegionKey(dregion);

      if (adata->regions_initialized && dteam && key) {
        HashReplace(&adata->region_owners, key, dteam);
      }
    }
    else if (new_state == DOM_REGION_STATE_CONTROLLED) {
      // Lookup previous owner and check if the region changed hands
      DomTeam *dteam = dom->GetRegionControllingTeam(dregion);
      char *key = dom->GetRegionKey(dregion);

      if (adata->regions_initialized && dteam && key) {
        DomTeam *pteam = HashRemoveAny(&adata->region_owners, key);

        if (pteam == dteam) {
          // Region defended
          chat->SendArenaMessage(arena, "Region defended by controlling team!");
        }
        else {
          // Region taken by a new team
          chat->SendArenaMessage(arena, "Region taken by a new team!");
        }
      }
    }
  }
  else {
    chat->SendArenaMessage(arena, "OnDomRegionStateChanged: Inactive or not active game: %d, %d", adata->active, dom->GetGameState(arena));
  }
}

static void OnDomFlagStateChanged(Arena *arena, DomFlag *dflag, DomFlagState prev_state, DomFlagState new_state) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  DomTeam *dteam;
  Player *player;
  Link *link;
  char *key;

  char ebuffer[1024];
  double hsd_reward, exp_reward;

  if (adata->active && dom->GetGameState(arena) == DOM_GAME_STATE_ACTIVE) {
    // Clear flag reward tick timer
    mainloop->ClearTimer(OnFlagCaptureRewardTick, dflag);

    if (new_state != DOM_FLAG_STATE_CONTROLLED && prev_state == DOM_FLAG_STATE_CONTROLLED) {
      dteam = dom->GetFlagControllingTeam(dflag);
      key = dom->GetFlagKey(dflag);

      if (adata->flags_initialized && dteam && key) {
        HashReplace(&adata->flag_owners, key, dteam);
      }
    }

    HashTable vars;
    FormulaVariable flag_var, player_var, arena_var;

    FlagInfo flaginfo;
    dom->GetFlagInfo(dflag, &flaginfo);

    HashInit(&vars);

    flag_var.name = NULL;
    flag_var.type = VAR_TYPE_FLAG;
    flag_var.flag = &flaginfo;

    player_var.name = NULL;
    player_var.type = VAR_TYPE_PLAYER;

    arena_var.name = NULL;
    arena_var.type = VAR_TYPE_ARENA;
    arena_var.arena = arena;

    HashReplace(&vars, "flag", &flag_var);
    HashReplace(&vars, "player", &player_var);
    HashReplace(&vars, "arena", &arena_var);


    switch (new_state) {
      case DOM_FLAG_STATE_CONTESTED:
        // Figure out who actually touched the flag. Might be a bit difficult...
        player = dom->GetLastFlagToucher(dflag);
        if (player) {
          player_var.player = player;

          DOM_EVALUATE_FORMULA(hsd_reward, adata, adata->cfg_contest_reward_hsd_formula, &vars, ebuffer, "Domination", "ContestRewardHSDFormula");
          DOM_EVALUATE_FORMULA(exp_reward, adata, adata->cfg_contest_reward_exp_formula, &vars, ebuffer, "Domination", "ContestRewardEXPFormula");

          AwardPlayer(player, hsd_reward, exp_reward, MONEY_TYPE_FLAG, "contesting a flag");
        }
        break;

      case DOM_FLAG_STATE_CAPTURING:
        if (adata->cfg_capture_tick_delay > 0) {
          mainloop->SetTimer(OnFlagCaptureRewardTick, 0, adata->cfg_capture_tick_delay, dflag, dflag);
        }
        else {
          chat->SendArenaMessage(arena, "Capture tick delay is zero or negative");
        }
        break;

      case DOM_FLAG_STATE_CONTROLLED:
        // Lookup previous owner and check if the flag changed hands
        dteam = dom->GetFlagControllingTeam(dflag);
        key = dom->GetFlagKey(dflag);

        if (adata->flags_initialized && dteam && key) {
          DomTeam *pteam = HashRemoveAny(&adata->flag_owners, key);

          if (pteam == dteam) {
            // Flag defended
            chat->SendArenaMessage(arena, "Flag defended by controlling team!");
          }
          else {
            // Flag taken by a new team
            int team_freq = dom->GetTeamFreq(dteam);

            pd->Lock();
            FOR_EACH_PLAYER_IN_ARENA(player, arena) {
              if (player->p_freq != team_freq || player->p_ship == SHIP_SPEC) {
                continue;
              }

              int dx = (player->position.x - flaginfo.x);
              int dy = (player->position.y - flaginfo.y);
              int dsq = dx * dx + dy * dy;

              if (dsq <= adata->cfg_flag_reward_radius) {
                player_var.player = player;

                DOM_EVALUATE_FORMULA(hsd_reward, adata, adata->cfg_capture_reward_hsd_formula, &vars, ebuffer, "Domination", "CaptureRewardHSDFormula");
                DOM_EVALUATE_FORMULA(exp_reward, adata, adata->cfg_capture_reward_exp_formula, &vars, ebuffer, "Domination", "CaptureRewardEXPFormula");

                AwardPlayer(player, hsd_reward, exp_reward, MONEY_TYPE_FLAG, "flag control contribution");
              }
            }
            pd->Unlock();
          }
        }
        break;

      default:;
    }

    HashDeinit(&vars);
  }
  else {
    chat->SendArenaMessage(arena, "OnDomFlagStateChanged: Inactive or not active game: %d, %d", adata->active, dom->GetGameState(arena));
  }
}

static int OnFlagCaptureRewardTick(void *param) {
  DomFlag *dflag = (DomFlag*) param;
  Arena *arena = dom->GetFlagArena(dflag);
  DomTeam *dteam = dom->GetFlagEntityControllingTeam(dflag);

  if (dflag && arena && dteam) {
    ArenaData *adata = P_ARENA_DATA(arena, adkey);

    if (adata->active && adata->cfg_flag_reward_radius > 0 && dom->GetGameState(arena) == DOM_GAME_STATE_ACTIVE) {
      FlagInfo flaginfo;
      Player *player;
      Link *link;

      char ebuffer[1024];
      double hsd_reward, exp_reward;

      int team_freq = dom->GetTeamFreq(dteam);

      // Setup formula variables...
      HashTable vars;
      FormulaVariable flag_var, player_var, arena_var;

      HashInit(&vars);
      dom->GetFlagInfo(dflag, &flaginfo);

      flag_var.name = NULL;
      flag_var.type = VAR_TYPE_FLAG;
      flag_var.flag = &flaginfo;

      player_var.name = NULL;
      player_var.type = VAR_TYPE_PLAYER;

      arena_var.name = NULL;
      arena_var.type = VAR_TYPE_ARENA;
      arena_var.arena = arena;

      HashReplace(&vars, "flag", &flag_var);
      HashReplace(&vars, "player", &player_var);
      HashReplace(&vars, "arena", &arena_var);

      pd->Lock();
      FOR_EACH_PLAYER_IN_ARENA(player, arena) {
        if (player->p_freq != team_freq || player->p_ship == SHIP_SPEC) {
          continue;
        }

        int dx = (player->position.x - flaginfo.x);
        int dy = (player->position.y - flaginfo.y);
        int dsq = dx * dx + dy * dy;

        if (dsq <= adata->cfg_flag_reward_radius) {
          player_var.player = player;

          DOM_EVALUATE_FORMULA_TICK(hsd_reward, adata, adata->cfg_capture_tick_reward_hsd_formula, &vars, ebuffer, "Domination", "CaptureTickRewardHSDFormula");
          DOM_EVALUATE_FORMULA_TICK(exp_reward, adata, adata->cfg_capture_tick_reward_exp_formula, &vars, ebuffer, "Domination", "CaptureTickRewardEXPFormula");

          AwardPlayer(player, hsd_reward, exp_reward, MONEY_TYPE_FLAG, "flag capture contribution");
        }
      }
      pd->Unlock();

      HashDeinit(&vars);
      return 1;
    }
  }

  return 0;
}

static void OnDomAlert(Arena *arena, DomAlertType type, DomAlertState state, ticks_t duration) {
  // Nothing special to do here, currently.
}

static void OnDomFlagDefense(Arena *arena, DomFlag *dflag, LinkedList *players) {
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  HashTable vars;
  Player *player;
  FlagInfo flaginfo;
  Link *link;

  char ebuffer[1024];
  double hsd_reward, exp_reward;

  FormulaVariable flag_var, player_var, arena_var;


  if (adata->active && dom->GetGameState(arena) == DOM_GAME_STATE_ACTIVE) {
    // Setup formula variables...
    HashInit(&vars);
    dom->GetFlagInfo(dflag, &flaginfo);

    flag_var.name = NULL;
    flag_var.type = VAR_TYPE_FLAG;
    flag_var.flag = &flaginfo;

    player_var.name = NULL;
    player_var.type = VAR_TYPE_PLAYER;

    arena_var.name = NULL;
    arena_var.type = VAR_TYPE_ARENA;
    arena_var.arena = arena;

    HashReplace(&vars, "flag", &flag_var);
    HashReplace(&vars, "player", &player_var);
    HashReplace(&vars, "arena", &arena_var);

    FOR_EACH(players, player, link) {
      player_var.player = player;

      DOM_EVALUATE_FORMULA(hsd_reward, adata, adata->cfg_defense_reward_hsd_formula, &vars, ebuffer, "Domination", "DefenseRewardHSDFormula");
      DOM_EVALUATE_FORMULA(exp_reward, adata, adata->cfg_defense_reward_exp_formula, &vars, ebuffer, "Domination", "DefenseRewardEXPFormula");

      AwardPlayer(player, hsd_reward, exp_reward, MONEY_TYPE_FLAG, "flag defense");
    }

    HashDeinit(&vars);
  }
  else {
    chat->SendArenaMessage(arena, "OnDomFlagDefense: Inactive or not active game: %d, %d", adata->active, dom->GetGameState(arena));
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
    formulas   = mm->GetInterface(I_FORMULA, ALLARENAS);
    items     = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    mainloop  = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);

    return mm &&
      (arenaman && cfg && chat && database && dom && formulas && items && lm && mainloop && pd);
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
    mm->ReleaseInterface(formulas);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(mainloop);
    mm->ReleaseInterface(pd);

    mm = NULL;
  }
}


EXPORT const char info_hs_domination_rewards[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hs_domination_rewards(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", DOM_REWARDS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate object data
      adkey = arenaman->AllocateArenaData(sizeof(ArenaData));
      if (adkey == -1) {
        printf("<%s> Unable to allocate per-arena data.\n", DOM_REWARDS_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-arena data.", DOM_REWARDS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // pdkey = pd->AllocatePlayerData(sizeof(PlayerGameData));
      // if (pdkey == -1) {
      //   printf("<%s> Unable to allocate per-player data.\n", DOM_REWARDS_MODULE_NAME);
      //   lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", DOM_REWARDS_MODULE_NAME);
      //   ReleaseInterfaces();
      //   break;
      // }
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      mm->RegCallback(CB_ARENAACTION, OnArenaAction, arena);

      mm->RegCallback(CB_DOM_GAME_STATE_CHANGED, OnDomGameStateChanged, arena);
      mm->RegCallback(CB_DOM_REGION_STATE_CHANGED, OnDomRegionStateChanged, arena);
      mm->RegCallback(CB_DOM_FLAG_STATE_CHANGED, OnDomFlagStateChanged, arena);
      mm->RegCallback(CB_DOM_ALERT, OnDomAlert, arena);
      mm->RegCallback(CB_DOM_FLAG_DEFENSE, OnDomFlagDefense, arena);

      OnArenaAttach(arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      OnArenaDetach(arena);

      mm->RegCallback(CB_DOM_FLAG_DEFENSE, OnDomFlagDefense, arena);
      mm->RegCallback(CB_DOM_ALERT, OnDomAlert, arena);
      mm->RegCallback(CB_DOM_FLAG_STATE_CHANGED, OnDomFlagStateChanged, arena);
      mm->RegCallback(CB_DOM_REGION_STATE_CHANGED, OnDomRegionStateChanged, arena);
      mm->RegCallback(CB_DOM_GAME_STATE_CHANGED, OnDomGameStateChanged, arena);

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

#undef DOM_EVALUATE_FORMULA

