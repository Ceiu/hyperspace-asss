#ifndef HSCORE_DATABASE_H
#define HSCORE_DATABASE_H

/* pyinclude: hscore/hscore_types.h */
/* pyinclude: hscore/hscore_database.h */

#define I_HSCORE_DATABASE "hscore_database-6"

/**
 * The maximum number of shipsets players are allowed. Must be at least 1. May go as high as memory
 * and sanity limitations will allow.
 */
#define HSCORE_MAX_SHIPSETS 3

/**
 * The maximum number of hulls allowed. Must be equal to HSCORE_MAX_SHIPSETS * 8 or bad things will
 * start happening.
 */
#define HSCORE_MAX_HULLS (HSCORE_MAX_SHIPSETS << 3)





#define CB_ITEM_COUNT_CHANGED "itemcount-3"
//NOTE: *entry may be NULL if newCount is 0.
//called with lock held
typedef void (*ItemCountChanged)(Player *p, ShipHull *hull, Item *item, InventoryEntry *entry, int newCount, int oldCount);

#define CB_HS_ITEMRELOAD "hs-itemreload-1"
typedef void (*HSItemReload)(void);

#define CB_SHIPS_LOADED "shipsloaded-1"
typedef void (*ShipsLoaded)(Player *p);

#define CB_SHIPSET_CHANGED "hscore_shipset_changed-1"
typedef void (*ShipSetChanged)(Player *p, int oldshipset, int newshipset);

#define CB_SHIP_ADDED "shipadded-2"
typedef void (*ShipAdded)(Player *p, int ship, int shipset);

typedef struct Ihscoredatabase
{
  INTERFACE_HEAD_DECL
  /* pyint: use */

  /**
   * Gets the player's current wallet ID. Primarily used for performing SQL transactions.
   *
   * @param *p
   *  The player for which to retrieve the wallet ID.
   *
   * @return
   *  The player's current wallet id, or -1 if the player's wallet has not been loaded.
   */
  int (*getPlayerWalletId)(Player *p);

  /**
   * Checks if the players wallet (money, exp, etc.) has been loaded for their current arena.
   *
   * @param *p
   *	The player to check.
   *
   * @return
   *	True if the player's wallet has been loaded; false otherwise.
   */
  int (*isWalletLoaded)(Player *p);

  /**
   * Checks if the players ships has been loaded for their current arena.
   *
   * @param *p
   *	The player to check.
   *
   * @return
   *	True if the player's ships has been loaded; false otherwise.
   */
	int (*areShipsLoaded)(Player *p);


	LinkedList * (*getItemList)();
	LinkedList * (*getStoreList)(Arena *arena);
	LinkedList * (*getCategoryList)(Arena *arena);
	LinkedList * (*getShipPropertyList)(Arena *arena, int ship);

	void (*lock)();
	void (*unlock)();

	//call whenever you want an item to be written back into SQL
	//a newCount of 0 will delete the item from the database
	void (*updateItem)(Player *p, int ship, Item *item, int newCount, int newData);
  void (*updateItemOnShipSet)(Player *p, int ship, int shipset, Item *item, int newCount, int newData);
  void (*updateItemOnHull)(Player *p, ShipHull *hull, Item *item, int newCount, int newData);
  void (*updateInventory)(Player *p, int ship, InventoryEntry *entry, int newCount, int newData);
  void (*updateInventoryOnShipSet)(Player *p, int ship, int shipset, InventoryEntry *entry, int newCount, int newData);
  void (*updateInventoryOnHull)(Player *p, ShipHull *hull, InventoryEntry *entry, int newCount, int newData);

  void (*addShip)(Player *p, int ship);
  void (*addShipToShipSet)(Player *player, int ship, int shipset);
	void (*removeShip)(Player *p, int ship); //NOTE: will destroy all items on the ship
	void (*removeShipFromShipSet)(Player *player, int ship, int shipset);



  /**
   * @todo:
   * Add functions to add money/exp to specific arena identifiers.
   */

	void (*addMoney)(Player *p, MoneyType type, int amount);
	/* pyint: player, int, int -> void */

	void (*setMoney)(Player *p, MoneyType type, int amount);
	/* pyint: player, int, int -> void */

	int (*getMoney)(Player *p);
	/* pyint: player -> int */

	int (*getMoneyType)(Player *p, MoneyType type); //used only for /?money -d
	/* pyint: player, int -> int */

	void (*addExp)(Player *p, int amount);
	/* pyint: player, int -> void */

	void (*setExp)(Player *p, int amount);
	/* pyint: player, int -> void */

	int (*getExp)(Player *p);
	/* pyint: player -> int */




	/**
	 * Retrieves the player's current shipset. If the player is invalid, this method returns -1.
	 *
	 * @param *player
	 *  A pointer to the player for which to retrieve the shipset.
	 *
	 * @return
	 *  The shipset for the specified player, or -1 if the player is invalid.
	 */
  int (*getPlayerShipSet)(Player *player);

	/**
	 * Sets the player's current shipset, returning the previous shipset. If the player or
	 * shipset is invalid, this method returns -1.
	 *
	 * @param *player
	 *  A pointer to the player for which to set the shipset.
	 *
	 * @param shipset
	 *  The shipset to set for the specified player. Must be in the range:
	 *  [0, HSCORE_MAX_SHIPSETS)
	 *
	 * @return
	 *  The previous shipset for the specified player, or -1 if the player or shipset was
	 *  invalid.
	 */
  int (*setPlayerShipSet)(Player *player, int shipset);

	/**
	 * Retrieves the current hull associated with specified ship and shipset. If the player
	 * does not exist, or the ship or shipset is out of range, or the player does not have the
	 * specified hull, this method returns null.
	 *
	 * @param *player
	 *  A pointer to the player for which to retrieve a hull.
	 *
	 * @param ship
	 *  The ship for which to retrieve a hull. Must be a valid ship value (0-7, inclusive).
	 *
	 * @param shipset
	 *  The shipset from which to retrieve a hull. Must be a valid shipset for the specified
	 *  player.
	 *
	 * @return
	 *  A pointer to the ShipHull specified, or null if no such hull exists.
	 */
  ShipHull* (*getPlayerHull)(Player *player, int ship, int shipset);

	/**
	 * Retrieves the current hull associated with specified ship in the player's current
	 * shipset. If the player does not exist, the ship is out of range, or the player does not
	 * have the specified hull, this method returns null.
	 *
	 * @param *player
	 *  A pointer to the player for which to retrieve a hull.
	 *
	 * @param ship
	 *  The ship for which to retrieve a hull. Must be a valid ship value (0-7, inclusive).
	 *
	 * @return
	 *  A pointer to the ShipHull for the specified ship, or null if no such hull exists.
	 */
  ShipHull* (*getPlayerShipHull)(Player *player, int ship);

	/**
	 * Retrieves the current hull for the specified player. If the player does not have a hull
	 * for their current ship, this method returns null.
	 *
	 * @param *player
	 *  A pointer to the player for which to retrieve a hull.
	 *
	 * @return
	 *  A pointer to the player's current ShipHull, or null if no such hull exists.
	 */
  ShipHull* (*getPlayerCurrentHull)(Player *player);
} Ihscoredatabase;

#endif //HSCORE_DATABASE_H
