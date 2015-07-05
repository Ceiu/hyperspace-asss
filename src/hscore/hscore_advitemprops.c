/**
 * hscore_advitemprops.c
 * Implements the old item system with spawner2. Uses code copied from hscore_spawner.c liberally.
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
#include "hscore_spawner2.h"


////////////////////////////////////////////////////////////////////////////////////////////////////
// Global Definitions
////////////////////////////////////////////////////////////////////////////////////////////////////

#define HSC_MODULE_NAME "hscore_advitemprops"

#define HSC_MIN(x, y) ((x) < (y) ? (x) : (y))
#define HSC_MAX(x, y) ((x) > (y) ? (x) : (y))
#define HSC_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

/**
 * Absolute maximum length of types. Comes from the ItemType declaration in hscore_types.h. Update
 * as necessary.
 */
#define HSC_TYPE_LENGTH 32

/**
 * Absolute maximum length of property names. Comes from the Property declaration in hscore_types.h.
 * Update as necessary.
 */
#define HSC_PROP_LENGTH 32


/**
 * Callback used with the CalculatePropertyTotal function.
 *
 * @param *item
 *  The item on which the property was found.
 *
 * @param count
 *  The number of the given item on the searched hull.
 *
 * @param *property
 *  The matching property.
 *
 * @param running_total
 *  The running total for the property.
 *
 * @param *exdata
 *  Extra data provided to the calculate function.
 *
 * @return
 *  The new running total.
 */
typedef int (*CombinatorCallbackFunction)(Item *item, int count, Property *property, int running_total, void *exdata);

/**
 * Player data -- stores abstracted stat information.
 */
typedef struct {
  int   modify_packets;

  int   mass;
  int   capacity;
  int   volume;

  int   energy;
  int   recharge;
  int   boost_power;
  int   hiboost_power;
  int   hiboost_usage_drain;
  int   overboost_power;
  int   rotation;

  char  gun_level;
  int   gun_usage_drain;
  char  gun_crit_rate;
  char  gun_crit_boost;

  char  bomb_level;
  int   bomb_usage_drain;
  char  bomb_crit_rate;
  char  bomb_crit_boost;
  char  bomb_shrap_count;
  char  bomb_shrap_level;
  char  bomb_shrap_bounce;
  char  bomb_shrap_crit_rate;
  char  bomb_shrap_crit_level_boost;
  char  bomb_shrap_crit_count_boost;

  char  mine_level;
  int   mine_usage_drain;
  char  mine_crit_rate;
  char  mine_crit_boost;
  char  mine_shrap_count;
  char  mine_shrap_level;
  char  mine_shrap_bounce;
  char  mine_shrap_crit_rate;
  char  mine_shrap_crit_level_boost;
  char  mine_shrap_crit_count_boost;

  char  stealth_available;
  int   stealth_usage_drain;
  char  cloak_available;
  int   cloak_usage_drain;
  char  xradar_available;
  int   xradar_usage_drain;
  char  antiwarp_available;
  int   antiwarp_usage_drain;
} PlayerStatsData;

/**
 * Used to calculate the total where the item type determines a base value. Should only be used for
 * item types which have a max of one.
 */
typedef struct {
  char *item_type;
  int item_type_len;

  int base;
  int augment;
  int absolute;
} TypeBasedPropertyTotal;

/**
 * Used to quickly zero out a memory block.
 */
struct Weapons NullWeapon = {0};




// Interfaces
static Imodman *mm;
static Ilogman *lm;
static Iconfig *cfg;
static Iplayerdata *pd;
static Iprng *prng;

static Ihscoreitems *items;
static Ihscoredatabase *database;
static IHSCoreSpawner *spawner;

// Global resource identifiers
static int pdkey;


////////////////////////////////////////////////////////////////////////////////////////////////////
// Prototypes
////////////////////////////////////////////////////////////////////////////////////////////////////

// Adviser functions
static void EditPlayerPositionPacket(Player *player, struct C2SPosition *pos);

// Utility functions
static int CalculatePropertyTotal(ShipHull *hull, char *property, int def_value);

// Callbacks
static int GetPrizeCount(Player *player, ShipHull *hull, int freq, int ship, int prize, int init_count, void *exdata);

// Module initialization
static int SetupArena(Arena *arena);
static void TearDownArena(Arena *arena);
static int GetInterfaces(Imodman *modman, Arena *arena);
static void ReleaseInterfaces();

////////////////////////////////////////////////////////////////////////////////////////////////////
// Advisers/Interfaces
////////////////////////////////////////////////////////////////////////////////////////////////////

static Appk PlayerPacketAdviser = {
  ADVISER_HEAD_INIT(A_PPK)
  EditPlayerPositionPacket, NULL
};

/**
 * Called on every player position packet. Can be used to modify the packet before the server
 * processes it. Modifies weapons accordingly.
 *
 * @param *player
 *  The player sending the packet.
 *
 * @param *pos
 *  The position/weapon packet sent by the player.
 */
static void EditPlayerPositionPacket(Player *player, struct C2SPosition *pos)
{
  if (!player || !pos) {
    return;
  }

  PlayerStatsData *pdata = PPDATA(player, pdkey);

  if (pdata->modify_packets) {
    int count;
    int level;

    switch (pos->weapon.type) {
      case W_BULLET:
      case W_BOUNCEBULLET:
        level = pdata->gun_level;

        if (pdata->gun_crit_rate > 0 && pdata->gun_crit_boost) {
          if (pdata->gun_crit_rate >= prng->Number(0, 100)) {
            level += pdata->gun_crit_boost;
          }
        }

        if (level > 0) {
          pos->weapon.level = HSC_MIN(3, level - 1);
        } else {
          pos->weapon = NullWeapon;
        }
        break;

      case W_BOMB:
      case W_PROXBOMB:
        if (!pos->weapon.alternate) {
          // Bombs
          level = pdata->bomb_level;

          if (pdata->bomb_crit_rate > 0 && pdata->bomb_crit_boost) {
            if (pdata->bomb_crit_rate >= prng->Number(0, 100)) {
              level += pdata->bomb_crit_boost;
            }
          }

          if (level > 0) {
            pos->weapon.level = HSC_MIN(3, level - 1);

            // Shrapnel
            count = pdata->bomb_shrap_count;
            level = pdata->bomb_shrap_level;

            if (pdata->bomb_shrap_crit_rate > 0) {
              if (pdata->bomb_shrap_crit_rate >= prng->Number(0, 100)) {
                count += pdata->bomb_shrap_crit_count_boost;
                level += pdata->bomb_shrap_crit_level_boost;
              }
            }

            if (level > 0 && count > 0) {
              pos->weapon.shrapbouncing = !!pdata->bomb_shrap_bounce;
              pos->weapon.shraplevel = HSC_MIN(3, level - 1);
              pos->weapon.shrap = HSC_MIN(32, count);
            } else {
              pos->weapon.shrapbouncing = 0;
              pos->weapon.shraplevel = 0;
              pos->weapon.shrap = 0;
            }
          } else {
            pos->weapon = NullWeapon;
          }
        } else {
          // Mines
          level = pdata->mine_level;

          if (pdata->mine_crit_rate > 0 && pdata->mine_crit_boost) {
            if (pdata->mine_crit_rate >= prng->Number(0, 100)) {
              level += pdata->mine_crit_boost;
            }
          }

          if (level > 0) {
            pos->weapon.level = HSC_MIN(3, level - 1);

            // Shrapnel
            count = pdata->mine_shrap_count;
            level = pdata->mine_shrap_level;

            if (pdata->mine_shrap_crit_rate > 0) {
              if (pdata->mine_shrap_crit_rate >= prng->Number(0, 100)) {
                count += pdata->mine_shrap_crit_count_boost;
                level += pdata->mine_shrap_crit_level_boost;
              }
            }

            if (level > 0 && count > 0) {
              pos->weapon.shrapbouncing = !!pdata->mine_shrap_bounce;
              pos->weapon.shraplevel = HSC_MIN(3, level - 1);
              pos->weapon.shrap = HSC_MIN(32, count);
            } else {
              pos->weapon.shrapbouncing = 0;
              pos->weapon.shraplevel = 0;
              pos->weapon.shrap = 0;
            }
          } else {
            pos->weapon = NullWeapon;
          }
        }

        break;
    }
  }
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Util functions
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Calculates the total value for the given property.
 *
 * @param *hull
 *  The hull to be scanned for items with the specified property.
 *
 * @param *prop_name
 *  The name of the property for which to calculate the totals
 *
 * @param def_value
 *  The value to use if the property was not found on any item.
 *
 * @return
 *  The property total.
 */
static int CalculatePropertyTotal(ShipHull *hull, char *prop_name, int def_value)
{
  if (!hull || !prop_name) {
    return def_value;
  }

  Link *ielink, *plink;

  InventoryEntry *entry;
  Item *item;
  Property *property;

  int found = 0;
  int tvalue = 0;


  database->lock();

  // Process hull properties.
  FOR_EACH(hull->propertyList, property, plink) {
    if (!property)
      continue;

    if (!strcmp(prop_name, property->name)) {
      found = 1;

      if (!property->absolute) {
        tvalue += property->value;
      } else {
        tvalue = property->value;
        goto ct_finished;
      }
    }
  }

  // Process items on the hull.
  FOR_EACH(&hull->inventoryEntryList, entry, ielink) {
    if (!entry || !entry->count || !entry->item) {
      continue;
    }

    item = entry->item;

    // Check the item for matching property/ies.
    FOR_EACH(&item->propertyList, property, plink) {
      if (!property)
        continue;

      if (!strcmp(prop_name, property->name)) {
        found = 1;

        if (!property->absolute) {
          tvalue += property->ignoreCount ? property->value : property->value * entry->count;
        } else {
          tvalue = property->ignoreCount ? property->value : property->value * entry->count;
          goto ct_finished;
        }
      }
    }
  }

ct_finished:
  database->unlock();

  return found ? tvalue : def_value;
}

/**
 * Calculates the base and augment value for the given property. Properties found on items matching
 * the specified type will be used as absolute values -- if multiple properties are found on
 * matching item types, only the last property found will be used.
 *
 * @param *hull
 *  The hull to be scanned for items with the specified property.
 *
 * @param *item_type
 *  The type of item to consider the base item type on which the property should be found.
 *
 * @param *prop_name
 *  The name of the property for which to calculate the totals
 *
 * @param def_value
 *  The default value to return if the player does not have the base item type required.
 *
 * @return
 *  The total value for the property.
 */
static int CalculateBasedPropertyTotal(ShipHull *hull, char *item_type, char *prop_name, int def_value)
{
  if (!hull || !item_type || !prop_name) {
    return def_value;
  }

  Link *ielink, *tlink, *plink;

  InventoryEntry *ientry;

  Item *item;
  ItemTypeEntry *tentry;
  Property *property;

  int base_found = 0;
  int bvalue = 0;
  int avalue = 0;
  int absolute = 0;


  database->lock();

  // Process hull properties.
  FOR_EACH(hull->propertyList, property, plink) {
    if (!property)
      continue;

    if (!strcmp(prop_name, property->name)) {
      if (!property->absolute) {
        avalue += property->value;
      } else {
        avalue = property->value;
        absolute = 1;
        break;
      }
    }
  }

  // Process items on the hull.
  FOR_EACH(&hull->inventoryEntryList, ientry, ielink) {
    if (!ientry || !ientry->count || !ientry->item) {
      continue;
    }

    item = ientry->item;
    int type_match = 0;

    // Check if the item type matches
    FOR_EACH(&item->itemTypeEntries, tentry, tlink) {
      if (!tentry || !tentry->itemType) {
        continue;
      }

      if (!strcmp(item_type, tentry->itemType->name)) {
        type_match = 1;
        break;
      }
    }

    // Check the item for matching property/ies.
    if (type_match || !absolute) {
      FOR_EACH(&item->propertyList, property, plink) {
        if (!property)
          continue;

        if (!strcmp(prop_name, property->name)) {
          if (type_match) {
            bvalue = property->ignoreCount ? property->value : property->value * ientry->count;
            base_found = 1;
          } else {
            if (!property->absolute) {
              avalue += property->ignoreCount ? property->value : property->value * ientry->count;
            } else {
              avalue = property->ignoreCount ? property->value : property->value * ientry->count;
              absolute = 1;
              break;
            }
          }
        }
      }
    }
  }

  database->unlock();

  return (base_found ? bvalue + avalue : def_value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Callbacks
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Called when getting a player's initial prize counts.
 */
static int GetPrizeCount(Player *player, ShipHull *hull, int freq, int ship, int prize, int init_count, void *exdata)
{
  switch (prize) {
    case PRIZE_BOUNCE:
      return (init_count + !!items->getPropertySumOnHull(player, hull, "Bouncing Bullets", 0));

    case PRIZE_PROX:
      return (init_count + !!items->getPropertySumOnHull(player, hull, "Bomb Proximity Trigger", 0));

    case PRIZE_MULTIFIRE:
      return (init_count + !!items->getPropertySumOnHull(player, hull, "Multifire Guns", 0));

    case PRIZE_SHRAP:
      return (init_count + items->getPropertySumOnHull(player, hull, "Bomb Shrapnel", 0));
  }

  return init_count;
}

/**
 * Calculates a player's gun/bomb level and critical rates.
 */
static void CalculateWeaponStats(Player *player, ShipHull *hull, int freq, int ship)
{
  // Input validation...
  if (!player || !hull) {
    return;
  }

  PlayerStatsData *pdata = PPDATA(player, pdkey);



  // Calculate gun stats
  pdata->gun_level = HSC_CLAMP(CalculateBasedPropertyTotal(hull, "Gun", "Gun Level", 0), 0, 4);
  pdata->gun_crit_rate = HSC_CLAMP(CalculatePropertyTotal(hull, "Gun Critical Rate", 0), 0, 100);
  pdata->gun_crit_boost = CalculatePropertyTotal(hull, "Gun Critical Boost", 0);

  if (!pdata->gun_level || pdata->gun_level > 3 || pdata->gun_crit_rate > 0) {
    pdata->modify_packets = 1;
  }

  // Impl note:
  // Bomb/mine crit stuff can cause desynch, as damage is based on the user's proximity to the
  // blast. Shrap level augmentation is about the only safe thing to do. Use these sparingly.

  // Calculate bomb stats
  pdata->bomb_level = HSC_CLAMP(CalculateBasedPropertyTotal(hull, "Bomb", "Bomb Level", 0), 0, 4);
  pdata->bomb_crit_rate = HSC_CLAMP(items->getPropertySumOnHull(player, hull, "Bomb Critical Rate", 0), 0, 100);
  pdata->bomb_crit_boost = CalculatePropertyTotal(hull, "Bomb Critical Boost", 0);

  if (!pdata->bomb_level || pdata->bomb_level > 3 || (pdata->bomb_crit_rate > 0 && pdata->bomb_crit_boost)) {
    pdata->modify_packets = 1;
  }

  pdata->bomb_shrap_count = HSC_CLAMP(CalculatePropertyTotal(hull, "Bomb Shrapnel", 0), 0, 32);
  pdata->bomb_shrap_level = HSC_CLAMP(CalculatePropertyTotal(hull, "Bomb Shrapnel Level", 0), 0, 4);
  pdata->bomb_shrap_bounce = !!CalculatePropertyTotal(hull, "Bomb Bouncing Shrapnel", !!CalculatePropertyTotal(hull, "Bouncing Bullets", 0));
  pdata->bomb_shrap_crit_rate = HSC_CLAMP(CalculatePropertyTotal(hull, "Bomb Shrapnel Critical Rate", 0), 0, 100);
  pdata->bomb_shrap_crit_level_boost = CalculatePropertyTotal(hull, "Bomb Shrapnel Crit. Level Boost", 0);
  pdata->bomb_shrap_crit_count_boost = CalculatePropertyTotal(hull, "Bomb Shrapnel Crit. Count Boost", 0);

  // Calculate mine stats
  pdata->mine_level = HSC_CLAMP(CalculateBasedPropertyTotal(hull, "Mine", "Mine Level", 0), 0, 4);
  pdata->mine_crit_rate = HSC_CLAMP(CalculatePropertyTotal(hull, "Mine Critical Rate", 0), 0, 100);
  pdata->mine_crit_boost = CalculatePropertyTotal(hull, "Mine Critical Boost", 0);

  if (!pdata->mine_level || pdata->mine_level > 3 || pdata->mine_level != pdata->bomb_level || (pdata->mine_crit_rate > 0 && pdata->mine_crit_boost)) {
    pdata->modify_packets = 1;
  }

  pdata->mine_shrap_count = HSC_CLAMP(CalculatePropertyTotal(hull, "Mine Shrapnel", 0), 0, 32);
  pdata->mine_shrap_level = HSC_CLAMP(CalculatePropertyTotal(hull, "Mine Shrapnel Level", 0), 0, 4);
  pdata->mine_shrap_bounce = !!CalculatePropertyTotal(hull, "Mine Bouncing Shrapnel", !!CalculatePropertyTotal(hull, "Bouncing Bullets", 0));
  pdata->mine_shrap_crit_rate = HSC_CLAMP(CalculatePropertyTotal(hull, "Mine Shrapnel Critical Rate", 0), 0, 100);
  pdata->mine_shrap_crit_level_boost = CalculatePropertyTotal(hull, "Mine Shrapnel Crit. Level Boost", 0);
  pdata->mine_shrap_crit_count_boost = CalculatePropertyTotal(hull, "Mine Shrapnel Crit. Count Boost", 0);

  if (pdata->bomb_shrap_count != pdata->mine_shrap_count || pdata->bomb_shrap_crit_rate > 0 || pdata->mine_shrap_crit_rate > 0) {
    pdata->modify_packets = 1;
  }




}


static int GetPropertySum(Player *player, ShipHull *hull, int freq, int ship, const char *section, const char *setting, int init_value, void *exdata)
{
  if (!player || !hull || !section || !setting || !exdata) {
    return init_value;
  }

  return items->getPropertySumOnHull(player, hull, exdata, init_value);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Handlers
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Called on a core player action (enter/leave arena, enter/leave game).
 */
static void OnPlayerAction(Player *player, int action, Arena *arena)
{
  PlayerStatsData *pdata = PPDATA(player, pdkey);

  switch (action) {
    case PA_ENTERARENA:
      pdata->modify_packets = 0;

      pdata->gun_level = 0;
      pdata->gun_crit_rate = 0;
      pdata->gun_crit_boost = 0;

      pdata->bomb_level = 0;
      pdata->bomb_crit_rate = 0;
      pdata->bomb_crit_boost = 0;

      pdata->bomb_shrap_count = 0;
      pdata->bomb_shrap_level = 0;
      pdata->bomb_shrap_bounce = 0;
      pdata->bomb_shrap_crit_rate = 0;
      pdata->bomb_shrap_crit_level_boost = 0;
      pdata->bomb_shrap_crit_count_boost = 0;

      pdata->mine_level = 0;
      pdata->mine_crit_rate = 0;
      pdata->mine_crit_boost = 0;

      pdata->mine_shrap_count = 0;
      pdata->mine_shrap_level = 0;
      pdata->mine_shrap_bounce = 0;
      pdata->mine_shrap_crit_rate = 0;
      pdata->mine_shrap_crit_level_boost = 0;
      pdata->mine_shrap_crit_count_boost = 0;
      break;

    case PA_LEAVEARENA:
      pdata->modify_packets = 0;
      break;
  }
}

/**
 * Called when a player performs an action that requires new settings to be compiled.
 */
static void OnOverridesRequested(Player *player, ShipHull *hull, int freq, int ship)
{
  lm->LogP(L_ERROR, "hscore_advitemprops", player, "Player overrides requested.");

  PlayerStatsData *pdata = PPDATA(player, pdkey);

  // Shut off packet modification. If it is required, it will be re-enabled by the settings
  // builder.
  pdata->modify_packets = 0;

  // Calculate magic stuff that would require too much work to risk doing multiple times in
  // callbacks.

  // WeaponLevels
  // Energy
  // Energy




}

/**
 * Called when a player has received setting overrides.
 */
static void OnOverridesReceived(Player *player, ShipHull *hull, int freq, int ship)
{
  lm->LogP(L_ERROR, "hscore_advitemprops", player, "Player overrides received.");
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Module Initialization
////////////////////////////////////////////////////////////////////////////////////////////////////

static int SetupArena(Arena *arena)
{
  // Register advisers
  mm->RegAdviser(&PlayerPacketAdviser, arena);

  // Register global event callbacks
  mm->RegCallback(CB_PLAYERACTION, OnPlayerAction, arena);
  mm->RegCallback(CB_OVERRIDES_REQUESTED, OnOverridesRequested, arena);
  mm->RegCallback(CB_OVERRIDES_RECEIVED, OnOverridesReceived, arena);

  #define REGISTER_PRIZE_CALLBACK(callback, arena, prize, exdata) \
    if (!spawner->registerPrizeCountCallback(callback, arena, prize, exdata)) { \
      lm->Log(L_ERROR, "<%s> Unable to register prize count callback in arena \"%s\" for prize: %i.", HSC_MODULE_NAME, arena->name, prize); \
      return 0; \
    }

  #define REGISTER_OVERRIDE_CALLBACK(callback, arena, section, setting, exdata) \
    if (!spawner->registerOverrideCallback(callback, arena, section, setting, exdata)) { \
      lm->Log(L_ERROR, "<%s> Unable to register override callback in arena \"%s\" for setting: %s.%s.", HSC_MODULE_NAME, arena->name, section, setting); \
      return 0; \
    }

  // Register prize count callbacks
  REGISTER_PRIZE_CALLBACK(GetPrizeCount, arena, PRIZE_BOUNCE, NULL);
  REGISTER_PRIZE_CALLBACK(GetPrizeCount, arena, PRIZE_PROX, NULL);
  REGISTER_PRIZE_CALLBACK(GetPrizeCount, arena, PRIZE_MULTIFIRE, NULL);
  REGISTER_PRIZE_CALLBACK(GetPrizeCount, arena, PRIZE_SHRAP, NULL);

  // Register global override callbacs

  // Register ship override callbacks
  for (int ship = SHIP_WARBIRD; ship < SHIP_SPEC; ++ship) {
    // REGISTER_OVERRIDE_CALLBACK(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "InitialGuns", (void*) HSC_TYPE_GUN);
    // REGISTER_OVERRIDE_CALLBACK(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "MaxGuns", (void*) HSC_TYPE_MAXGUN);
    // REGISTER_OVERRIDE_CALLBACK(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "InitialBombs", (void*) HSC_TYPE_BOMB);
    // REGISTER_OVERRIDE_CALLBACK(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "MaxBombs", (void*) HSC_TYPE_MAXBOMB);
  }

  #undef REGISTER_OVERRIDE_CALLBACK
  #undef REGISTER_PRIZE_CALLBACK

  return 1;
}

static void TearDownArena(Arena *arena)
{
  // Deregister global override callbacks

  // Deregister ship override callbacks
  for (int ship = SHIP_WARBIRD; ship < SHIP_SPEC; ++ship) {
    // spawner->deregisterOverrideCallback(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "InitialGuns");
    // spawner->deregisterOverrideCallback(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "MaxGuns");
    // spawner->deregisterOverrideCallback(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "InitialBombs");
    // spawner->deregisterOverrideCallback(CalculateWeaponLevel, arena, cfg->SHIP_NAMES[ship], "MaxBombs");
  }

  // Deregister prize count callbacks
  spawner->deregisterPrizeCountCallback(GetPrizeCount, arena, PRIZE_BOUNCE);
  spawner->deregisterPrizeCountCallback(GetPrizeCount, arena, PRIZE_PROX);
  spawner->deregisterPrizeCountCallback(GetPrizeCount, arena, PRIZE_MULTIFIRE);
  spawner->deregisterPrizeCountCallback(GetPrizeCount, arena, PRIZE_SHRAP);

  // Deregister event callbacks.
  mm->UnregCallback(CB_PLAYERACTION, OnPlayerAction, arena);
  mm->UnregCallback(CB_OVERRIDES_REQUESTED, OnOverridesRequested, arena);
  mm->UnregCallback(CB_OVERRIDES_RECEIVED, OnOverridesReceived, arena);

  // Deregister advisers
  mm->UnregAdviser(&PlayerPacketAdviser, arena);
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

    lm        = mm->GetInterface(I_LOGMAN, ALLARENAS);
    cfg       = mm->GetInterface(I_CONFIG, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    prng      = mm->GetInterface(I_PRNG, ALLARENAS);

    items     = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    database  = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    spawner   = mm->GetInterface(I_HSCORE_SPAWNER2, ALLARENAS);

    printf("%i, %i, %i, %i -- %i, %i, %i\n", !!lm, !!cfg, !!pd, !!prng, !!items, !!database, !!spawner);

    return mm && (lm && cfg && pd && prng) && (items && database && spawner);
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
    mm->ReleaseInterface(prng);

    mm->ReleaseInterface(items);
    mm->ReleaseInterface(database);
    mm->ReleaseInterface(spawner);

    mm = NULL;
  }
}


EXPORT const char info_hscore_advitemprops[] = "v1.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hscore_advitemprops(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      // Get interfaces
      if (!GetInterfaces(modman, arena)) {
        printf("<hscore_advitemprops> Could not acquire required interfaces.\n");
        ReleaseInterfaces();
        break;
      }

      // Allocate pdata
      pdkey = pd->AllocatePlayerData(sizeof(PlayerStatsData));
      if (pdkey == -1) {
        printf("<%s> Unable to allocate per-player data.\n", HSC_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", HSC_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      if (!SetupArena(arena)) {
        lm->Log(L_ERROR, "<%s> Unable to complete arena setup tasks for arena \"%s.\"", HSC_MODULE_NAME, arena->name);
        TearDownArena(arena);
        break;
      }

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      TearDownArena(arena);
      return MM_OK;

    //////////////////////////////////////////////////

    case MM_UNLOAD:
      pd->FreePlayerData(pdkey);

      ReleaseInterfaces();
      return MM_OK;
  }

  return MM_FAIL;
}