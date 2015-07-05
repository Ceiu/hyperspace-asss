#ifndef HSCORE_ITEMS_H
#define HSCORE_ITEMS_H

/* pyinclude: hscore/hscore_types.h */
/* pyinclude: hscore/hscore_items.h */
/* pytype: opaque, Item *, item */
/* pytype: opaque, ItemType *, type */
/* pytype: opaque, ShipHull *, hull */

#define I_HSCORE_ITEMS "hscore_items-12"

//callback
#define CB_EVENT_ACTION "eventaction"
#define CB_TRIGGER_EVENT "triggerevent-2"
#define CB_AMMO_ADDED "ammoaddded-2"
#define CB_AMMO_REMOVED "ammoremoved-2"
#define CB_ITEMS_CHANGED "hscb_items_changed-2"

//callback function prototype
typedef void (*eventActionFunction)(Player *p, int eventID);
typedef void (*triggerEventFunction)(Player *p, Item *triggerItem, ShipHull *hull, const char *eventName);
typedef void (*ammoAddedFunction)(Player *p, ShipHull *hull, Item *ammoUser); //warnings: cache is out of sync
typedef void (*ammoRemovedFunction)(Player *p, ShipHull *hull, Item *ammoUser); //warnings: cache is out of sync
typedef void (*ItemsChangedFunction)(Player *p, ShipHull *hull);


typedef struct Ihscoreitems
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	int (*getItemCount)(Player *p, Item *item, int ship);
	/* pyint: player, item, int -> int */

	int (*getItemCountOnShipSet)(Player *p, Item *item, int ship, int shipset);
	/* pyint: player, item, int, int -> int */

	int (*getItemCountOnHull)(Player *p, Item *item, ShipHull *hull);
	/* pyint: player, item, hull -> int */


	int (*addItem)(Player *p, Item *item, int ship, int amount);
	/* pyint: player, item, int, int -> int */

	int (*addItemToShipSet)(Player *p, Item *item, int ship, int shipset, int amount);
	/* pyint: player, item, int, int, int -> int */

	int (*addItemToHull)(Player *p, Item *item, ShipHull *hull, int amount);
	/* pyint: player, item, hull, int -> int */


	int (*addItemCheckLimits)(Player *p, Item *item, int ship, int amount);
	/* pyint: player, item, int, int -> int */

	int (*addItemToShipSetCheckLimits)(Player *p, Item *item, int ship, int shipset, int amount);
	/* pyint: player, item, int, int, int -> int */

	int (*addItemToHullCheckLimits)(Player *p, Item *item, ShipHull *hull, int amount);
	/* pyint: player, item, hull, int -> int */


	Item * (*getItemByName)(const char *name, Arena *arena);
	/* pyint: string, arena -> item */

	Item * (*getItemByPartialName)(const char *name, Arena *arena);
	/* pyint: string, arena -> item */


	int (*getPropertySum)(Player *p, int ship, const char *prop, int def); //properties ARE case sensitive
	/* pyint: player, int, string, int -> int */

	int (*getPropertySumOnShipSet)(Player *p, int ship, int shipset, const char *prop, int def); //properties ARE case sensitive
	/* pyint: player, int, int, string, int -> int */

	int (*getPropertySumOnHull)(Player *p, ShipHull *hull, const char *prop, int def); //properties ARE case sensitive
	/* pyint: player, hull, string, int -> int */


	void (*triggerEvent)(Player *p, int ship, const char *event);
	/* pyint: player, int, string -> void */

	void (*triggerEventOnShipSet)(Player *p, int ship, int shipset, const char *event);
	/* pyint: player, int, int, string -> void */

	void (*triggerEventOnHull)(Player *p, ShipHull *hull, const char *event);
	/* pyint: player, hull, string -> void */

	void (*triggerEventOnItem)(Player *p, Item *item, int ship, const char *event);
	/* pyint: player, item, int, string -> void */

	void (*triggerEventOnShipSetItem)(Player *p, Item *item, int ship, int shipset, const char *event);
	/* pyint: player, item, int, int, string -> void */

	void (*triggerEventOnHullItem)(Player *p, Item *item, ShipHull *hull, const char *event);
	/* pyint: player, item, hull, string -> void */


	int (*getFreeItemTypeSpots)(Player *p, ItemType *type, int ship);
	/* pyint: player, type, int -> int */

	int (*getFreeItemTypeSpotsOnShipSet)(Player *p, ItemType *type, int ship, int shipset);
	/* pyint: player, type, int, int -> int */

	int (*getFreeItemTypeSpotsOnHull)(Player *p, ItemType *type, ShipHull *hull);
	/* pyint: player, type, hull -> int */


	int (*hasItemsLeftOnShip)(Player *p, int ship);
	/* pyint: player, int -> int */

	int (*hasItemsLeftOnShipSetShip)(Player *p, int ship, int shipset);
	/* pyint: player, int, int -> int */

	int (*hasItemsLeftOnHull)(Player *p, ShipHull *hull);
	/* pyint: player, hull -> int */

	void (*recalculateEntireCache)(Player *p, int ship);
	void (*recalculateEntireCacheForShipSet)(Player *p, int ship, int shipset);
	void (*recalculateEntireCacheForHull)(Player *p, ShipHull *hull);
} Ihscoreitems;

#define A_HSCORE_ITEMS "hscore_items-11"
typedef struct Ahscoreitems
{
	ADVISER_HEAD_DECL

	/*
	Called after regular checks. Return 1 to allow the
	target to be granted the item(s). Return 0 to deny.
	Send an appropriate message to the player *p using
	the grantitem command if you deny it. Note that if
	the granting player uses the 'ignore' flag (-i),
	this function is never called.
	*/

	int (*CanGrantItem)(Player *p, Player *t, Item *item, int ship, int shipset, int count);
} Ahscoreitems;

#endif //HSCORE_ITEMS_H
