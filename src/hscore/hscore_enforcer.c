#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner2.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Iconfig *cfg;
local Ihscoredatabase *database;

////////////////////////////////////////////////////////////////////////////////////////////////////

local int CanChangeToShip(Player *p, int new_ship, int is_changing, char *err_buf, int buf_len)
{
  int allowed = 0;

  if (new_ship != SHIP_SPEC) {
    if (database->areShipsLoaded(p)) {
      IHSCoreSpawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER2, p->arena);
      int shipset;

      if (spawner) {
        shipset = spawner->hasPendingShipSetChange(p) ? spawner->getPendingShipSet(p) : database->getPlayerShipSet(p);
        mm->ReleaseInterface(spawner);
      } else {
        shipset = database->getPlayerShipSet(p);
      }

      int ship_cost = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[new_ship], "BuyPrice", 0);
      int ship_exp = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[new_ship], "ExpRequired", 0);
      ShipHull *hull = database->getPlayerHull(p, new_ship, shipset);

      if (!hull && !ship_cost) {
        // The ship is free, but the player hasn't used it before. We need to initialize it here.
        // Probably safe to assume we need to delay the player here as well or they may crash out if they don't have good settings...

        if (ship_exp < 1 || database->getExp(p) >= ship_exp) {
          lm->LogP(L_DRIVEL, "hscore_enforcer", p, "Player joining the game in an unowned, free ship. Granting ship to player and assigning them to one later.");
          database->addShipToShipSet(p, new_ship, shipset);

          allowed = 1;
        } else {
          if (err_buf)
            snprintf(err_buf, buf_len, "You do not have enough experience to use the %s.", cfg->SHIP_NAMES[new_ship]);
        }
      } else if (hull || !ship_cost) {
        lm->LogP(L_DRIVEL, "hscore_enforcer", p, "Player owns ship %s.", cfg->SHIP_NAMES[new_ship]);
        allowed = 1;
      } else {
        if (err_buf)
          snprintf(err_buf, buf_len, "You do not own a %s hull on shipset %i. Please use \"?buy ships\" to examine the ship hulls for sale.", cfg->SHIP_NAMES[new_ship], shipset + 1);
      }
    } else {
      if (err_buf)
        snprintf(err_buf, buf_len, "Your ship data is not loaded in this arena. If you just entered, please wait a moment and try again.");
    }
  } else {
    allowed = 1;
  }

  lm->LogP(L_DRIVEL, "hscore_enforcer", p, "Ship change allowed: %d", allowed);

  return allowed;
}

local shipmask_t GetAllowableShips(Player *p, int freq, char *err_buf, int buf_len)
{
  shipmask_t mask = 0;

  // New freq; build a proper shipmask.
  if (database->areShipsLoaded(p)) {
    IHSCoreSpawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER2, p->arena);
    int shipset;

    if (spawner) {
      shipset = spawner->hasPendingShipSetChange(p) ? spawner->getPendingShipSet(p) : database->getPlayerShipSet(p);
      mm->ReleaseInterface(spawner);
    } else {
      shipset = database->getPlayerShipSet(p);
    }

    for (int i = SHIP_WARBIRD; i < SHIP_SPEC; ++i) {
      if (database->getPlayerHull(p, i, shipset) || !cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[i], "BuyPrice", 0)) {
        mask |= (1 << i);
      }
    }

    if (err_buf && p->p_ship != SHIP_SPEC && !(mask & (1 << p->p_ship))) {
      snprintf(err_buf, buf_len, "You do not own a %s hull on shipset %i. Please use \"?buy ships\" to examine the ship hulls for sale.", cfg->SHIP_NAMES[p->p_ship], shipset + 1);
    }
  } else {
    if (err_buf)
      snprintf(err_buf, buf_len, "Your ship data is not loaded in this arena. If you just entered, please wait a moment and try again.");
  }

  return mask;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

local Aenforcer hscore_enforcer =
{
  ADVISER_HEAD_INIT(A_ENFORCER)
  NULL,
  NULL,
  CanChangeToShip,
  NULL,
  GetAllowableShips
};

EXPORT const char info_hscore_enforcer[] = "v2.0 Ceiu <ceiu@cericlabs.com>";


static int getInterfaces() {
  lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
  cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
  database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

  return (lm && cfg && database);
}

static void releaseInterfaces() {
  mm->ReleaseInterface(database);
  mm->ReleaseInterface(cfg);
  mm->ReleaseInterface(lm);
}

EXPORT int MM_hscore_enforcer(int action, Imodman *_mm, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      mm = _mm;

      if (!getInterfaces()) {
        releaseInterfaces();
        return MM_FAIL;
      }

      return MM_OK;


    case MM_ATTACH:
      mm->RegAdviser(&hscore_enforcer, arena);
      return MM_OK;


    case MM_DETACH:
      mm->UnregAdviser(&hscore_enforcer, arena);
      return MM_OK;


    case MM_UNLOAD:
      releaseInterfaces();
      return MM_OK;
  }

  return MM_FAIL;
}

