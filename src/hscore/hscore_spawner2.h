/**
 * hscore_spawner2.h
 *
 * @author Chris "Ceiu" Rog <ceiu@cericlabs.com>
 */
#ifndef HSCORE_SPAWNER2_H
#define HSCORE_SPAWNER2_H

/**
 * Callback identifier for the overrides requested callback. See the documentation associated with
 * the OverridesRequestedCallback function typedef below for details.
 */
#define CB_OVERRIDES_REQUESTED "hscs_overrides_requested-1"

/**
 * Callback identifier for the overrides received callback. See the documentation associated with
 * the OverridesReceivedCallback function typedef below for details.
 */
#define CB_OVERRIDES_RECEIVED "hscs_overrides_received-1"

/**
 * Registerable callback for retrieving setting overrides. Called when the spawner is compiling
 * settings overrides for the specified player.
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
 * @param *section
 *  The section in which the setting is defined.
 *
 * @param *setting
 *  The setting to override.
 *
 * @param init_value
 *  The initial value for the setting.
 *
 * @param *exdata
 *  The extra data provided during callback registration.
 *
 * @return
 *  The new value for the setting. This value should be clamped to known safe values.
 */
typedef int (*SettingOverrideCallbackFunction)(Player *player, ShipHull *hull, int freq, int ship, const char *section, const char *setting, int init_value, void *exdata);

/**
 * Called when the spawner needs the number of the specified prize the player should receive on
 * spawn.
 *
 * @param *player
 *  The player to receive the prizes.
 *
 * @param *hull
 *  The ship hull from which to determine prize count.
 *
 * @param freq
 *  The frequency on which the prize counts will be valid.
 *
 * @param ship
 *  The ship on which the prize counts will be valid.
 *
 * @param prize
 *  The prize for which to retrieve the count.
 *
 * @param init_count
 *  The number of the specified prize the player is currently set to receive.
 *
 * @param *exdata
 *  The extra data provided during callback registration.
 *
 * @return
 *  The number of the specified prize the player should receive upon spawn. Returned value should
 *  be clamped on [0, 255].
 */
typedef int (*PrizeCountCallbackFunction)(Player *player, ShipHull *hull, int freq, int ship, int prize, int init_count, void *exdata);

/**
 * Called before a compiling new setting overrides for the specified player.
 *
 * Note:
 *  The player will have be assigned to the given frequency and ship once they receive the settings.
 *
 * @param *player
 *  The player that requested the setting overrides.
 *
 * @param *hull
 *  The hull from which the settings were derived.
 *
 * @param freq
 *  The freq on which the settings are valid.
 *
 * @param ship
 *  The ship on which the settings are valid.
 */
typedef void (*OverridesRequestedCallbackFunction)(Player *player, ShipHull *hull, int freq, int ship);

/**
 * Called after a player has received overridden settings.
 *
 * Note:
 *  The player will have been assigned to the given frequency and ship by the time this override is
 *  called.
 *
 * @param *player
 *  The player that received the settings.
 *
 * @param *hull
 *  The hull from which the settings were derived.
 *
 * @param freq
 *  The freq on which the settings are valid.
 *
 * @param ship
 *  The ship on which the settings are valid.
 */
typedef void (*OverridesReceivedCallbackFunction)(Player *player, ShipHull *hull, int freq, int ship);



// Interface
#define I_HSCORE_SPAWNER2 "hscore_spawner2_interface-1"
typedef struct IHSCoreSpawner
{
	INTERFACE_HEAD_DECL

  /**
   * Checks if the specified player's settings are currently managed by this spawner.
   *
   * @param *player
   *  The player for which to check management status.
   *
   * @return
   *  True if the player is managed by this spawner; false otherwise.
   */
  int (*isManaged)(Player *player);

  /**
   * Recompiles and resends settings for the specified player if, and only if, they are managed by
   * this spawner and currently in a ship. If the player is not in a ship (or not managed), this
   * function does nothing.
   *
   * @param *player
   *  The player for which to recompile settings.
   *
   * @return
   *  True if the player is managed and the settings were recompiled and resent; false otherwise.
   */
  int (*recompileOverrides)(Player *player);

  /**
   * Retrieves the current setting value for the specified player. If the player is not managed by
   * this spawner, this function returns the default value.
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
  int (*getPlayerOverrideValue)(Player *player, const char *section, const char *setting, int default_value);

  /**
   * Checks if the player has a pending shipset change.
   *
   * @param *player
   *  A pointer to the player for which to check for a pending shipset change.
   *
   * @return
   *  True if the player has a pending shipset change; false otherwise.
   */
  int (*hasPendingShipSetChange)(Player *player);

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
  int (*getPendingShipSet)(Player *player);

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
  int (*registerPrizeCountCallback)(PrizeCountCallbackFunction callback, Arena *arena, int prize, void *exdata);

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
   *  True if the callback was deregistered successfully; false otherwise.
   */
  int (*deregisterPrizeCountCallback)(PrizeCountCallbackFunction callback, Arena *arena, int prize);

  /**
   * Registers the specified setting override callback for the given setting.
   *
   * @param *callback
   *  The callback to execute when retrieving the value for the specified setting.
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
   *  True if the callback was registered successfully; false otherwise.
   */
  int (*registerOverrideCallback)(SettingOverrideCallbackFunction callback, Arena *arena, const char *section, const char *setting, void *exdata);

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
   *  True if the callback was deregistered successfully; false otherwise.
   */
  int (*deregisterOverrideCallback)(SettingOverrideCallbackFunction callback, Arena *arena, const char *section, const char *setting);
} IHSCoreSpawner;


#endif //HSCORE_SPAWNER2_H
