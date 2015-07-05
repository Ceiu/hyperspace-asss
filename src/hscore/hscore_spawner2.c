/**
 * hscore_spawner2.c
 *
 * Current issues:
 *  - Doors are out-of-sync
 *
 * @author Chris "Ceiu" Rog <ceiu@cericlabs.com>
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "clientset.h"

#include "hscore.h"
#include "hscore_storeman.h"
#include "hscore_database.h"

#include "hscore_spawner2.h"


////////////////////////////////////////////////////////////////////////////////////////////////////
// Global defs
////////////////////////////////////////////////////////////////////////////////////////////////////

#define HSC_MIN(x, y) ((x) < (y) ? (x) : (y))
#define HSC_MAX(x, y) ((x) > (y) ? (x) : (y))
#define HSC_CLAMP(val, min, max) HSC_MIN(HSC_MAX((val), (min)), (max))

#define HSC_MODULE_NAME "hscore_spawner2"

/**
 * The number of spawn prizes we're tracking. Should be equal to the largest prize in the prize
 * enum (defs.h).
 *
 * Note:
 *  Indexes into prize arrays are zero-indexed, where prize numbers are not.
 */
#define PRIZE_COUNT PRIZE_PORTAL

/**
 * The number of buckets we will maintain for the override callback registration map. This should be
 * around 50-75% the total number of overridden settings.
 */
#define OVERRIDE_CALLBACK_BUCKET_COUNT 75


/**
 * Structure to store settings and override keys in a way that they can be stepped through
 * programmatically.
 */
typedef struct SettingOverride {
  /**
   * The section in which the setting is found.
   */
  const char *section;

  /**
   * The name of the section setting.
   */
  const char *setting;

  /**
   * The override key for this setting.
   */
  override_key_t orkey;

  /**
   * The minimum value allowed for this setting.
   */
  i32 min;

  /**
   * The maximum value allowed for this setting.
   */
  i32 max;

  /**
   * The default value for this setting. Only applicable when the arena setting is not already
   * defined.
   */
  i32 def;

  /**
   * A link to the next override in the chain.
   */
  struct SettingOverride *next;
} SettingOverride;


/**
 * Information associated with a compile settings request.
 */
typedef struct CSAction {
  /**
   * The hull from which to derive the new settings.
   */
  ShipHull *hull;

  /**
   * The freq for which the player's ship settings will be vaild. The player will be placed on this
   * freq if they're not already on it once the settings are received.
   */
  int freq;

  /**
   * The ship to receive the settings. The player should also end up in this ship, since they won't
   * have (proper) settings for the others.
   */
  int ship;

  /**
   * The prizes with which the player spawned prior to receiving these settings. Used to calculate
   * prize diffs and remove spawned prizes. Null if we don't need to calculate a diff.
   */
  unsigned char *prizes;

  /**
   * The length of the prizes array.
   */
  unsigned int prize_len;

  /**
   * The next compilation action to perform. Will be set if the settings are out of sync before the
   * client receives them (quick purchases, bad module logic, etc.). If set, freq/ship changes will
   * not be performed.
   */
  struct CSAction *next;
} CSAction;


/**
 * Player data.
 */
typedef struct {
  /**
   * Whether or not the player is one for whom we're maintaining settings and spawning.
   */
  char managed_player;

  /**
   * The compile settings action currently in progress. Null between compilations.
   */
  CSAction *csaction;

  /**
   * Whether or not the player has a pending shipset change.
   */
  char pending_shipset_change;

  /**
   * The player's pending shipset.
   */
  char pending_shipset;

  /**
   * The prizes to give the player on reprize. Range: 0-255
   *
   * Note:
   *  When checking a prize constant, its index in this array is one lower than the actual constant
   *  value.
   */
  unsigned char prizes[PRIZE_COUNT];
} PlayerSpawnData;


/**
 * Information associated with registered callbacks.
 */
typedef struct CallbackEntry {
  /**
   * The key for this callback entry.
   */
  u32 key;

  /**
   * The arena in which the callback is to be used. If ALLARENAS... well...
   */
  Arena *arena;

  /**
   * The callback to execute when retrieving the override value for this setting.
   */
  void *callback;

  /**
   * The extra/extended data to pass to the callback.
   */
  void *exdata;

  /**
   * The next callback entry in the chain for the current bucket.
   */
  struct CallbackEntry *next;
} CallbackEntry;



// Modules
static Imodman *mm;
static Ilogman *lm;
static Imainloop *ml;
static Inet *net;
static Igame *game;
static Iclientset *clientset;
static Iplayerdata *pd;
static Ifreqman *freqman;
static Ichat *chat;
static Icmdman *cmd;
static Iconfig *cfg;

static Ihscoreitems *items;
static Ihscoredatabase *database;

// Global resource identifiers
static int pdkey;

// Lock used to keep csaction juggling in sync.
static pthread_mutexattr_t pdata_mutexattr;
static pthread_mutex_t pdata_mutex;
static char pdata_mutexattr_status = 0;
static char pdata_mutex_status = 0;

// A list of overridden settings.
SettingOverride *(ship_overrides[8]);
SettingOverride *global_overrides;
static char overrides_initialized = 0;

// Our pseudo-map of callback entries. Indexes are (override_key_t % OVERRIDE_CALLBACK_BUCKET_COUNT).
CallbackEntry *(override_callbacks[OVERRIDE_CALLBACK_BUCKET_COUNT]);
CallbackEntry *(prizecount_callbacks[PRIZE_COUNT]);


////////////////////////////////////////////////////////////////////////////////////////////////////
// Prototypes!
////////////////////////////////////////////////////////////////////////////////////////////////////

static SettingOverride* BuildSettingOverride(const char *section, const char *setting, i32 min, i32 max, i32 def);
static void InitializeOverrides();
static void ReleaseOverrides();

static CallbackEntry* BuildPrizeCountCallbackEntry(PrizeCountCallbackFunction callback, Arena *arena, int prize, void *exdata);
static int ExecutePrizeCountCallbacks(Player *player, ShipHull *hull, int freq, int ship, int prize, int init_value);
static CallbackEntry* BuildOverrideCallbackEntry(SettingOverrideCallbackFunction callback, Arena *arena, override_key_t orkey, void *exdata);
static int ExecuteOverrideCallbacks(Player *player, ShipHull *hull, int freq, int ship, override_key_t orkey, const char *section, const char *setting, int init_value);

static CSAction* BuildCSAction(ShipHull *hull, int freq, int ship, unsigned char *prizes, unsigned int prize_len);
static void DoSettingsOverrides(Player *player, ShipHull *hull, int freq, int ship);
static void DoPrizeOverrides(Player *player, ShipHull *hull, int freq, int ship);
static int CompileSettings(Player *player, CSAction *csaction);
static int CompileSettingsEx(Player *player, ShipHull *hull, int freq, int ship, unsigned char *prizes, unsigned int prize_len);
static void RespawnPlayer(Player *player);
static void RevertPlayerOverrides(Player *player);
static int CompleteShipSetChange(Player *player);

static int HasPendingShipSetChange(Player *player);
static int GetPendingShipSet(Player *player);
static int CompleteShipSetChange(Player *player);

static int ContinueAdviserFreqChangeCheck(Player *player, int requested_freq, int is_changing, char *mbuffer, int buffer_len);
static int ContinueAdviserShipChangeCheck(Player *player, int requested_ship, int is_changing, char *mbuffer, int buffer_len);
static int CanChangeToFreq(Player *player, int requested_freq, int is_changing, char *mbuffer, int buffer_len);
static int CanChangeToShip(Player *player, int requested_ship, int is_changing, char *mbuffer, int buffer_len);
static shipmask_t GetAllowableShips(Player *player, int frequency, char *mbuffer, int buffer_len);

static void OnPlayerAction(Player *player, int action, Arena *arena);
static void OnSettingsReceived(Player *player, int success, void *clos);
static void OnPlayerSpawn(Player *player, int reason);
static void OnItemCountChanged(Player *player, ShipHull *hull, Item *item, InventoryEntry *entry, int newCount, int oldCount);
static void OnItemsReloaded();
static void OnModuleAttach(Arena *arena);
static void OnModuleDetach(Arena *arena);

static void OnShipSetCommand(const char *command, const char *params, Player *player, const Target *target);

static int IsManaged(Player *player);
static int RecompileOverrides(Player *player);
static int GetPlayerOverrideValue(Player *player, const char *section, const char *setting, int default_value);
static int HasPendingShipSetChange(Player *player);
static int GetPendingShipSet(Player *player);
static int RegisterPrizeCountCallback(PrizeCountCallbackFunction callback, Arena *arena, int prize, void *exdata);
static int DeregisterPrizeCountCallback(PrizeCountCallbackFunction callback, Arena *arena, int prize);
static int RegisterOverrideCallback(SettingOverrideCallbackFunction callback, Arena *arena, const char *section, const char *setting, void *exdata);;
static int DeregisterOverrideCallback(SettingOverrideCallbackFunction callback, Arena *arena, const char *section, const char *setting);

static int GetInterfaces(Imodman *modman, Arena *arena);
static void ReleaseInterfaces();


////////////////////////////////////////////////////////////////////////////////////////////////////
// Interface & Adviser Declarations
////////////////////////////////////////////////////////////////////////////////////////////////////

static IHSCoreSpawner spawner_interface = {
  INTERFACE_HEAD_INIT(I_HSCORE_SPAWNER2, HSC_MODULE_NAME)

  IsManaged,
  RecompileOverrides,
  GetPlayerOverrideValue,
  HasPendingShipSetChange,
  GetPendingShipSet,
  RegisterPrizeCountCallback,
  DeregisterPrizeCountCallback,
  RegisterOverrideCallback,
  DeregisterOverrideCallback
};

static Aenforcer spawner_freqman_enforcer = {
  ADVISER_HEAD_INIT(A_ENFORCER)

  NULL,
  NULL,
  CanChangeToShip,
  CanChangeToFreq,
  GetAllowableShips
};


////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Allocates and initializes a new SettingOverride struct for the specified setting. If the setting
 * is not valid or cannot be overridden, this function returns null.
 *
 * @param *section
 *  The section in which the setting is defined.
 *
 * @param *setting
 *  The setting to override.
 *
 * @param min
 *  The minimum value allowed for the setting.
 *
 * @param max
 *  The maximum value allowed for the setting.
 *
 * @return
 *  A pointer to the new SettingOverride struct, or null if the setting could not be overridden.
 */
static SettingOverride* BuildSettingOverride(const char *section, const char *setting, i32 min, i32 max, i32 def)
{
  if (!section) {
    lm->Log(L_ERROR, "<%s> BuildSettingOverride: No section provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!setting) {
    lm->Log(L_ERROR, "<%s> BuildSettingOverride: No setting provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (min > max) {
    lm->Log(L_ERROR, "<%s> BuildSettingOverride: Minimum allowed value is larger than the maximum allowed value. (Min: %i, Max: %i).", HSC_MODULE_NAME, min, max);
    return NULL;
  }

  if (def < min || def > max) {
    lm->Log(L_ERROR, "<%s> BuildSettingOverride: Default value is out-of-range. (Def: %i, Min: %i, Max: %i).", HSC_MODULE_NAME, def, min, max);
    return NULL;
  }


  SettingOverride *override = NULL;
  override_key_t orkey = clientset->GetOverrideKey(section, setting);

  if (orkey) {
    if (!(override = malloc(sizeof(SettingOverride)))) {
      Error(EXIT_MEMORY, "<%s> Unable to allocate new SettingOverride (size: %lu)\n", HSC_MODULE_NAME, sizeof(SettingOverride));
    }

    override->section = section;
    override->setting = setting;
    override->orkey = orkey;

    override->min = min;
    override->max = max;
    override->def = def;

    override->next = NULL;
  } else {
    lm->Log(L_ERROR, "<%s> Unable to override setting %s.%s", HSC_MODULE_NAME, section, setting);
  }

  return override;
}

/**
 * Links the overrides together in a chain, returning the new tail of the chain.
 *
 * Note:
 *  Any existing chain built on the base override will be overridden by this function.
 *
 * @param *base
 *  The base override link. If omitted, the chain will begin with the next override.
 *
 * @param *next
 *  The next override in the chain. If null, the tail will not be chained.
 *
 * @return
 *  The new tail of the override chain, or null if no chain was begun.
 */
static inline void ChainOverrides(SettingOverride **head, SettingOverride **tail, SettingOverride *new)
{
  if (new) {
    if (!*head) {
      *head = *tail = new;
    } else if (*tail) {
      *tail = (*tail)->next = new;
    }
  }
}

/**
 * Initializes the list of supported overrides. Should only be called during module load.
 */
static void InitializeOverrides()
{
  SettingOverride *head, *tail;

  if (!overrides_initialized) {
    // Ship overrides
    for (int ship = SHIP_WARBIRD; ship < SHIP_SPEC; ++ship) {
      head = tail = NULL;
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "ShrapnelMax", 0, 32, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "ShrapnelRate", 0, 32, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "CloakStatus", 0, 2, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "StealthStatus", 0, 2, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "XRadarStatus", 0, 2, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "AntiWarpStatus", 0, 2, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialGuns", 0, 3, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaxGuns", 0, 3, 1));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialBombs", 0, 3, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaxBombs", 0, 3, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "SeeMines", 0, 1, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "SeeBombLevel", 0, 1, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "Gravity", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "GravityTopSpeed", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BulletFireEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MultiFireEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BombFireEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BombFireEnergyUpgrade", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "LandmineFireEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "LandmineFireEnergyUpgrade", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "CloakEnergy", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "StealthEnergy", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "AntiWarpEnergy", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "XRadarEnergy", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaximumRotation", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaximumThrust", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaximumSpeed", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaximumRecharge", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaximumEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialRotation", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialThrust", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialSpeed", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialRecharge", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "UpgradeRotation", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "UpgradeThrust", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "UpgradeSpeed", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "UpgradeRecharge", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "UpgradeEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "AfterburnerEnergy", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "DisableFastShooting", 0, 1, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BombThrust", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "TurretThrustPenalty", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "TurretSpeedPenalty", -32767, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BulletFireDelay", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MultiFireDelay", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BombFireDelay", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "LandmineFireDelay", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "RocketTime", 0, 32767, 0));
      // ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialBounty", 0, 32767, 0)); // This is handled manually to account for on-spawn prizes.
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "DamageFactor", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "AttachBounty", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "SoccerThrowTime", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "SoccerBallProximity", 0, 32767, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "MaxMines", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "RepelMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BurstMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "DecoyMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "ThorMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "BrickMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "RocketMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "PortalMax", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialRepel", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialBurst", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialBrick", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialRocket", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialThor", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialDecoy", 0, 255, 0));
      ChainOverrides(&head, &tail, BuildSettingOverride(cfg->SHIP_NAMES[ship], "InitialPortal", 0, 255, 0));

      ship_overrides[ship] = head;
    }

    // Global overrides
    head = tail = NULL;
    ChainOverrides(&head, &tail, BuildSettingOverride("Bullet", "BulletDamageLevel", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bullet", "BulletDamageUpgrade", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Burst", "BurstDamageLevel", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bomb", "BombDamageLevel", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bomb", "BombExplodePixels", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bomb", "EBombShutdownTime", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bomb", "EBombDamagePercent", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bomb", "BBombDamagePercent", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Bomb", "JitterTime", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Misc", "DecoyAliveTime", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Misc", "WarpPointDelay", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Rocket", "RocketThrust", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Rocket", "RocketSpeed", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Shrapnel", "InactiveShrapDamage", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Shrapnel", "ShrapnelDamagePercent", -32767, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Radar", "MapZoomFactor", 1, 48, 10));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerGunUpgrade", 0, 1, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerGunFireDelay", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerBombUpgrade", 0, 1, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerBombFireDelay", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerFireCostPercent", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerSpeedAdjustment", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerThrustAdjustment", 0, 32767, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Flag", "FlaggerOnRadar", 0, 1, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Soccer", "AllowGuns", 0, 1, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Soccer", "AllowBombs", 0, 1, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Soccer", "UseFlagger", 0, 1, 0));
    ChainOverrides(&head, &tail, BuildSettingOverride("Soccer", "BallLocation", 0, 1, 0));

    global_overrides = head;

    overrides_initialized = 1;
  }
}

/**
 * Frees the overrides list. Should only be called during module unload.
 */
static void ReleaseOverrides()
{
  SettingOverride *chain, *temp;

  if (overrides_initialized) {
    // Ship overrides
    for (int ship = SHIP_WARBIRD; ship < SHIP_SPEC; ++ship) {
      chain = ship_overrides[ship];

      while (chain) {
        temp = chain;
        chain = chain->next;

        free(temp);
      }
    }

    // Global overrides
    chain = global_overrides;

    while (chain) {
      temp = chain;
      chain = chain->next;

      free(temp);
    }

    // Release prize count callbacks
    for (int i = 0; i < PRIZE_COUNT; ++i) {
      CallbackEntry *chain = prizecount_callbacks[i];
      CallbackEntry *temp;

      while (chain) {
        temp = chain;
        chain = chain->next;

        free(temp);
      }
    }

    // Release override callbacks
    for (int i = 0; i < OVERRIDE_CALLBACK_BUCKET_COUNT; ++i) {
      CallbackEntry *chain = override_callbacks[i];
      CallbackEntry *temp;

      while (chain) {
        temp = chain;
        chain = chain->next;

        free(temp);
      }
    }

    overrides_initialized = 0;
  }
}

/**
 * Allocates and initializes a new CallbackEntry struct for the specified prize count override. If
 * the prize number is not valid or cannot be overridden, this function returns null.
 *
 * @param *callback
 *  The callback to call when retrieving the initial count for the specified prize.
 *
 * @param *arena
 *  The arena in which the callback will be registered. May be set to ALLARENAS.
 *
 * @param prize
 *  The prize for which to override the initial count.
 *
 * @param *exdata
 *  Extra/extended data to pass to the callback. May be null.
 *
 * @return
 *  A pointer to the new CallbackEntry struct, or null if the prize count could not be overridden.
 */
static CallbackEntry* BuildPrizeCountCallbackEntry(PrizeCountCallbackFunction callback, Arena *arena, int prize, void *exdata)
{
  if (!callback) {
    lm->Log(L_ERROR, "<%s> BuildOverrideCallbackEntry: No callback provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!prize) {
    lm->Log(L_ERROR, "<%s> BuildOverrideCallbackEntry: No prize provided.", HSC_MODULE_NAME);
    return NULL;
  }

  CallbackEntry *entry = NULL;

  if (!(entry = malloc(sizeof(CallbackEntry)))) {
    Error(EXIT_MEMORY, "<%s> Unable to allocate new CallbackEntry (size: %lu)\n", HSC_MODULE_NAME, sizeof(CallbackEntry));
  }

  entry->key = (u32) prize;
  entry->arena = arena;
  entry->callback = callback;
  entry->exdata = exdata;

  entry->next = NULL;

  return entry;
}

/**
 * Executes the override callbacks registered for the specified prize and returns the overridden
 * initial count. If no callbacks have been registered for the prize, the initial value will be
 * returned unmodified.
 *
 * @param *player
 *  The player for whom the prize count will be applied.
 *
 * @param *hull
 *  The ship hull from which to derive the prize count.
 *
 * @param freq
 *  The frequency on which the overridden prize count will be valid.
 *
 * @param ship
 *  The ship on which the overridden prize count will be valid.
 *
 * @param prize
 *  The prize for which to retrieve the initial count.
 *
 * @param init_value
 *  The initial value for the setting.
 *
 * @return
 *  The new initial prize count for the specified prize.
 */
static int ExecutePrizeCountCallbacks(Player *player, ShipHull *hull, int freq, int ship, int prize, int init_value)
{
#ifdef HSCS_EXTRA_CHECKS
  // Due to the sheer volume of calls this function will be receiving, we don't want to do these
  // checks unless we're debugging.
  if (!player) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No player provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!hull) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No hull provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (prize < 0 || prize >= PRIZE_COUNT) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: Invalid prize specified %i.", HSC_MODULE_NAME, prize);
    return NULL;
  }
#endif

  Arena *arena = player->arena;
  int value = init_value;
  u32 cbkey = (u32) prize;

  pthread_mutex_lock(&pdata_mutex);

  for (CallbackEntry *chain = prizecount_callbacks[prize - 1]; chain; chain = chain->next) {
    if ((chain->key == cbkey) && (chain->arena == arena || chain->arena == ALLARENAS)) {
      value = ((PrizeCountCallbackFunction)chain->callback)(player, hull, freq, ship, prize, value, chain->exdata);
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  return value;
}



/**
 * Allocates and initializes a new CallbackEntry struct for the specified setting override. If the
 * setting is not valid or cannot be overridden, this function returns null.
 *
 * @param *callback
 *  The callback to call when retrieving the value for the specified setting.
 *
 * @param *arena
 *  The arena in which the callback will be registered. May be set to ALLARENAS.
 *
 * @param *section
 *  The section in which the setting is found.
 *
 * @param *setting
 *  The setting to override.
 *
 * @param *exdata
 *  Extra/extended data to pass to the callback. May be null.
 *
 * @return
 *  A pointer to the new CallbackEntry struct, or null if the setting could not be overridden.
 */
static CallbackEntry* BuildOverrideCallbackEntry(SettingOverrideCallbackFunction callback, Arena *arena, override_key_t orkey, void *exdata)
{
  if (!callback) {
    lm->Log(L_ERROR, "<%s> BuildOverrideCallbackEntry: No callback provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!orkey) {
    lm->Log(L_ERROR, "<%s> BuildOverrideCallbackEntry: No override key provided.", HSC_MODULE_NAME);
    return NULL;
  }

  CallbackEntry *entry = NULL;

  if (!(entry = malloc(sizeof(CallbackEntry)))) {
    Error(EXIT_MEMORY, "<%s> Unable to allocate new CallbackEntry (size: %lu)\n", HSC_MODULE_NAME, sizeof(CallbackEntry));
  }

  entry->key = (u32) orkey;
  entry->arena = arena;
  entry->callback = callback;
  entry->exdata = exdata;

  entry->next = NULL;

  return entry;
}

/**
 * Executes the override callbacks registered for the specified key and returns the override value.
 * If no callbacks have been registered for the key, the initial value will be returned unmodified.
 *
 * @param *player
 *  The player for whom the overrides will be applied.
 *
 * @param *hull
 *  The ship hull from which to derive the setting value.
 *
 * @param freq
 *  The frequency on which the overridden settings will be valid.
 *
 * @param ship
 *  The ship on which the overridden settings will be valid.
 *
 * @param orkey
 *  The key for which to execute registered override callbacks.
 *
 * @param *section
 *  The section in which the setting is defined.
 *
 * @param *setting
 *  The setting to override.
 *
 * @param init_value
 *  The initial value for the setting.
 *
 * @return
 *  The new value for the setting.
 */
static int ExecuteOverrideCallbacks(Player *player, ShipHull *hull, int freq, int ship, override_key_t orkey, const char *section, const char *setting, int init_value)
{
#ifdef HSCS_EXTRA_CHECKS
  // Due to the sheer volume of calls this function will be receiving, we don't want to do these
  // checks unless we're debugging.
  if (!player) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No player provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!hull) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No hull provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!orkey) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No override key provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!section) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No section provided.", HSC_MODULE_NAME);
    return NULL;
  }

  if (!setting) {
    lm->Log(L_ERROR, "<%s> ExecuteOverrideCallbacks: No setting provided.", HSC_MODULE_NAME);
    return NULL;
  }
#endif

  Arena *arena = player->arena;
  int value = init_value;
  int index = orkey % OVERRIDE_CALLBACK_BUCKET_COUNT;
  u32 cbkey = (u32) orkey;

  pthread_mutex_lock(&pdata_mutex);

  for (CallbackEntry *chain = override_callbacks[index]; chain; chain = chain->next) {
    if ((chain->key == cbkey) && (chain->arena == arena || chain->arena == ALLARENAS)) {
      value = ((SettingOverrideCallbackFunction)chain->callback)(player, hull, freq, ship, section, setting, value, chain->exdata);
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  return value;
}

/**
 * Allocates a new CSAction for the specified ship and hull. If the CSAction cannot be allocated,
 * this function throws an error.
 *
 * @param *hull
 *  A pointer to the ship hull from which to derive the new settings.
 *
 * @param freq
 *  The frequency on which the new settings are valid. The player will be placed on this frequency
 *  once the new settings are received.
 *
 * @param ship
 *  The ship to receive the compiled settings. The player will be placed in this ship once the new
 *  settings are received.
 *
 * @return
 *  A pointer to the newly allocated CSAction, or NULL if any input is invalid. The CSAction should
 *  be freed when it is no longer needed.
 */
static CSAction* BuildCSAction(ShipHull *hull, int freq, int ship, unsigned char *prizes, unsigned int prize_len)
{
  if (!hull) {
    lm->Log(L_ERROR, "<%s> BuildCSAction: Invalid value for hull: NULL", HSC_MODULE_NAME);
    return NULL;
  }

  if (freq < 0 || freq > 9999) {
    lm->Log(L_ERROR, "<%s> BuildCSAction: Invalid value for freq: %d", HSC_MODULE_NAME, freq);
    return NULL;
  }

  if (ship < SHIP_WARBIRD || ship > SHIP_SHARK) {
    lm->Log(L_ERROR, "<%s> BuildCSAction: Invalid value for ship: %d", HSC_MODULE_NAME, ship);
    return NULL;
  }


  CSAction *csaction = malloc(sizeof(CSAction));

  if (!csaction) {
    Error(EXIT_MEMORY, "<%s> Unable to allocate new CSAction (size: %lu)\n", HSC_MODULE_NAME, sizeof(CSAction));
  }

  csaction->hull = hull;
  csaction->freq = freq;
  csaction->ship = ship;

  if (prizes && prize_len) {
    size_t bytes = sizeof(unsigned char) * prize_len;
    csaction->prizes = malloc(bytes);

    if (!csaction->prizes) {
      Error(EXIT_MEMORY, "<%s> Unable to allocate new prizes array (size: %lu)\n", HSC_MODULE_NAME, bytes);
    }

    memcpy(csaction->prizes, prizes, bytes);
    csaction->prize_len = prize_len;
  } else {
    csaction->prizes = NULL;
    csaction->prize_len = 0;
  }

  csaction->next = NULL;

  return csaction;
}

/**
 * Calls advisers to get the player's settings overrides.
 *
 * @param *player
 *  The player for which to retrieve settings overrides.
 *
 * @param *hull
 *  The ship hull from which to derive settings overrides.
 *
 * @param freq
 *  The freq on which the settings will be valid.
 *
 * @param ship
 *  The ship on which the settings will be valid.
 *
 * @return void
 */
static void DoSettingsOverrides(Player *player, ShipHull *hull, int freq, int ship)
{
  if (!player) {
    lm->Log(L_ERROR, "<%s> DoPrizeOverrides: no player provided.", HSC_MODULE_NAME);
    return;
  }

  if (!hull) {
    lm->Log(L_ERROR, "<%s> DoPrizeOverrides: no ship hull provided.", HSC_MODULE_NAME);
    return;
  }

  if (ship < SHIP_WARBIRD || ship > SHIP_SHARK) {
    lm->LogP(L_ERROR, HSC_MODULE_NAME, player, "Tried to compile settings for an invalid ship: %i.", ship);
    return;
  }

  if (!overrides_initialized) {
    lm->LogP(L_ERROR, HSC_MODULE_NAME, player, "Tried to compile settings before settings are initialized.");
    return;
  }


  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  ConfigHandle config = player->arena->cfg;
  SettingOverride *chain;
  override_key_t orkey;

  pthread_mutex_lock(&pdata_mutex);

  // Count the number of prizes will be granted on spawn.
  int pcount = 0;
  for (int prize = 0; prize < PRIZE_COUNT; ++prize) {
    pcount += pdata->prizes[prize];
  }

  // Do ship overrides...
  for (chain = ship_overrides[ship]; chain; chain = chain->next) {
    i32 value = cfg->GetInt(config, chain->section, chain->setting, chain->def);

    printf("Initial value for setting %s.%s: %i\n", chain->section, chain->setting, value);

    // Call registered override callbacks
    value = ExecuteOverrideCallbacks(player, hull, freq, ship, chain->orkey, chain->section, chain->setting, value);

    printf("Final value for setting %s.%s: %i\n", chain->section, chain->setting, HSC_CLAMP(value, chain->min, chain->max));

    // Set clamped value
    clientset->PlayerOverride(player, chain->orkey, HSC_CLAMP(value, chain->min, chain->max));
  }

  // Setup initial bounty...
  if ((orkey = clientset->GetOverrideKey(cfg->SHIP_NAMES[ship], "InitialBounty"))) {
    i32 value = cfg->GetInt(config, cfg->SHIP_NAMES[ship], "InitialBounty", 0);

    // Call registered override callbacks
    value = ExecuteOverrideCallbacks(player, hull, freq, ship, orkey, cfg->SHIP_NAMES[ship], "InitialBounty", value);

    // Reduce initial bounty by the known prize count
    clientset->PlayerOverride(player, orkey, HSC_CLAMP(value - pcount, 0, 32767));
  } else {
    lm->LogP(L_ERROR, HSC_MODULE_NAME, player, "Unable to obtain override key for initial bounty setting on %s.", cfg->SHIP_NAMES[ship]);
  }

  // Do global overrides...
  for (chain = global_overrides; chain; chain = chain->next) {
    i32 value = cfg->GetInt(config, chain->section, chain->setting, chain->def);

    printf("Initial value for setting %s.%s: %i\n", chain->section, chain->setting, value);

    // Call registered override callbacks
    value = ExecuteOverrideCallbacks(player, hull, freq, ship, chain->orkey, chain->section, chain->setting, value);

    printf("Final value for setting %s.%s: %i\n", chain->section, chain->setting, HSC_CLAMP(value, chain->min, chain->max));

    // Set clamped value
    clientset->PlayerOverride(player, chain->orkey, HSC_CLAMP(value, chain->min, chain->max));
  }

  pthread_mutex_unlock(&pdata_mutex);
}

/**
 * Calls registered callbacks to get the player's spawn prize counts.
 *
 * @param *player
 *  The player for which to retrieve spawn prize counts.
 *
 * @param *hull
 *  The ship hull from which to derive spawn prize counts.
 *
 * @param freq
 *  The freq on which the prize counts will be valid.
 *
 * @param ship
 *  The ship on which the prize counts will be valid.
 *
 * @return void
 */
static void DoPrizeOverrides(Player *player, ShipHull *hull, int freq, int ship)
{
  if (!player) {
    lm->Log(L_ERROR, "<%s> DoPrizeOverrides: no player provided.", HSC_MODULE_NAME);
    return;
  }

  if (!hull) {
    lm->Log(L_ERROR, "<%s> DoPrizeOverrides: no ship hull provided.", HSC_MODULE_NAME);
    return;
  }

  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  int prize, count;

  for (prize = 0; prize < PRIZE_COUNT; ++prize) {
    count = 0;

    // Call registered prize count callbacks.
    count = (ExecutePrizeCountCallbacks(player, hull, freq, ship, (prize + 1), count) & 0xFF);

    // Set clamped value
    pdata->prizes[prize] = count;
  }
}

/**
 * Compiles settings for the specified player using the data in the provided CSAction.
 *
 * @param *player
 *  The player to receive the compiled settings.
 *
 * @param *csaction
 *  The compiled-settings action
 *
 * @return
 *  True if the new settings were compiled and sent successfully; false otherwise.
 */
static int CompileSettings(Player *player, CSAction *csaction)
{
  if (!player) {
    lm->Log(L_ERROR, "<%s> CompileSettings: no player provided.", HSC_MODULE_NAME);
    return 0;
  }

  if (!csaction) {
    lm->Log(L_ERROR, "<%s> CompileSettings: no csaction provided.", HSC_MODULE_NAME);
    return 0;
  }


  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  CSAction *base;

  // Update player's action chain...
  pthread_mutex_lock(&pdata_mutex);

  if (pdata->csaction) {
    for (base = pdata->csaction; base->next; base = base->next);
    base->next = csaction;
  } else {
    pdata->csaction = csaction;
  }

  DO_CBS(CB_OVERRIDES_REQUESTED, player->arena, OverridesReceivedCallbackFunction, (player, csaction->hull, csaction->freq, csaction->ship));

  // Impl note:
  // We need prize counts before settings overrides so we can adjust initial bounty appropriately.
  DoPrizeOverrides(player, csaction->hull, csaction->freq, csaction->ship);
  DoSettingsOverrides(player, csaction->hull, csaction->freq, csaction->ship);

  // Send settings w/callback
  clientset->SendClientSettingsWithCallback(player, OnSettingsReceived, NULL);

  pthread_mutex_unlock(&pdata_mutex);
  return 1;
}

/**
 * Compiles settings for the given hull and sends them to the specified player.
 *
 * @param *player
 *  The player to receive the compiled settings.
 *
 * @param *hull
 *  The hull containing the items to use for compiling settings.
 *
 * @param ship
 *  The ship to receive the settings. All other ships will be ignored.
 *
 * @param *prizes
 *  An array of prizes the player has spawned with. If provided, the prizes will be removed once the
 *  new settings have been received.
 *
 * @return
 *  True if the new settings were compiled and sent successfully; false otherwse.
 */
static int CompileSettingsEx(Player *player, ShipHull *hull, int freq, int ship, unsigned char *prizes, unsigned int prize_len)
{
  return CompileSettings(player, BuildCSAction(hull, freq, ship, prizes, prize_len));
}

/**
 * Performs any respawn operations necessary for the specified player. This may or may not include
 * giving prizes and/or resetting timers.
 *
 * @param *player
 *  The player to be prized.
 */
static void RespawnPlayer(Player *player)
{
  if (!player) {
    lm->Log(L_ERROR, "<%s> RespawnPlayer: no player provided.", HSC_MODULE_NAME);
    return;
  }

  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  Target target;
  int prize;

  target.type = T_PLAYER;
  target.u.p = player;

  for (prize = 0; prize < PRIZE_COUNT; ++prize) {
    if (pdata->prizes[prize]) {
      game->GivePrize(&target, prize + 1, pdata->prizes[prize]);
    }
  }
}

/**
 * Reverts a player's settings back to the arena default and frees any memory associated with custom
 * settings.
 *
 * @param *player
 *  The player for whom to revert settings.
 */
static void RevertPlayerOverrides(Player *player)
{
  if (!player) {
    lm->Log(L_ERROR, "<%s> RevertPlayerOverrides: no player provided.", HSC_MODULE_NAME);
    return;
  }

  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  SettingOverride *chain;
  CSAction *csaction, *temp;

  if (overrides_initialized) {
    // Ship overrides
    for (int ship = SHIP_WARBIRD; ship < SHIP_SPEC; ++ship) {
      for (chain = ship_overrides[ship]; chain; chain = chain->next) {
        clientset->PlayerUnoverride(player, chain->orkey);
      }
    }

    // Global overrides
    for (chain = global_overrides; chain; chain = chain->next) {
      clientset->PlayerUnoverride(player, chain->orkey);
    }
  }

  pthread_mutex_lock(&pdata_mutex);

  // Release CSActions and prize states.
  for (csaction = pdata->csaction; csaction;) {
    if (csaction->prizes) {
      free(csaction->prizes);
    }

    temp = csaction;
    csaction = csaction->next;

    free(temp);
  }

  pthread_mutex_unlock(&pdata_mutex);
}

/**
 * Completes the specified player's queued shipset change.
 *
 * @param *player
 *  The player whose queued ship set change should be completed.
 *
 * @return
 *  True if the player had a pending ship set change which was completed successfully; false
 *  otherwise.
 */
static int CompleteShipSetChange(Player *player)
{
  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  int prev;

  if (pdata->pending_shipset_change && (pdata->pending_shipset > -1 && pdata->pending_shipset < HSCORE_MAX_SHIPSETS)) {
    if ((prev = database->setPlayerShipSet(player, pdata->pending_shipset)) != -1) {
      lm->LogP(L_DRIVEL, HSC_MODULE_NAME, player, "Completed queued shipset change (old: %i, new: %i).", prev + 1, pdata->pending_shipset + 1);
      chat->SendMessage(player, "You are now using shipset %i.", pdata->pending_shipset + 1);

      pdata->pending_shipset_change = 0;
      pdata->pending_shipset = -1;

      return 1;
    }
  }

  return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Advisor Hooks & Hacks
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Hack to perform a continuation of the adviser CanChangeToFreq/Ship check, allowing this adviser
 * to pretend it's perpetually at the end of the adviser chain.
 *
 * Impl note:
 *  This type of thing is only necessary because we intend to intercept the request /only/ if every
 *  other adviser were to allow it. The only other alternate (checking CanChangeToFreq) could cause
 *  other advisers to be called more than once, and it changes the state of the is_changing flag.
 *  As neither of these are very good alternatives, I'm opting to, instead, use a hack which relies
 *  on implementation details we're not supposed to know.
 */
static int ContinueAdviserFreqChangeCheck(Player *player, int requested_freq, int is_changing, char *mbuffer, int buffer_len)
{
  LinkedList advisers;
  Link *link;
  Aenforcer *adviser;

  LLInit(&advisers);
  mm->GetAdviserList(A_ENFORCER, player->arena, &advisers);

  int found = 0;
  int result = 1;

  FOR_EACH(&advisers, adviser, link) {
    // Skip checks until we've found our adviser in the list.
    if (!found) {
      found = (adviser == &spawner_freqman_enforcer);
      continue;
    }

    // Continue...
    if (adviser->CanChangeToFreq && !adviser->CanChangeToFreq(player, requested_freq, is_changing, mbuffer, buffer_len)) {
      result = 0;
      break;
    }
  }

  mm->ReleaseAdviserList(&advisers);
  return result;
}

/**
 * See above.
 */
static int ContinueAdviserShipChangeCheck(Player *player, int requested_ship, int is_changing, char *mbuffer, int buffer_len)
{
  LinkedList advisers;
  Link *link;
  Aenforcer *adviser;

  LLInit(&advisers);
  mm->GetAdviserList(A_ENFORCER, player->arena, &advisers);

  int found = 0;
  int result = 1;

  FOR_EACH(&advisers, adviser, link) {
    // Skip checks until we've found our adviser in the list.
    if (!found) {
      found = (adviser == &spawner_freqman_enforcer);
      continue;
    }

    // Continue...
    if (adviser->CanChangeToShip && !adviser->CanChangeToShip(player, requested_ship, is_changing, mbuffer, buffer_len)) {
      result = 0;
      break;
    }
  }

  mm->ReleaseAdviserList(&advisers);
  return result;
}

/**
 * Called when checking if the player can change to the specified freq. Only triggered when the
 * change is passed through the freqman.
 */
static int CanChangeToFreq(Player *player, int requested_freq, int is_changing, char *mbuffer, int buffer_len)
{
  // Check that the player is actually requesting a ship.
  if (player->p_ship == SHIP_SPEC) {
    return 1;
  }

  pthread_mutex_lock(&pdata_mutex);
  int result = 0;

  // Check that the player's ships are loaded.
  if (database->areShipsLoaded(player)) {
    // Check that they can use a ship on the requested freq.
    shipmask_t mask = GetAllowableShips(player, requested_freq, NULL, 0);

    if (mask) {
      // If the player is actively changing, check that the other advisers are okay with the change.
      if (is_changing && ContinueAdviserFreqChangeCheck(player, requested_freq, is_changing, mbuffer, buffer_len)) {
        // If they need a shipset change, do so now.
        CompleteShipSetChange(player);

        // Figure out which ship they'll end up in...
        int ship = player->p_ship;

        if (!SHIPMASK_HAS(ship, mask)) {
          for (ship = SHIP_WARBIRD; !SHIPMASK_HAS(ship, mask) && ship < SHIP_SPEC; ++ship);
        }

        // They're good. Send them new settings and change them later.
        CompileSettingsEx(player, database->getPlayerShipHull(player, ship), requested_freq, ship, NULL, 0);
      }

      result = !is_changing;
    } else {
      if (mbuffer) {
        int shipset = HasPendingShipSetChange(player) ? GetPendingShipSet(player) : database->getPlayerShipSet(player);
        snprintf(mbuffer, buffer_len, "You do not own any hulls on shipset %i. Please use \"?buy ships\" to examine the ship hulls for sale.", shipset + 1);
      }
    }
  } else {
    if (mbuffer) {
      snprintf(mbuffer, buffer_len, "Your ship data is not loaded in this arena. If you just entered, please wait a moment and try again.");
    }
  }

  // Impl note:
  // We always return false here so we can intercept the request.
  pthread_mutex_unlock(&pdata_mutex);
  return result;
}

/**
 * Called when checking if the player can change to the specified ship. Only triggered when the
 * change is passed through the freqman.
 */
static int CanChangeToShip(Player *player, int requested_ship, int is_changing, char *mbuffer, int buffer_len)
{
  // Check that the player wants to go to ship other than spec ship.
  if (requested_ship == SHIP_SPEC) {
    return 1;
  }

  pthread_mutex_lock(&pdata_mutex);
  int result = 0;

  // Check that the player's ships are loaded.
  if (database->areShipsLoaded(player)) {
    // Check that they have the ship on the (potentially pending) shipset.
    shipmask_t mask = GetAllowableShips(player, player->p_freq, NULL, 0);

    if (SHIPMASK_HAS(requested_ship, mask)) {
      // If the player is actively changing, check that the other advisers are okay with the change.
      if (is_changing && ContinueAdviserShipChangeCheck(player, requested_ship, is_changing, mbuffer, buffer_len)) {
        // If they need a shipset change, do so now.
        CompleteShipSetChange(player);

        // If they're currently in spectator mode (and on the spec freq), we need to find a new
        // freq for them...
        int freq = player->p_freq;

        if ((player->p_ship != SHIP_SPEC || freq != player->arena->specfreq) || (freq = freqman->FindEntryFreq(player, is_changing, mbuffer, buffer_len)) != player->arena->specfreq) {
          // They're good. Send them new settings and change them later.
          CompileSettingsEx(player, database->getPlayerShipHull(player, requested_ship), freq, requested_ship, NULL, 0);
        }
      }

      result = !is_changing;
    } else {
      if (mbuffer) {
        int shipset = HasPendingShipSetChange(player) ? GetPendingShipSet(player) : database->getPlayerShipSet(player);
        snprintf(mbuffer, buffer_len, "You do not own a %s hull on shipset %i. Please use \"?buy ships\" to examine the ship hulls for sale.", cfg->SHIP_NAMES[requested_ship], shipset + 1);
      }
    }
  } else {
    if (mbuffer) {
      snprintf(mbuffer, buffer_len, "Your ship data is not loaded in this arena. If you just entered, please wait a moment and try again.");
    }
  }

  // Impl note:
  // We always return false here so we can intercept the request.
  pthread_mutex_unlock(&pdata_mutex);
  return result;
}

/**
 * Called when a module needs the mask of allowed ships on the specified freq.
 */
static shipmask_t GetAllowableShips(Player *player, int frequency, char *mbuffer, int buffer_len)
{
  // Build a mask from the player's (pending) shipset.
  shipmask_t mask = 0;

  // New freq; build a proper shipmask.
  if (database->areShipsLoaded(player)) {
    int shipset = HasPendingShipSetChange(player) ? GetPendingShipSet(player) : database->getPlayerShipSet(player);

    for (int i = SHIP_WARBIRD; i < SHIP_SPEC; ++i) {
      if (database->getPlayerHull(player, i, shipset) || !cfg->GetInt(player->arena->cfg, cfg->SHIP_NAMES[i], "BuyPrice", 0)) {
        mask |= (1 << i);
      }
    }
  } else {
    if (mbuffer) {
      snprintf(mbuffer, buffer_len, "Your ship data is not loaded in this arena. If you just entered, please wait a moment and try again.");
    }
  }

  return mask;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// Event Handlers
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Called on a core player action (enter/leave arena, enter/leave game).
 */
static void OnPlayerAction(Player *player, int action, Arena *arena)
{
  PlayerSpawnData *pdata = PPDATA(player, pdkey);

  pthread_mutex_lock(&pdata_mutex);

  switch (action) {
    case PA_ENTERARENA:
      pdata->managed_player = 1;
      pdata->csaction = NULL;

      pdata->pending_shipset_change = 0;
      pdata->pending_shipset = -1;

      // Player's overrides will be setup upon entering a ship.
      break;

    case PA_LEAVEARENA:
      pdata->managed_player = 0;
      RevertPlayerOverrides(player);
      break;
  }

  pthread_mutex_unlock(&pdata_mutex);
}

/**
 * Called when a player has received their compiled arena settings.
 */
static void OnSettingsReceived(Player *player, int success, void *clos)
{
  PlayerSpawnData *pdata = PPDATA(player, pdkey);
  CSAction *csaction;
  Target target;

  pthread_mutex_lock(&pdata_mutex);

  if ((csaction = pdata->csaction)) {
    if (success) {
      // Check if we need to perform a diff on the spawn prize counts.
      if (csaction->prizes) {
        // target.type = T_PLAYER;
        // target.u.p = player;

        // // Calculate diff...
        // int prize_len = HSC_MIN(PRIZE_COUNT, csaction->prize_len);
        // for (int prize = 0; prize < prize_len; ++prize) {
        //   int count = pdata->prizes[prize] - csaction->prizes[prize];

        //   if (count > 0) {
        //     game->GivePrize(&target, prize + 1, count);
        //   } else if (count < 0) {
        //     game->GivePrize(&target, ~prize, -count);
        //   }
        // }

        // We're done with this now.
        free(csaction->prizes);
        csaction->prizes = NULL;
      }

      // Check if we need to recompile...
      if (csaction->next) {
        // Uh oh. Gotta recompile already.
        csaction = csaction->next;

        free(pdata->csaction);
        pdata->csaction = NULL;

        CompileSettings(player, csaction);
      } else {
        // Update the player's freq and/or ship if necessary.
        if (player->p_freq != csaction->freq || player->p_ship != csaction->ship) {
          game->SetShipAndFreq(player, csaction->ship, csaction->freq);
        } else {
          // Player needs a shipreset for the new stuff to take effect.
        }

        pdata->csaction = NULL;

        DO_CBS(CB_OVERRIDES_RECEIVED, player->arena, OverridesReceivedCallbackFunction, (player, csaction->hull, csaction->freq, csaction->ship));
        free(csaction);
      }
    } else {
      // Settings failed (uh oh). Try again.

      // Impl note:
      // We don't want to free the player's CSAction here, since we're going to be using it again
      // momentarily.
      pdata->csaction = NULL;

      CompileSettings(player, csaction);
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
}

/**
 * Called when a player respawns (after death, *shipreset, etc.).
 */
static void OnPlayerSpawn(Player *player, int reason)
{
  RespawnPlayer(player);
}

/**
 * Called when a player's item count changes.
 */
static void OnItemCountChanged(Player *player, ShipHull *hull, Item *item, InventoryEntry *entry, int new_count, int old_count)
{
  lm->LogP(L_ERROR, HSC_MODULE_NAME, player, "player's item count changed.");

  if (player && database->getPlayerCurrentHull(player) == hull && item && item->resendSets && new_count != old_count) {
    PlayerSpawnData *pdata = PPDATA(player, pdkey);

    // Send new settings with no post-action.
    CompileSettingsEx(player, hull, player->p_freq, player->p_ship, pdata->prizes, PRIZE_COUNT);
  }
}

/**
 * Called when all database items are reloaded.
 */
static void OnItemsReloaded()
{
  // Send new settings to all players.
  PlayerSpawnData *pdata;
  Player *player;
  Link *link;
  ShipHull *hull;

  // Reload settings for managed players.
  pd->Lock();
  database->lock();
  pthread_mutex_lock(&pdata_mutex);

  FOR_EACH_PLAYER(player) {
    pdata = PPDATA(player, pdkey);

    if (pdata->managed_player && player->p_ship != SHIP_SPEC && (hull = database->getPlayerCurrentHull(player))) {
      CompileSettingsEx(player, hull, player->p_freq, player->p_ship, pdata->prizes, PRIZE_COUNT);
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  database->unlock();
  pd->Unlock();
}

/**
 * Called when the module has attached to the specified arena.
 */
static void OnModuleAttach(Arena *arena)
{
  PlayerSpawnData *pdata;
  Player *player;
  Link *link;
  ShipHull *hull;

  // Manage players who are already in the arena
  pd->Lock();
  database->lock();
  pthread_mutex_lock(&pdata_mutex);

  FOR_EACH_PLAYER(player) {
    if (player->arena == arena) {
      pdata = PPDATA(player, pdkey);

      pdata->managed_player = 1;
      pdata->csaction = NULL;

      pdata->pending_shipset_change = 0;
      pdata->pending_shipset = -1;

      if (player->p_ship != SHIP_SPEC && (hull = database->getPlayerCurrentHull(player))) {
        CompileSettingsEx(player, hull, player->p_freq, player->p_ship, NULL, 0);
      }
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  database->unlock();
  pd->Unlock();
}

/**
 * Called when the module has detached from the specified arena.
 */
static void OnModuleDetach(Arena *arena)
{
  PlayerSpawnData *pdata;
  Player *player;
  Link *link;

  // Revert settings for players who are still in the arena
  pd->Lock();
  database->lock();
  pthread_mutex_lock(&pdata_mutex);

  FOR_EACH_PLAYER(player) {
    if (player->arena == arena) {
      pdata = PPDATA(player, pdkey);

      pdata->managed_player = 0;
      RevertPlayerOverrides(player);
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  database->unlock();
  pd->Unlock();
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Command Handlers
////////////////////////////////////////////////////////////////////////////////////////////////////

static helptext_t shipset_command_help =
"Targets: none\n"
"Args: [shipset #]\n"
"Views or changes your current shipset.\n";

static void OnShipSetCommand(const char *command, const char *params, Player *player, const Target *target)
{
  PlayerSpawnData *pdata;

  database->lock();

  if (target->type == T_PLAYER) {
    // Private command
    Player *t = target->u.p;

    if (database->areShipsLoaded(t)) {
      int shipset = database->getPlayerShipSet(t);
      chat->SendMessage(player, "Player %s is currently using shipset %i.", t->name, (shipset + 1));
    } else {
      chat->SendMessage(player, "Ship data has not yet loaded for player %s. Try again in a moment, or advise the player re-enter the arena if this issue persists.", t->name);
    }
  } else {
    // Public command (or another form about which we don't care)
    pdata = PPDATA(player, pdkey);

    if (database->areShipsLoaded(player)) {
      if (params && *params) {
        int shipset = atoi(params) - 1;
        int cur_shipset = database->getPlayerShipSet(player);

        if (shipset != cur_shipset) {
          if (shipset >= 0 && shipset < HSCORE_MAX_SHIPSETS) {

            if (player->p_ship != SHIP_SPEC) {
              pdata->pending_shipset_change = 1;
              pdata->pending_shipset = shipset;

              chat->SendMessage(player, "Your shipset change will take effect on your next freq or ship change.");
            } else {
              if (database->setPlayerShipSet(player, shipset) != -1) {
                chat->SendMessage(player, "You are now using shipset %i.", shipset + 1);
              } else {
                chat->SendMessage(player, "Error: Unable to change to shipset %i.", shipset + 1);
              }
            }
          } else {
            chat->SendMessage(player, "Invalid shipset number. Shipset must be between 1 and %i, inclusive.", HSCORE_MAX_SHIPSETS);
          }
        } else {
          if (pdata->pending_shipset_change) {
            pdata->pending_shipset_change = 0;
            pdata->pending_shipset = -1;

            chat->SendMessage(player, "You are now using shipset %i.", shipset + 1);
          } else {
            chat->SendMessage(player, "You are already using shipset %i.", shipset + 1);
          }
        }
      } else {
        int shipset = database->getPlayerShipSet(player);
        chat->SendMessage(player, "You are currently using shipset %i.", shipset + 1);
      }
    } else {
      chat->SendMessage(player, "Your ship data has not yet loaded. Try again in a moment, or re-enter the arena if this issue persists.");
    }
  }

  database->unlock();
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Interface functionality
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Checks if the specified player's settings are currently managed by this spawner.
 *
 * @param *player
 *  The player for which to check management status.
 *
 * @return
 *  True if the player is managed by this spawner; false otherwise.
 */
static int IsManaged(Player *player)
{
  PlayerSpawnData *pdata;
  return (player && (pdata = PPDATA(player, pdkey))) ? pdata->managed_player : 0;
}

/**
 * Recompiles and resends settings for the specified player if, and only if, they are managed by
 * this spawner.
 *
 * @param *player
 *  The player for which to recompile settings.
 *
 * @return
 *  True if the player is managed and the settings were recompiled and resent; false otherwise.
 */
static int RecompileOverrides(Player *player)
{
  PlayerSpawnData *pdata;
  ShipHull *hull;

  if (IsManaged(player) && player->p_ship != SHIP_SPEC && (hull = database->getPlayerCurrentHull(player))) {
    pdata = PPDATA(player, pdkey);
    return CompileSettingsEx(player, hull, player->p_freq, player->p_ship, pdata->prizes, PRIZE_COUNT);
  }

  return 0;
}

/**
 * Retrieves the current setting value for the specified player. If the
 *
 * @param *player
 *  The player for which to retrieve the current setting value.
 *
 * @param *section
 *  The section in which the setting is located.
 *
 * @param *setting
 *  The setting to retrieve.
 *
 * @param default_value
 *  The default value if the setting cannot be found.
 *
 * @return
 *  The player's current value of the specified setting.
 */
static int GetPlayerOverrideValue(Player *player, const char *section, const char *setting, int default_value)
{
  // Impl note:
  // We can just offload this to the clientset/config using the override key. Much easier/quicker
  // than invoking the advisers and whatnot.

  override_key_t orkey = clientset->GetOverrideKey(section, setting);
  int value = default_value;

  if (IsManaged(player) && player->arena && orkey) {
    if (!clientset->GetPlayerOverride(player, orkey, &value)) {
      value = cfg->GetInt(player->arena->cfg, section, setting, default_value);
    }
  }

  return value;
}

/**
 * Checks if the player has a pending shipset change.
 *
 * @param *player
 *  A pointer to the player for which to check for a pending shipset change.
 *
 * @return
 *  True if the player has a pending shipset change; false otherwise.
 */
static int HasPendingShipSetChange(Player *player)
{
  PlayerSpawnData *pdata;
  return (player && (pdata = PPDATA(player, pdkey)) && pdata->managed_player) ? pdata->pending_shipset_change : 0;
}

/**
 * Retrieves the players pending shipset. If the player does not have a pending shipset
 * change, this method returns -1.
 *
 * @param *player
 *  A pointer to the player for which to check for a pending shipset change.
 *
 * @return
 *  The pending shipset for the specified player, or -1 if the player is invalid or does not
 *  have a pending shipset change.
 */
static int GetPendingShipSet(Player *player)
{
  PlayerSpawnData *pdata;
  return (player && (pdata = PPDATA(player, pdkey)) && pdata->managed_player) ? pdata->pending_shipset : -1;
}

/**
 * Registers the prize count override callback for the given prize.
 *
 * @param *callback
 *  The callback to execute when retrieving the initial count for the specified prize.
 *
 * @param *arena
 *  The arena in which the callback will be registered. May be set to ALLARENAS.
 *
 * @param prize
 *  The prize for which to register the callback.
 *
 * @param *exdata
 *  Extra/extended data to pass to the callback. May be null.
 *
 * @return
 *  True if the callback was registered successfully; false otherwise.
 */
static int RegisterPrizeCountCallback(PrizeCountCallbackFunction callback, Arena *arena, int prize, void *exdata)
{
  if (!callback || prize < 1 || prize >= PRIZE_COUNT) {
    return 0;
  }

  pthread_mutex_lock(&pdata_mutex);
  int result = 0;

  CallbackEntry *entry = BuildPrizeCountCallbackEntry(callback, arena, prize, exdata);

  if (entry) {
    int index = prize - 1;
    CallbackEntry *chain = prizecount_callbacks[index];

    if (chain) {
      while (chain->next) {
        chain = chain->next;
      }

      chain->next = entry;
    } else {
      prizecount_callbacks[index] = entry;
    }

    result = 1;
  }

  pthread_mutex_unlock(&pdata_mutex);
  return result;
}

/**
 * Deregisters the specified prize count callback if it has been previously registered.
 *
 * @param *callback
 *  The callback to deregister.
 *
 * @param *arena
 *  The arena from which the callback will be deregistered. If this is set to ALLARENAS, the
 *  callback will be deregistered from all registered arenas.
 *
 * @param prize
 *  The prize for which the callback has been registered.
 *
 * @return
 *  The number of callbacks removed as a result of this operation.
 */
static int DeregisterPrizeCountCallback(PrizeCountCallbackFunction callback, Arena *arena, int prize)
{
  if (!callback || prize < 1 || prize >= PRIZE_COUNT) {
    return 0;
  }

  pthread_mutex_lock(&pdata_mutex);
  int result = 0;

  u32 cbkey = (u32) prize;
  int index = prize - 1;

  CallbackEntry *temp;
  CallbackEntry *prev = NULL;
  CallbackEntry *chain = prizecount_callbacks[index];

  while (chain) {
    if (chain->key == cbkey && (chain->callback == callback) && (chain->arena == arena || arena == ALLARENAS)) {
      // Remove callback from the chain.
      if (prev) {
        prev->next = chain->next;
      } else {
        prizecount_callbacks[index] = chain->next;
      }

      // Move pointers
      temp = chain;
      chain = chain->next;

      // Free resources.
      free(temp);
      ++result;
    } else {
      prev = chain;
      chain = chain->next;
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  return result;
}


/**
 * Registers the specified setting override callback for the given setting.
 *
 * @param *arena
 *  The arena in which the callback will be registered. May be set to ALLARENAS.
 *
 * @param *callback
 *  The callback to call when retrieving the value for the specified setting. Will be called after
 *  global advisers.
 *
 * @param *section
 *  The section in which the setting is found.
 *
 * @param *setting
 *  The setting to override.
 *
 * @param *exdata
 *  Extra/extended data to pass to the callback. May be null.
 *
 * @return
 *  True if the callback was registered successfully; false otherwise.
 */
static int RegisterOverrideCallback(SettingOverrideCallbackFunction callback, Arena *arena, const char *section, const char *setting, void *exdata)
{
  if (!callback || !section || !setting) {
    return 0;
  }

  pthread_mutex_lock(&pdata_mutex);

  int result = 0;
  override_key_t orkey = clientset->GetOverrideKey(section, setting);

  if (orkey) {
    CallbackEntry *entry = BuildOverrideCallbackEntry(callback, arena, orkey, exdata);

    if (entry) {
      int index = entry->key % OVERRIDE_CALLBACK_BUCKET_COUNT;
      CallbackEntry *chain = override_callbacks[index];

      if (chain) {
        // Needs to be added to the end of the chain.
        while (chain->next) {
          chain = chain->next;
        }

        chain->next = entry;
      } else {
        // First callback in this bucket.
        override_callbacks[index] = entry;
      }

      result = 1;
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  return result;
}

/**
 * Deregisters the specified setting override callback if it has been previously registered.
 *
 * @param *callback
 *  The callback to deregister.
 *
 * @param *arena
 *  The arena from which the callback will be deregistered. If this is set to ALLARENAS, the
 *  callback will be deregistered from all registered arenas.
 *
 * @param *section
 *  The section in which the setting is found.
 *
 * @param *setting
 *  The overridden setting.
 *
 * @return
 *  The number of callbacks removed as a result of this operation.
 */
static int DeregisterOverrideCallback(SettingOverrideCallbackFunction callback, Arena *arena, const char *section, const char *setting)
{
  if (!callback || !section || !setting) {
    return 0;
  }

  pthread_mutex_lock(&pdata_mutex);
  int result = 0;

  override_key_t orkey = clientset->GetOverrideKey(section, setting);

  if (orkey) {
    u32 cbkey = (u32) orkey;
    int index = orkey % OVERRIDE_CALLBACK_BUCKET_COUNT;

    CallbackEntry *temp;
    CallbackEntry *prev = NULL;
    CallbackEntry *chain = override_callbacks[index];

    while (chain) {
      if (chain->key == cbkey && (chain->callback == callback) && (chain->arena == arena || arena == ALLARENAS)) {
        // Remove callback from the chain.
        if (prev) {
          prev->next = chain->next;
        } else {
          override_callbacks[index] = chain->next;
        }

        // Move pointers
        temp = chain;
        chain = chain->next;

        // Free resources.
        free(temp);
        ++result;
      } else {
        prev = chain;
        chain = chain->next;
      }
    }
  }

  pthread_mutex_unlock(&pdata_mutex);
  return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Module Initialization
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
    ml        = mm->GetInterface(I_MAINLOOP, ALLARENAS);
    net       = mm->GetInterface(I_NET, ALLARENAS);
    game      = mm->GetInterface(I_GAME, ALLARENAS);
    clientset = mm->GetInterface(I_CLIENTSET, ALLARENAS);
    pd        = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    freqman   = mm->GetInterface(I_FREQMAN, ALLARENAS);
    chat      = mm->GetInterface(I_CHAT, ALLARENAS);
    cmd       = mm->GetInterface(I_CMDMAN, ALLARENAS);
    cfg       = mm->GetInterface(I_CONFIG, ALLARENAS);

    items     = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    database  = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

    return mm && (lm && ml && net && game && clientset && pd && freqman && chat && cmd && cfg) && (items && database);
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
    mm->ReleaseInterface(ml);
    mm->ReleaseInterface(net);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(clientset);
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(freqman);
    mm->ReleaseInterface(chat);
    mm->ReleaseInterface(cmd);
    mm->ReleaseInterface(cfg);

    mm->ReleaseInterface(items);
    mm->ReleaseInterface(database);

    mm = NULL;
  }
}


EXPORT const char info_hscore_spawner2[] = "v2.0 Chris \"Ceiu\" Rog <ceiu@cericlabs.com>";

EXPORT int MM_hscore_spawner2(int action, Imodman *modman, Arena *arena)
{
  switch (action) {
    case MM_LOAD:
      if (!GetInterfaces(modman, arena)) {
        printf("<%s> Could not acquire required interfaces.\n", HSC_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate pdata
      pdkey = pd->AllocatePlayerData(sizeof(PlayerSpawnData));
      if (pdkey == -1) {
        printf("<%s> Unable to allocate per-player data.\n", HSC_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to allocate per-player data.", HSC_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      // Allocate & configure pdata mutex.
      if (!pdata_mutexattr_status && !(pdata_mutexattr_status = !pthread_mutexattr_init(&pdata_mutexattr))) {
        printf("<%s> Unable to initialize pdata_mutexattr.\n", HSC_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to initialize pdata_mutexattr.", HSC_MODULE_NAME);
        ReleaseInterfaces();
        break;
      }

      pthread_mutexattr_settype(&pdata_mutexattr, PTHREAD_MUTEX_RECURSIVE);

      if (!pdata_mutex_status && !(pdata_mutex_status = !pthread_mutex_init(&pdata_mutex, &pdata_mutexattr))) {
        printf("<%s> Unable to initialize pdata_mutex.\n", HSC_MODULE_NAME);
        lm->Log(L_ERROR, "<%s> Unable to initialize pdata_mutex.", HSC_MODULE_NAME);

        pdata_mutexattr_status = pthread_mutexattr_destroy(&pdata_mutexattr);
        ReleaseInterfaces();
        break;
      }

      // Impl note:
      // As long as hscore_database is a global module, this callback needs to be registered for all
      // arenas. If/when it is changed to attach to specific arenas, this callback should be changed
      // accordingly.
      mm->RegCallback(CB_HS_ITEMRELOAD, OnItemsReloaded, ALLARENAS);

      // Initialize overrides...
      InitializeOverrides();

      // Register our interface globally.
      mm->RegInterface(&spawner_interface, ALLARENAS);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_ATTACH:
      // Register callbacks & interfaces
      mm->RegAdviser(&spawner_freqman_enforcer, arena);

      // Register events
      mm->RegCallback(CB_PLAYERACTION, OnPlayerAction, arena);
      mm->RegCallback(CB_SPAWN, OnPlayerSpawn, arena);
      mm->RegCallback(CB_ITEM_COUNT_CHANGED, OnItemCountChanged, arena);

      // Register commands
      cmd->AddCommand("shipset", OnShipSetCommand, arena, shipset_command_help);

      // Do post-attach stuff
      OnModuleAttach(arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_DETACH:
      // Do pre-detach stuff
      OnModuleDetach(arena);

      mm->UnregAdviser(&spawner_freqman_enforcer, arena);

      mm->UnregCallback(CB_PLAYERACTION, OnPlayerAction, arena);
      mm->UnregCallback(CB_SPAWN, OnPlayerSpawn, arena);
      mm->UnregCallback(CB_ITEM_COUNT_CHANGED, OnItemCountChanged, arena);

      return MM_OK;

    //////////////////////////////////////////////////

    case MM_UNLOAD:
      if (mm->UnregInterface(&spawner_interface, ALLARENAS)) {
        lm->Log(L_ERROR, "<%s> Unable to unregister spawner interface.", HSC_MODULE_NAME);
        break;
      }

      // Impl note:
      // See the above comment on the associated RegCallback call.
      mm->UnregCallback(CB_HS_ITEMRELOAD, OnItemsReloaded, ALLARENAS);

      pd->FreePlayerData(pdkey);

      if (pdata_mutex_status && (pdata_mutex_status = pthread_mutex_destroy(&pdata_mutex))) {
        lm->Log(L_ERROR, "<%s> Unable to destroy mutex data.", HSC_MODULE_NAME);
        break;
      }

      if (pdata_mutexattr_status && (pdata_mutexattr_status = pthread_mutexattr_destroy(&pdata_mutexattr))) {
        lm->Log(L_ERROR, "<%s> Unable to destroy mutex attribute data.", HSC_MODULE_NAME);
        break;
      }

      ReleaseOverrides();
      ReleaseInterfaces();

      return MM_OK;
  }

  return MM_FAIL;
}
