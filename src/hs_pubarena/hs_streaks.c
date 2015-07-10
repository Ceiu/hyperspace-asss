/**
 * hs_streaks.c
 * Implements simple kill streaks with rewards.
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

#define HS_MODULE_NAME "hs_streaks"

#define HS_MIN(x, y) ((x) < (y) ? (x) : (y))
#define HS_MAX(x, y) ((x) > (y) ? (x) : (y))
#define HS_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

/**
 * Player streak data
 */
typedef struct {

  int     kill_streak;
  ticks_t streak_start;
  int     streak_level;

  ticks_t last_kill;
  int     quick_kill_count;

} PlayerStreakData;


// Interfaces
static Imodman *mm;

static Ilogman *lm;
static Iconfig *cfg;
static Iplayerdata *pd;
static Ichat *chat;

static Ihscoredatabase *database;

// Global resource identifiers
static int pdkey;

// Pseudo constants which could be replaced by configuration stuff eventually.
static int cfg_doublekill_delay = 300;
static int cfg_kills_per_level = 5;
static int cfg_cash_per_level = 0;
static int cfg_exp_per_level = 0;

static int cfg_cash_per_quickkill = 0;
static int cfg_exp_per_quickkill = 0;


////////////////////////////////////////////////////////////////////////////////////////////////////

static void ResetStreakData(Player *player) {
  PlayerStreakData *pdata = PPDATA(player, pdkey);

  pdata->kill_streak = 0;
  pdata->streak_start = 0;
  pdata->streak_level = 0;
  pdata->last_kill = 0;
  pdata->quick_kill_count = 1;
}

static int GetStreakLevel(int kills)
{
  return kills / cfg_kills_per_level;
}

static void AnnounceStreak(Player *player, int kills)
{
  int level = GetStreakLevel(kills);

  switch (level) {
    case 1:
      chat->SendArenaMessage(player->arena, "%s is on a killing spree (%d-0)!", player->name, kills);
      break;

    case 2:
      chat->SendArenaMessage(player->arena, "%s is on a rampage (%d-0)!", player->name, kills);
      break;

    case 3:
      chat->SendArenaMessage(player->arena, "%s is dominating (%d-0)!", player->name, kills);
      break;

    case 4:
      chat->SendArenaMessage(player->arena, "%s is unstoppable (%d-0)!", player->name, kills);
      break;

    default:
      if (level <= 0)
        break;

    case 5:
      chat->SendArenaMessage(player->arena, "%s is godlike (%d-0)!", player->name, kills);
      break;
  }
}

static void AnnounceQuickKill(Player *player, int kills)
{
  switch (kills) {
    case 2:
      chat->SendArenaMessage(player->arena, "%s: DOUBLE KILL!", player->name);
      break;

    case 3:
      chat->SendArenaMessage(player->arena, "%s: MULTI KILL!", player->name);
      break;

    case 4:
      chat->SendArenaMessage(player->arena, "%s: ULTRA KILL!", player->name);
      break;

    default:
      if (kills <= 1)
        break;

    case 5:
      chat->SendArenaMessage(player->arena, "%s: MONSTER KILL!", player->name);
      break;
  }
}

static void AnnounceStreakWithReward(Player *player, int kills, int money, int exp)
{
  int level = GetStreakLevel(kills);

  switch (level) {
    case 1:
      chat->SendArenaMessage(player->arena, "%s is on a killing spree (%d-0)!  Reward: $%d, %d exp", player->name, kills, money, exp);
      break;

    case 2:
      chat->SendArenaMessage(player->arena, "%s is on a rampage (%d-0)!  Reward: $%d, %d exp", player->name, kills, money, exp);
      break;

    case 3:
      chat->SendArenaMessage(player->arena, "%s is dominating (%d-0)!  Reward: $%d, %d exp", player->name, kills, money, exp);
      break;

    case 4:
      chat->SendArenaMessage(player->arena, "%s is unstoppable (%d-0)!  Reward: $%d, %d exp", player->name, kills, money, exp);
      break;

    default:
      if (level <= 0)
        break;

    case 5:
      chat->SendArenaMessage(player->arena, "%s is godlike (%d-0)!  Reward: $%d, %d exp", player->name, kills, money, exp);
      break;
  }
}

static void AnnounceQuickKillWithReward(Player *player, int kills, int money, int exp)
{
  switch (kills) {
    case 2:
      chat->SendArenaMessage(player->arena, "%s: DOUBLE KILL!  Reward: $%d, %d exp", player->name, money, exp);
      break;

    case 3:
      chat->SendArenaMessage(player->arena, "%s: MULTI KILL!  Reward: $%d, %d exp", player->name, money, exp);
      break;

    case 4:
      chat->SendArenaMessage(player->arena, "%s: ULTRA KILL!  Reward: $%d, %d exp", player->name, money, exp);
      break;

    default:
      if (kills <= 1)
        break;

    case 5:
      chat->SendArenaMessage(player->arena, "%s: MONSTER KILL!  Reward: $%d, %d exp", player->name, money, exp);
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////

static void OnPlayerAction(Player *player, int action, Arena *arena)
{
  switch (action) {
    case PA_ENTERARENA:
      ResetStreakData(player);
      break;

    case PA_LEAVEARENA:
      break;
  }
}

static void OnPlayerDeath(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
  PlayerStreakData *kdata = PPDATA(killer, pdkey);
  PlayerStreakData *ddata = PPDATA(killed, pdkey);

  ticks_t now = current_ticks();
  ticks_t diff;
  int level, min, sec, money, exp;

  if (killer->p_freq != killed->p_freq) {
    // Do death stuff...
    if (ddata->streak_level > 0) {
      ticks_t diff = TICK_DIFF(now, ddata->streak_start);
      min = diff / 6000;
      sec = (diff % 6000) / 100;
      money = HS_MAX(0, ddata->streak_level * cfg_cash_per_level);
      exp = HS_MAX(0, ddata->streak_level * cfg_exp_per_level);

      if (money > 0 || exp > 0) {
        chat->SendArenaMessage(arena, "%s's killing spree was ended after %d'%d\" by %s. Reward: $%d, %d exp", killed->name, min, sec, killer->name, money, exp);
        database->addMoney(killer, MONEY_TYPE_EVENT, money);
        database->addExp(killer, exp);
      } else {
        chat->SendArenaMessage(arena, "%s's killing spree was ended after %d'%d\" by %s.", killed->name, min, sec, killer->name);
      }
    }


    // Do kill stuff...
    if (++kdata->kill_streak == 1) {
      kdata->streak_start = current_ticks();
    }

    level = GetStreakLevel(kdata->kill_streak);
    if (level > kdata->streak_level) {
      kdata->streak_level = level;

      money = HS_MAX(0, kdata->streak_level * cfg_cash_per_level);
      exp = HS_MAX(0, kdata->streak_level * cfg_exp_per_level);

      if (money > 0 || exp > 0) {
        AnnounceStreakWithReward(killer, kdata->kill_streak, money, exp);
        database->addMoney(killer, MONEY_TYPE_EVENT, money);
        database->addExp(killer, exp);
      } else {
        AnnounceStreak(killer, kdata->kill_streak);
      }
    }

    // Check for quick kills.
    if (kdata->last_kill) {
      diff = TICK_DIFF(now, kdata->last_kill);

      if (diff <= cfg_doublekill_delay) {
        ++kdata->quick_kill_count;

        money = HS_MAX(0, kdata->quick_kill_count * cfg_cash_per_quickkill);
        exp = HS_MAX(0, kdata->quick_kill_count * cfg_exp_per_quickkill);

        if (money > 0 || exp > 0) {
          AnnounceQuickKillWithReward(killer, kdata->quick_kill_count, money, exp);
          database->addMoney(killer, MONEY_TYPE_EVENT, money);
          database->addExp(killer, exp);
        } else {
          AnnounceQuickKill(killer, kdata->quick_kill_count);
        }
      } else {
        kdata->quick_kill_count = 1;
      }
    }

    kdata->last_kill = now;
  } else {
    if (ddata->streak_level > 0) {
      ticks_t diff = TICK_DIFF(now, ddata->streak_start);
      min = diff / 6000;
      sec = (diff % 6000) / 100;

      chat->SendArenaMessage(arena, "%s's killing spree was ended after %d'%d\" by %s (TEAMKILL).", killed->name, min, sec, killer->name);
    }
  }

  // We probably don't need to do this with the spawn handler, but it prevents post-mortem kill
  // bonuses.
  ResetStreakData(killed);
}

static void OnPlayerSpawn(Player *player, int reason)
{
  ResetStreakData(player);
}

static void OnPlayerFreqShipChange(Player *player, int newship, int oldship, int newfreq, int oldfreq)
{
  ResetStreakData(player);
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

    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    cfg       = mm->GetInterface(I_CONFIG, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    chat      = mm->GetInterface(I_CHAT, ALLARENAS);

    database  = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

    return mm && (lm && cfg && pd && chat) && (database);
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
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(cfg);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(chat);

    mm->ReleaseInterface(database);

    mm = NULL;
  }
}


EXPORT const char info_hs_streaks[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hs_streaks(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", HS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate pdata
      pdkey = pd->AllocatePlayerData(sizeof(PlayerStreakData));
      if (pdkey == -1) {
        printf("<%s> Unable to allocate per-player data.\n", HS_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", HS_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      mm->RegCallback(CB_PLAYERACTION, OnPlayerAction, arena);
      mm->RegCallback(CB_KILL, OnPlayerDeath, arena);
      mm->RegCallback(CB_SPAWN, OnPlayerSpawn, arena);
      mm->RegCallback(CB_SHIPFREQCHANGE, OnPlayerFreqShipChange, arena);
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      mm->UnregCallback(CB_PLAYERACTION, OnPlayerAction, arena);
      mm->UnregCallback(CB_KILL, OnPlayerDeath, arena);
      mm->UnregCallback(CB_SPAWN, OnPlayerSpawn, arena);
      mm->UnregCallback(CB_SHIPFREQCHANGE, OnPlayerFreqShipChange, arena);
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_UNLOAD:
      pd->FreePlayerData(pdkey);

      ReleaseInterfaces();
      return MM_OK;
  }

  return MM_FAIL;
}
