#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_mysql.h"
#include "hscore_database.h"

typedef struct PerPlayerData
{
    int money;
    int moneyType[MONEY_TYPE_COUNT];

    int exp;

    // @todo:
    // Eventually replace this with a hashtable so we don't have a predefined
    // number of ships per player (or a bunch of wasted pointers/memory).
    // 0-7 = ship set 1, 8-15 = ship set 2, 16-23 = ship set 3.
    ShipHull * hull[HSCORE_MAX_HULLS];

    // The ship set we're currently using. The hull for the player's current
    // ship items is PS + (S * 8) where S is the value of this variable and
    // PS is the player's current ship.
    int shipset;

    int id; //MySQL use
    int walletLoaded; //internal use only
    const char *warena; // Arena identifier

    int shipsLoaded; //internal use only
    const char *sarena; // Arena identifier (in case we desynch)

    long uniqueID; //internal use only (some kind of documentation would have been nice...)
} PerPlayerData;

typedef struct PerArenaData
{
  LinkedList storeList;
  LinkedList categoryList;
  LinkedList shipPropertyLists[8];
} PerArenaData;


//modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Iconfig *cfg;
local Icmdman *cmd;
local Ihscoremysql *mysql;
local Iarenaman *aman;
local Iplayerdata *pd;
local Imainloop *ml;

//local prototypes
local int hash_enum_remove(const char *key, void *val, void *d);
local PerArenaData * getPerArenaData(Arena *arena);
local Item * getItemByID(int id);
local ItemType * getItemTypeByID(int id);
local const char * getArenaIdentifier(Arena *arena);
local void LinkAmmo();
local void loadPropertiesQueryCallback(int status, db_res *result, void *passedData);
local void loadEventsQueryCallback(int status, db_res *result, void *passedData);
local void loadItemsQueryCallback(int status, db_res *result, void *passedData);
local void loadItemTypeAssocQueryCallback(int status, db_res *result, void *passedData);
local void loadItemTypesQueryCallback(int status, db_res *result, void *passedData);
local void loadPlayerWalletQueryCallback(int status, db_res *result, void *passedData);
local void loadPlayerShipItemsQueryCallback(int status, db_res *result, void *passedData);
local void loadPlayerShipsQueryCallback(int status, db_res *result, void *passedData);
local void loadStoreItemsQueryCallback(int status, db_res *result, void *passedData);
local void loadArenaStoresQueryCallback(int status, db_res *result, void *passedData);
local void loadCategoryItemsQueryCallback(int status, db_res *result, void *passedData);
local void loadArenaCategoriesQueryCallback(int status, db_res *result, void *passedData);
local void loadShipPropertyListsQueryCallback(int status, db_res *result, void *passedData);
local void InitPerPlayerData(Player *p);
local void InitPerArenaData(Arena *arena);
local void UnloadPlayerWallet(Player *p);
local void UnloadPlayerShip(ShipHull *ship);
local void UnloadPlayerShips(Player *p);
local void UnloadCategoryList(Arena *arena);
local void UnloadStoreList(Arena *arena);
local void UnloadShipPropertyLists(Arena *arena);
local void UnloadItemListEnumCallback(const void *ptr);
local void UnloadItemList();
local void UnloadItemTypeList();
local void UnloadAllPerArenaData();
local void UnloadAllPerPlayerData();
local void LoadPlayerWallet(Player *p, Arena *arena);
local void LoadPlayerShipItems(Player *p, Arena *arena);
local void LoadPlayerShips(Player *p, Arena *arena);
local void LoadCategoryItems(Arena *arena);
local void LoadCategoryList(Arena *arena);
local void LoadStoreItems(Arena *arena);
local void LoadStoreList(Arena *arena);
local void LoadShipPropertyLists(Arena *arena);
local void LoadEvents();
local void LoadProperties();
local void LoadItemList();
local void LoadItemTypeList();
local void AssociateItemTypes();
local void StorePlayerWallet(Player *p);
local void StorePlayerShips(Player *p, Arena *arena);
local void StoreAllPerPlayerData();

//interface prototypes
local int getPlayerWalletId(Player *p);
local int isWalletLoaded(Player *p);
local int areShipsLoaded(Player *p);
local LinkedList * getItemList();
local LinkedList * getStoreList(Arena *arena);
local LinkedList * getCategoryList(Arena *arena);
local void lock();
local void unlock();
local void updateItem(Player *p, int ship, Item *item, int newCount, int newData);
local void updateItemOnShipSet(Player *p, int ship, int shipset, Item *item, int newCount, int newData);
local void updateItemOnHull(Player *p, ShipHull *hull, Item *item, int newCount, int newData);
local void updateInventory(Player *p, int ship, InventoryEntry *entry, int newCount, int newData);
local void updateInventoryOnShipSet(Player *p, int ship, int shipset, InventoryEntry *entry, int newCount, int newData);
local void updateInventoryOnHull(Player *p, ShipHull *hull, InventoryEntry *entry, int newCount, int newData);
local void addShip(Player *p, int ship);
local void addShipToShipSet(Player *p, int ship, int shipset);
local void removeShip(Player *p, int ship);
local void removeShipFromShipSet(Player *p, int ship, int shipset);
local PerPlayerData *getPerPlayerData(Player *p);
local int getPlayerShipSet(Player *p);
local int setPlayerShipSet(Player *p, int shipset);
local ShipHull* getPlayerHull(Player *p, int ship, int shipset);
local ShipHull* getPlayerShipHull(Player *p, int ship);
local ShipHull* getPlayerCurrentHull(Player *p);

//"unique" player ids
local long currentUniqueID = 0;
typedef struct PlayerReference
{
    int pid;
    long uniqueID;
    Arena *arena;
} PlayerReference;

//keys
local int playerDataKey;
local int arenaDataKey;

//lists
local LinkedList itemList;
local LinkedList itemTypeList;

//mutex
local pthread_mutexattr_t db_mutex_attr;
local pthread_mutex_t db_mutex;


//+-------------------------+
//|                         |
//| Miscellaneous Functions |
//|                         |
//+-------------------------+
local int hash_enum_remove(const char *key, void *val, void *d)
{
        return 1;
}

//+-------------------------+
//|                         |
//|  Data Access Functions  |
//|                         |
//+-------------------------+

local PerArenaData * getPerArenaData(Arena *arena)
{
        return P_ARENA_DATA(arena, arenaDataKey);
}

local PerPlayerData *getPerPlayerData(Player *p)
{
       return PPDATA(p, playerDataKey);
}

local Item * getItemByID(int id)
{
    Link *link;
    Item *returnValue = NULL;

    lock();
    for (link = LLGetHead(&itemList); link; link = link->next)
    {
        Item *item = link->data;

        if (item->id == id)
        {
            returnValue = item;
            break;
        }
    }
    unlock();

    return returnValue;
}

local ItemType * getItemTypeByID(int id)
{
    Link *link;
    ItemType *returnValue = NULL;

    lock();
    for (link = LLGetHead(&itemTypeList); link; link = link->next)
    {
        ItemType *itemType = link->data;

        if (itemType->id == id)
        {
            returnValue = itemType;
            break;
        }
    }
    unlock();

    return returnValue;
}

//+--------------------------+
//|                          |
//|  Misc Utility Functions  |
//|                          |
//+--------------------------+

local const char * getArenaIdentifier(Arena *arena)
{
    if (!arena) {
        lm->Log(L_ERROR, "<hscore_database> getArenaIdentifier: No arena specified.");
        return "-UNKNOWN-";
    }

    /* cfghelp: Hyperspace:ArenaIdentifier, arena, string, mod: hscore_database
     * String to compare to the arena field in the MySQL. Defaults to the arena base name. */
    const char *arenaIdent = cfg->GetStr(arena->cfg, "Hyperspace", "ArenaIdentifier");

    if (arenaIdent == NULL) {
        arenaIdent = arena->basename; //default fallback
    }

    return arenaIdent;
}

local Player *getPlayerByRef(PlayerReference *ref)
{
  Player *p = pd->PidToPlayer(ref->pid);

  if (p) {
    PerPlayerData *playerData = getPerPlayerData(p);
    if (playerData->uniqueID != ref->uniqueID) return NULL;
  }

  return p;
}

local PlayerReference *getPlayerReference(Player *p, Arena *arena)
{
  PerPlayerData *playerData = getPerPlayerData(p);

  PlayerReference *ref = amalloc(sizeof(PlayerReference));
  ref->pid = p->pid;
  ref->uniqueID = playerData->uniqueID;
  ref->arena = arena;

  return ref;
}

//+------------------------+
//|                        |
//|  Post-Query Functions  |
//|                        |
//+------------------------+

local void LinkAmmo()
{
    Link *link;

    lock();
    for (link = LLGetHead(&itemList); link; link = link->next)
    {
        Item *item = link->data;

        if (item->ammoID != 0)
        {
            Item *ammo;
            ammo = getItemByID(item->ammoID);

            item->ammo = ammo;
            LLAdd(&ammo->ammoUsers, item);

            if (item->ammo == NULL)
            {
                lm->Log(L_ERROR, "<hscore_database> No ammo matched id %i requested by item id %i.", item->ammoID, item->id);
            }
        }
        else
        {
            // force it, for reloaditems
            item->ammo = NULL;
        }
    }
    unlock();
}

//+----------------------------------+
//|                                  |
//|  MySQL Table Creation Functions  |
//|                                  |
//+----------------------------------+

#define CREATE_CATEGORIES_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_categories` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `name` varchar(32) NOT NULL default ''," \
"  `description` varchar(64) NOT NULL default ''," \
"  `arena` varchar(32) NOT NULL default ''," \
"  `order` tinyint(4) NOT NULL default '0'," \
"  `hidden` tinyint(4) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_CATEGORY_ITEMS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_category_items` (" \
"  `item_id` int(10) unsigned NOT NULL default '0'," \
"  `category_id` int(10) unsigned NOT NULL default '0'," \
"  `order` tinyint(4) NOT NULL default '0'" \
")"

#define CREATE_ITEM_EVENTS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_item_events` (" \
"  `item_id` int(10) unsigned NOT NULL default '0'," \
"  `event` varchar(16) NOT NULL default ''," \
"  `action` mediumint(9) NOT NULL default '0'," \
"  `data` int(11) NOT NULL default '0'," \
"  `message` varchar(200) NOT NULL default ''," \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_ITEM_PROPERTIES_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_item_properties` (" \
"  `item_id` int(10) unsigned NOT NULL default '0'," \
"  `name` varchar(32) NOT NULL default ''," \
"  `value` int(11) NOT NULL default '0'," \
"  `absolute` tinyint(4) NOT NULL default '0'," \
"  `ignore_count` tinyint(4) NOT NULL default '0'," \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_ITEM_TYPES_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_item_types` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `name` varchar(32) NOT NULL default ''," \
"  `max` int(11) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_ITEMS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_items` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `name` varchar(32) NOT NULL default ''," \
"  `short_description` varchar(32) NOT NULL default ''," \
"  `long_description` varchar(200) NOT NULL default ''," \
"  `buy_price` int(11) NOT NULL default '0'," \
"  `sell_price` int(11) NOT NULL default '0'," \
"  `exp_required` int(11) NOT NULL default '0'," \
"  `ships_allowed` int(11) NOT NULL default '0'," \
"  `max` int(11) NOT NULL default '0'," \
"  `delay_write` tinyint(4) NOT NULL default '0'," \
"  `ammo` int(10) unsigned NOT NULL default '0'," \
"  `needs_ammo` tinyint(4) NOT NULL default '0'," \
"  `min_ammo` int(11) NOT NULL default '0'," \
"  `affects_sets` tinyint(4) NOT NULL default '0'," \
"  `resend_sets` tinyint(4) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_PLAYER_SHIP_ITEMS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_player_ship_items` (" \
"  `ship_id` int(10) unsigned NOT NULL default '0'," \
"  `item_id` int(10) unsigned NOT NULL default '0'," \
"  `count` int(11) NOT NULL default '0'," \
"  `data` int(11) NOT NULL default '0'," \
"  PRIMARY KEY  (`ship_id`,`item_id`)" \
")"

#define CREATE_PLAYER_SHIPS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_player_ships` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `player_id` int(10) unsigned NOT NULL default '0'," \
"  `ship` tinyint(4) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_PLAYERS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_players` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `name` varchar(32) NOT NULL default ''," \
"  `arena` varchar(32) NOT NULL default ''," \
"  `money` int(11) NOT NULL default '0'," \
"  `exp` int(11) NOT NULL default '0'," \
"  `money_give` int(11) NOT NULL default '0'," \
"  `money_grant` int(11) NOT NULL default '0'," \
"  `money_buysell` int(11) NOT NULL default '0'," \
"  `money_kill` int(11) NOT NULL default '0'," \
"  `money_flag` int(11) NOT NULL default '0'," \
"  `money_ball` int(11) NOT NULL default '0'," \
"  `money_event` int(11) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_STORE_ITEMS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_store_items` (" \
"  `item_id` int(10) unsigned NOT NULL default '0'," \
"  `store_id` int(10) unsigned NOT NULL default '0'," \
"  `order` tinyint(4) NOT NULL default '0'" \
")"

#define CREATE_STORES_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_stores` (" \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  `name` varchar(32) NOT NULL default ''," \
"  `description` varchar(200) NOT NULL default ''," \
"  `region` varchar(16) NOT NULL default ''," \
"  `arena` varchar(32) NOT NULL default ''," \
"  `order` tinyint(4) NOT NULL default '0'," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_SHIP_PROPERTIES_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_ship_properties` (" \
"  `ship` tinyint(4) NOT NULL default '0'," \
"  `name` varchar(32) NOT NULL default ''," \
"  `value` int(11) NOT NULL default '0'," \
"  `absolute` tinyint(4) NOT NULL default '0'," \
"  `arena` varchar(32) NOT NULL default ''," \
"  `id` int(10) unsigned NOT NULL auto_increment," \
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_TRANSACTIONS_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_transactions` (" \
"  `id` int(10) NOT NULL auto_increment," \
"  `srcplayer` int(11) NOT NULL default '0'," \
"  `tgtplayer` int(11) NOT NULL default '0'," \
"  `action` tinyint(4) NOT NULL default '0', " \
"  `amount` int(11) NOT NULL default '0'," \
"  `timestamp` timestamp NOT NULL default CURRENT_TIMESTAMP on update CURRENT_TIMESTAMP,  "\
"  PRIMARY KEY  (`id`)" \
")"

#define CREATE_ITEM_TYPE_ASSOC_TABLE \
"CREATE TABLE IF NOT EXISTS `hs_item_type_assoc` (" \
"  `item_id` int(10) unsigned NOT NULL," \
"  `type_id` int(10) unsigned NOT NULL," \
"  `qty` int(10) NOT NULL default '1'," \
"  PRIMARY KEY  (`item_id`,`type_id`)" \
")"

local void initTables()
{
    mysql->Query(NULL, NULL, 0, CREATE_CATEGORIES_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_CATEGORY_ITEMS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_ITEM_EVENTS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_ITEM_PROPERTIES_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_ITEM_TYPES_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_ITEMS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_PLAYER_SHIP_ITEMS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_PLAYER_SHIPS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_PLAYERS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_STORE_ITEMS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_STORES_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_SHIP_PROPERTIES_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_TRANSACTIONS_TABLE);
    mysql->Query(NULL, NULL, 0, CREATE_ITEM_TYPE_ASSOC_TABLE);
}

//+-------------------------+
//|                         |
//|  MySQL Query Callbacks  |
//|                         |
//+-------------------------+

local void loadPropertiesQueryCallback(int status, db_res *result, void *passedData)
{
    Player *p;
    Link *link;
    int results;
    int x;
    db_row *row;

    if (status != 0 || result == NULL)
    {
            lm->Log(L_ERROR, "<hscore_database> Unexpected database error during property load.");
            return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
            lm->Log(L_WARN, "<hscore_database> No properties returned from MySQL query.");
    }


    lock();

    for (link = LLGetHead(&itemList); link; link = link->next)
    {
            Item *i = link->data;
            LLEnum(&i->propertyList, afree);
            LLEmpty(&i->propertyList);
    }

    pd->Lock();
    FOR_EACH_PLAYER(p)
    {
            PerPlayerData *playerData = getPerPlayerData(p);
            for (x = 0; x < HSCORE_MAX_HULLS; ++x)
            {
                    if (!playerData->hull[x])
                            continue;

                    HashEnum(playerData->hull[x]->propertySums, hash_enum_afree, 0);
                    HashEnum(playerData->hull[x]->propertySums, hash_enum_remove, 0);
            }
    }
    pd->Unlock();

    while ((row = mysql->GetRow(result)))
    {
            int itemID = atoi(mysql->GetField(row, 0));                                     //item_id
            Item *item;

            item = getItemByID(itemID);

            if (item != NULL)
            {
                    Property *property = NULL;
                    /*for (link = LLGetHead(&item->propertyList); link; link = link->next)
                    {
                            if (!strcmp(link->data->name, mysql->GetField(row, 1)))
                            {
                                    property = link->data;
                                    break;
                            }
                    }*/

                    //if the property doesn't exist in this item yet, create it. otherwise, just change the value.
                    if (!property)
                    {
                            property = amalloc(sizeof(*property));
                            astrncpy(property->name, mysql->GetField(row, 1), 33);
                            LLAdd(&item->propertyList, property);
                    }

                    property->value = atoi(mysql->GetField(row, 2));                //value
                    property->absolute = atoi(mysql->GetField(row, 3));             //absolute
                    property->ignoreCount = atoi(mysql->GetField(row, 4));  //ignore_count
            }
            else
            {
                    lm->Log(L_ERROR, "<hscore_database> property looking for item ID %i.", itemID);
            }
    }
    unlock();

    DO_CBS(CB_HS_ITEMRELOAD, ALLARENAS, HSItemReload, ());

    lm->Log(L_DRIVEL, "<hscore_database> %i properties were loaded from MySQL.", results);
}

local void loadEventsQueryCallback(int status, db_res *result, void *passedData)
{
    Link *link;
    int results;
    db_row *row;

    if (status != 0 || result == NULL)
    {
            lm->Log(L_ERROR, "<hscore_database> Unexpected database error during event load.");
            return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
            lm->Log(L_WARN, "<hscore_database> No events returned from MySQL query.");
    }

    lock();
    for (link = LLGetHead(&itemList); link; link = link->next)
    {
            Item *i = link->data;
            LLEnum(&i->eventList, afree);
            LLEmpty(&i->eventList);
    }

    while ((row = mysql->GetRow(result)))
    {
            int itemID = atoi(mysql->GetField(row, 0)); //item_id
            Item *item = getItemByID(itemID);

            if (item != NULL)
            {
                    Event *event = NULL;
                    /*for (link = LLGetHead(&item->eventList); link; link = link->next)
                    {
                            if (!strcmp(link->data->event, mysql->GetField(row, 1)))
                            {
                                    event = link->data;
                                    break;
                            }
                    }*/

                    //if the event doesn't exist in this item yet, create it. otherwise, just change the values.
                    if (!event)
                    {
                            event = amalloc(sizeof(*event));
                            astrncpy(event->event, mysql->GetField(row, 1), 17);
                            LLAdd(&item->eventList, event);
                    }



                    event->action = atoi(mysql->GetField(row, 2));                  //action
                    event->data = atoi(mysql->GetField(row, 3));                    //data
                    astrncpy(event->message, mysql->GetField(row, 4), 201); //message


            }
            else
            {
                    lm->Log(L_ERROR, "<hscore_database> event looking for item ID %i.", itemID);
            }
    }
    unlock();

    lm->Log(L_DRIVEL, "<hscore_database> %i events were loaded from MySQL.", results);

}

local void loadItemsQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    if (status != 0 || result == NULL)
    {
            lm->Log(L_ERROR, "<hscore_database> Unexpected database error during items load.");
            return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
            lm->Log(L_WARN, "<hscore_database> No items returned from MySQL query.");
    }

    while ((row = mysql->GetRow(result)))
    {
            Link *link;
            Item *item = NULL;
            int id = atoi(mysql->GetField(row, 0));

            //char itemTypes[256];

            lock();
            for (link = LLGetHead(&itemList); link; link = link->next)
            {
                    Item *i = link->data;
                    if (i->id == id)
                    {
                            item = i;
                            break;
                    }
            }

            //if the Item doesn't exist yet, create it. otherwise, just change the values.
            if (!item)
            {
                    item = amalloc(sizeof(*item));
                    LLInit(&item->propertyList);
                    LLInit(&item->eventList);
                    LLInit(&item->itemTypeEntries);
                    LLInit(&item->ammoUsers);
                    item->id = id;
                    LLAdd(&itemList, item);
            }
            else
            {
                    //empty the item types
                    LLEnum(&item->itemTypeEntries, afree);
                    LLEmpty(&item->itemTypeEntries);
                    LLEmpty(&item->ammoUsers);
            }

            astrncpy(item->name, mysql->GetField(row, 1), 33);              //name
            astrncpy(item->shortDesc, mysql->GetField(row, 2), 33);         //short_description
            astrncpy(item->longDesc, mysql->GetField(row, 3), 201);         //long_description
            item->buyPrice = atoi(mysql->GetField(row, 4));                 //buy_price
            item->sellPrice = atoi(mysql->GetField(row, 5));                //sell_price
            item->expRequired = atoi(mysql->GetField(row, 6));              //exp_required
            item->shipsAllowed = atoi(mysql->GetField(row, 7));             //ships_allowed

            item->max = atoi(mysql->GetField(row, 8));                      //max

            item->delayStatusWrite = atoi(mysql->GetField(row, 9));         //delay_write
            item->ammoID = atoi(mysql->GetField(row, 10));                  //ammo
            item->needsAmmo = atoi(mysql->GetField(row, 11));               //needs_ammo
            item->minAmmo = atoi(mysql->GetField(row, 12));                 //min_ammo
            item->affectsSets = atoi(mysql->GetField(row, 13));             //affects_sets
            item->resendSets = atoi(mysql->GetField(row, 14));              //resend_sets

            unlock();

    }

    lm->Log(L_DRIVEL, "<hscore_database> %i items were loaded from MySQL.", results);

    //now that all the items are in, load the properties & events.
    LoadProperties();
    LoadEvents();

    //process the ammo ids
    LinkAmmo();
}

local void loadItemTypeAssocQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;
    Item *item;

    if (status != 0 || result == NULL)
    {
            lm->Log(L_ERROR, "<hscore_database> Unexpected database error during item type association.");
            return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
            lm->Log(L_WARN, "<hscore_database> No items returned from MySQL query.");
    }

    while ((row = mysql->GetRow(result)))
    {
            int item_id = atoi(mysql->GetField(row, 0));
            int type_id = atoi(mysql->GetField(row, 1));
            int qty = atoi(mysql->GetField(row, 2));

            item = getItemByID(item_id);

            if(item != NULL)
            {
                    ItemType *itemType = getItemTypeByID(type_id);
                    if (itemType != NULL)
                    {
                            ItemTypeEntry *entry = amalloc(sizeof(*entry));
                            entry->itemType = itemType;
                            entry->delta = qty;

                            LLAdd(&item->itemTypeEntries, entry);
                    }
                    else
                    {
                            lm->Log(L_ERROR, "<hscore_database> bad item type %d on item %s", type_id, item->name);
                    }
            }
            else
            {
                    lm->Log(L_ERROR, "<hscore_database> tried to assign type %d to bad item %d", type_id, item_id);
            }
    }

    lm->Log(L_DRIVEL, "<hscore_database> %i item types were assigned from MySQL.", results);
}

local void loadItemTypesQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    if (status != 0 || result == NULL)
    {
            lm->Log(L_ERROR, "<hscore_database> Unexpected database error during item types load.");
            return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
            lm->Log(L_WARN, "<hscore_database> No item types returned from MySQL query.");
    }

    while ((row = mysql->GetRow(result)))
    {
        int id = atoi(mysql->GetField(row, 0));
        Link *link;
        ItemType *itemType = NULL;
        lock();
        for (link = LLGetHead(&itemTypeList); link; link = link->next)
        {
            ItemType *it = link->data;
            if (it->id == id)
            {
                itemType = it;
                break;
            }
        }

        //if the ItemType doesn't exist yet, create it, otherwise just change the values
        if (!itemType)
        {
            itemType = amalloc(sizeof(*itemType));
            itemType->id = id;
            LLAdd(&itemTypeList, itemType);
        }

        astrncpy(itemType->name, mysql->GetField(row, 1), 33);  //name
        itemType->max = atoi(mysql->GetField(row, 2));                  //max

        unlock();
    }

    lm->Log(L_DRIVEL, "<hscore_database> %i item types were loaded from MySQL.", results);
    LoadItemList(); //now that all the item types are in, load the items.
    AssociateItemTypes(); //give items their types

}

local void loadPlayerWalletQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    PlayerReference *ref = (PlayerReference*)passedData;
    Arena *arena = ref->arena;
    Player *p = getPlayerByRef(ref);
    afree(ref);

    if (!p) return;

    PerPlayerData *playerData = getPerPlayerData(p);

    if (status != 0 || result == NULL)
    {
        lm->LogP(L_ERROR, "hscore_database", p, "Unexpected database error during player globals load.");
        return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
        //insert a new player into MySQL and then get it
        /* cfghelp: Hyperspace:InitialMoney, global, int, mod: hscore_database
         * The amount of money that is given to a new player. */
        int initialMoney = cfg->GetInt(GLOBAL, "hyperspace", "initialmoney", 1000);
        /* cfghelp: Hyperspace:InitialExp, global, int, mod: hscore_database
         * The amount of exp that is given to a new player. */
        int initialExp = cfg->GetInt(GLOBAL, "hyperspace", "initialexp", 0);

        mysql->Query(NULL, NULL, 0, "INSERT INTO hs_players VALUES (NULL, ?, ?, #, #, 0, 0, 0, 0, 0, 0, 0)", p->name, getArenaIdentifier(arena), initialMoney, initialExp);
        LoadPlayerWallet(p, arena);
        return;
    }

    if (results > 1)
    {
        lm->LogP(L_ERROR, "hscore_database", p, "Multiple rows returned from database: using first.");
    }

    row = mysql->GetRow(result);

    playerData->id = atoi(mysql->GetField(row, 0));                                 //id
    playerData->money = atoi(mysql->GetField(row, 1));                              //money
    playerData->exp = atoi(mysql->GetField(row, 2));                                //exp
    playerData->moneyType[MONEY_TYPE_GIVE] = atoi(mysql->GetField(row, 3));         //money_give
    playerData->moneyType[MONEY_TYPE_GRANT] = atoi(mysql->GetField(row, 4));        //money_grant
    playerData->moneyType[MONEY_TYPE_BUYSELL] = atoi(mysql->GetField(row, 5));      //money_buysell
    playerData->moneyType[MONEY_TYPE_KILL] = atoi(mysql->GetField(row, 6));         //money_kill
    playerData->moneyType[MONEY_TYPE_FLAG] = atoi(mysql->GetField(row, 7));         //money_flag
    playerData->moneyType[MONEY_TYPE_BALL] = atoi(mysql->GetField(row, 8));         //money_ball
    playerData->moneyType[MONEY_TYPE_EVENT] = atoi(mysql->GetField(row, 9));        //money_event

    playerData->walletLoaded = 1;
    playerData->warena = getArenaIdentifier(arena);

    lm->LogP(L_DRIVEL, "hscore_database", p, "Loaded played wallet for arena %s.", playerData->warena);
    LoadPlayerShips(p, arena);
}

local void loadShipIDQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;
    int id;
    int ship;
    ShipHull *hull;

    PlayerReference *ref = (PlayerReference*)passedData;
    Arena *arena = ref->arena;
    Player *p = getPlayerByRef(ref);
    afree(ref);

    if (!p) return;

    PerPlayerData *playerData = getPerPlayerData(p);

    if (status != 0 || result == NULL)
    {
        lm->Log(L_ERROR, "<hscore_database> Unexpected database error during ship ID load.");
        return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
        lm->Log(L_ERROR, "<hscore_database> No ship ID results returned. Expect things to go to hell.");
         return;
    }

    if (results > 1)
    {
        lm->Log(L_ERROR, "<hscore_database> More than one ship ID returned. Using first.");
    }

    row = mysql->GetRow(result);

    id = atoi(mysql->GetField(row, 0));
    ship = atoi(mysql->GetField(row, 1));


    if (0 <= ship && ship < HSCORE_MAX_HULLS) {
      hull = playerData->hull[ship];
      hull->id = id;

      // Translate the ship to the actual ship and shipset.
      DO_CBS(CB_SHIP_ADDED, arena, ShipAdded, (p, ship % 8, ship / 8));
    } else {
      lm->Log(L_ERROR, "<hscore_database> Invalid ship (%d) for ship id %d", ship, id);
    }
}

local void loadPlayerShipItemsQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    PlayerReference *ref = (PlayerReference*)passedData;
    Arena *arena = ref->arena;
    Player *p = getPlayerByRef(ref);
    afree(ref);

    if (!p) return;

    PerPlayerData *playerData = getPerPlayerData(p);

    if (status != 0 || result == NULL)
    {
        lm->LogP(L_ERROR, "hscore_database", p, "Unexpected database error during player ship item load.");
        return;
    }

    results = mysql->GetRowCount(result);

    while ((row = mysql->GetRow(result)))
    {
        int itemID = atoi(mysql->GetField(row, 0));     //item_id
        int count = atoi(mysql->GetField(row, 1));      //count
        int data = atoi(mysql->GetField(row, 2));       //data
        int ship = atoi(mysql->GetField(row, 3));       //ship

        Item *item = getItemByID(itemID);

        if (0 <= ship && ship < HSCORE_MAX_HULLS)
        {
            if (playerData->hull[ship] != NULL)
            {
                if (item != NULL)
                {
                    ShipHull *hull = playerData->hull[ship];

                    InventoryEntry *inventoryEntry = amalloc(sizeof(*inventoryEntry));

                    inventoryEntry->item = item;
                    inventoryEntry->count = count;
                    inventoryEntry->data = data;

                    lock();
                    LLAdd(&hull->inventoryEntryList, inventoryEntry);
                    unlock();
                }
                else
                {
                    lm->LogP(L_ERROR, "hscore_database", p, "Bad item id (%i) attached to ship %i", itemID, ship);
                }
            }
            else
            {
                lm->LogP(L_ERROR, "hscore_database", p, "Item %i attached to ship %i, but no ship hull exists there!", itemID, ship);
            }
        }
        else
        {
            lm->LogP(L_ERROR, "hscore_database", p, "Extreme error! item %i attached to ship %i", itemID, ship);
        }
    }

    playerData->shipsLoaded = 1;

    DO_CBS(CB_SHIPS_LOADED, arena, ShipsLoaded, (p));

    lm->LogP(L_DRIVEL, "hscore_database", p, "%i ship items were loaded from MySQL.", results);
}

local void loadPlayerShipsQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    PlayerReference *ref = (PlayerReference*)passedData;
    Arena *arena = ref->arena;
    Player *p = getPlayerByRef(ref);
    afree(ref);

    if (!p) return;

    PerPlayerData *playerData = getPerPlayerData(p);
    PerArenaData *arenaData = getPerArenaData(arena);

    if (status != 0 || result == NULL)
    {
        lm->LogP(L_ERROR, "hscore_database", p, "Unexpected database error during player ship load.");
        return;
    }

    results = mysql->GetRowCount(result);

    while ((row = mysql->GetRow(result)))
    {
        int id = atoi(mysql->GetField(row, 0));         //id
        int ship = atoi(mysql->GetField(row, 1));       //ship & shipset (shipset * ships + ship)

        if (0 <= ship && ship < HSCORE_MAX_HULLS)
        {
            ShipHull *hull = amalloc(sizeof(*hull));

            LLInit(&hull->inventoryEntryList);
            hull->propertySums = HashAlloc();
            hull->ship = ship % 8;
            hull->id = id;

            hull->propertyList = &arenaData->shipPropertyLists[hull->ship];
            playerData->hull[ship] = hull;
        }
        else
        {
            lm->LogP(L_ERROR, "hscore_database", p, "ship id %i had bad ship number (%i)", id, ship);
        }
    }

    playerData->shipsLoaded = 1;
    playerData->sarena = getArenaIdentifier(arena);

    lm->LogP(L_DRIVEL, "hscore_database", p, "%i ships were loaded from MySQL.", results);

    LoadPlayerShipItems(p, arena); //load the inventory for the ship
}

local void loadStoreItemsQueryCallback(int status, db_res *result, void *passedData)
{
        int results;
        db_row *row;

        Arena *arena = passedData;
        PerArenaData *arenaData = getPerArenaData(arena);

        if (status != 0 || result == NULL)
        {
                lm->LogA(L_ERROR, "hscore_database", arena, "Unexpected database error during store item load.");
                return;
        }

        results = mysql->GetRowCount(result);

        if (results == 0)
        {
                //no big deal
                return;
        }

        while ((row = mysql->GetRow(result)))
        {
                Link *link;
                int itemID = atoi(mysql->GetField(row, 0));             //item_id
                int storeID = atoi(mysql->GetField(row, 1));    //store_id

                Item *item = getItemByID(itemID);
                if (item == NULL)
                {
                        lm->LogA(L_ERROR, "hscore_database", arena, "item id %i not found (linked to store id %i)", itemID, storeID);
                        continue;
                }

                for (link = LLGetHead(&arenaData->storeList); link; link = link->next)
                {
                        Store *store = link->data;

                        if (store->id == storeID)
                        {
                                //add the item to the store's list
                                lock();
                                LLAdd(&store->itemList, item);
                                unlock();
                                break;
                        }
                }

                //bad store id will give no error!
        }

        lm->LogA(L_DRIVEL, "hscore_database", arena, "%i store items were loaded from MySQL.", results);
}

local void loadArenaStoresQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    Arena *arena = passedData;
    PerArenaData *arenaData = getPerArenaData(arena);

    if (status != 0 || result == NULL)
    {
        lm->LogA(L_ERROR, "hscore_database", arena, "Unexpected database error during store load.");
        return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
        //no big deal
        return;
    }

    lock();
    while ((row = mysql->GetRow(result)))
    {
        Store *store = NULL;
        Link *link;
        int id = atoi(mysql->GetField(row, 0));

        for (link = LLGetHead(&arenaData->storeList); link; link = link->next)
        {
            Store *s = link->data;
            if (s->id == id)
            {
                store = s;
                break;
            }
        }

        //if the Store does not exist in this arena yet, create it. otherwise just modify the values, and reset the item lists.
        if (!store)
        {
            store = amalloc(sizeof(*store));
            LLInit(&store->itemList);
            store->id = id;
            store->name[0] = 0;
            store->description[0] = 0;
            store->region[0] = 0;
            LLAdd(&arenaData->storeList, store);
        }
        else
        {
            LLEmpty(&store->itemList);
        }

        astrncpy(store->name, mysql->GetField(row, 1), 33);                     //name
        astrncpy(store->description, mysql->GetField(row, 2), 201);     //description
        astrncpy(store->region, mysql->GetField(row, 3), 17);           //region
    }
    unlock();

    lm->LogA(L_DRIVEL, "hscore_database", arena, "%i stores were loaded from MySQL.", results);
    LoadStoreItems(arena); //now that all the stores are in, load the items into them.
}

local void loadCategoryItemsQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    Arena *arena = passedData;
    PerArenaData *arenaData = getPerArenaData(arena);

    if (status != 0 || result == NULL)
    {
        lm->LogA(L_ERROR, "hscore_database", arena, "Unexpected database error during category item load.");
        return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
        //no big deal
        return;
    }

    while ((row = mysql->GetRow(result)))
    {
        Link *link;
        int itemID = atoi(mysql->GetField(row, 0));             //item_id
        int categoryID = atoi(mysql->GetField(row, 1)); //category_id

        Item *item = getItemByID(itemID);
        if (item == NULL)
        {
            lm->LogA(L_ERROR, "hscore_database", arena, "item id %i not found (linked to category id %i)", itemID, categoryID);
            continue;
        }

        for (link = LLGetHead(&arenaData->categoryList); link; link = link->next)
        {
            Category *category = link->data;

            if (category->id == categoryID)
            {
                //add the item to the category's list
                lock();
                LLAdd(&category->itemList, item);
                unlock();
                break;
            }
        }

        //bad category id will give no error!
    }

    lm->LogA(L_DRIVEL, "hscore_database", arena, "%i cateogry items were loaded from MySQL.", results);
}

local void loadArenaCategoriesQueryCallback(int status, db_res *result, void *passedData)
{
        int results;
        db_row *row;

        Arena *arena = passedData;
        PerArenaData *arenaData = getPerArenaData(arena);

        if (status != 0 || result == NULL)
        {
                lm->LogA(L_ERROR, "hscore_database", arena, "Unexpected database error during category load.");
                return;
        }

        results = mysql->GetRowCount(result);

        if (results == 0)
        {
                //no big deal
                return;
        }

        lock();
        while ((row = mysql->GetRow(result)))
        {
                Category *category = NULL;
                Link *link;
                int id = atoi(mysql->GetField(row, 0));

                for (link = LLGetHead(&arenaData->categoryList); link; link = link->next)
                {
                        Category *c = link->data;
                        if (c->id == id)
                        {
                                category = c;
                                break;
                        }
                }

                //if the Category doesn't exist, create it, otherwise just change the values and reset the lists
                if (!category)
                {
                        category = amalloc(sizeof(*category));
                        LLInit(&category->itemList);
                        category->id = id;
                        category->name[0] = 0;
                        category->description[0] = 0;
                        category->hidden = 0;
                        LLAdd(&arenaData->categoryList, category);
                }
                else
                {
                        LLEmpty(&category->itemList);
                }

                astrncpy(category->name, mysql->GetField(row, 1), 33);                  //name
                astrncpy(category->description, mysql->GetField(row, 2), 65);   //description
                category->hidden = atoi(mysql->GetField(row, 3));                               //hidden
        }
        unlock();

        lm->LogA(L_DRIVEL, "hscore_database", arena, "%i categories were loaded from MySQL.", results);
        LoadCategoryItems(arena); //now that all the stores are in, load the items into them.
}

local void loadShipPropertyListsQueryCallback(int status, db_res *result, void *passedData)
{
    int results;
    db_row *row;

    Arena *arena = passedData;
    PerArenaData *arenaData = getPerArenaData(arena);

    if (status != 0 || result == NULL)
    {
        lm->LogA(L_ERROR, "hscore_database", arena, "Unexpected database error during ship property load.");
        return;
    }

    results = mysql->GetRowCount(result);

    if (results == 0)
    {
        //no big deal
        return;
    }

    UnloadShipPropertyLists(arena);

    lock();
    while ((row = mysql->GetRow(result)))
    {
        int ship = atoi(mysql->GetField(row, 0)); //ship
        if (ship < 0 || ship >= HSCORE_MAX_HULLS)
        {
            lm->LogA(L_ERROR, "hscore_database", arena, "ship property looking for ship %i.", ship);
            continue;
        }

        Property *property = amalloc(sizeof(*property));

        astrncpy(property->name, mysql->GetField(row, 1), 33);
        property->value = atoi(mysql->GetField(row, 2));                //value
        property->absolute = atoi(mysql->GetField(row, 3));             //absolute

        LLAdd(&arenaData->shipPropertyLists[ship], property);
    }
    unlock();

    lm->LogA(L_DRIVEL, "hscore_database", arena, "%i ship properties were loaded from MySQL.", results);
}

//+------------------+
//|                  |
//|  Init Functions  |
//|                  |
//+------------------+

local void InitPerPlayerData(Player *p) //called before data is touched
{
    PerPlayerData *playerData = getPerPlayerData(p);
    int i;

    playerData->uniqueID = currentUniqueID;
    currentUniqueID++;

    playerData->walletLoaded = 0;
    playerData->shipsLoaded = 0;

    playerData->shipset = 0;

    for (i = 0; i < HSCORE_MAX_HULLS; i++) {
        playerData->hull[i] = NULL;
    }
}

local void InitPerArenaData(Arena *arena) //called before data is touched
{
    PerArenaData *arenaData = getPerArenaData(arena);

    LLInit(&arenaData->categoryList);
    LLInit(&arenaData->storeList);

    for (int i = 0; i < 8; i++) {
        LLInit(&arenaData->shipPropertyLists[i]);
    }
}

//+--------------------+
//|                    |
//|  Unload Functions  |
//|                    |
//+--------------------+

local void UnloadPlayerWallet(Player *p)
{
    PerPlayerData *playerData = getPerPlayerData(p);

    playerData->walletLoaded = 0; //nothing else needed
    playerData->warena = NULL;

    lm->LogP(L_DRIVEL, "hscore_database", p, "Freed global data.");
}

local void UnloadPlayerShip(ShipHull *ship)
{
    lock();

    LLEnum(&ship->inventoryEntryList, afree);
    LLEmpty(&ship->inventoryEntryList);

    HashEnum(ship->propertySums, hash_enum_afree, NULL);
    HashFree(ship->propertySums);

    afree(ship);

    unlock();
}

local void UnloadPlayerShips(Player *p) //called to free any allocated data
{
    PerPlayerData *playerData = getPerPlayerData(p);
    int i;

    lock();

    for (i = 0; i < HSCORE_MAX_HULLS; i++)
    {
        if (playerData->hull[i] != NULL)
        {
            UnloadPlayerShip(playerData->hull[i]);
            playerData->hull[i] = NULL;
        }
    }

    playerData->shipsLoaded = 0;
    playerData->sarena = NULL;

    unlock();

    lm->LogP(L_DRIVEL, "hscore_database", p, "Freed ship data.");
}

local void UnloadCategoryList(Arena *arena) //called when the arena is about to die
{
    PerArenaData *arenaData = getPerArenaData(arena);

    lock();
    LLEnum(&arenaData->categoryList, afree); //can simply free all the Category structs

    lm->LogA(L_DRIVEL, "hscore_database", arena, "Freed %i categories.", LLCount(&arenaData->categoryList));

    LLEmpty(&arenaData->categoryList);
    unlock();
}

local void UnloadStoreList(Arena *arena)
{
    PerArenaData *arenaData = getPerArenaData(arena);

    lock();
    LLEnum(&arenaData->storeList, afree); //can simply free all the Store structs

    lm->LogA(L_DRIVEL, "hscore_database", arena, "Freed %i stores.", LLCount(&arenaData->storeList));

    LLEmpty(&arenaData->storeList);
    unlock();
}

local void UnloadShipPropertyLists(Arena *arena)
{
        PerArenaData *arenaData = getPerArenaData(arena);

        int i;
        int count = 0;

        lock();
        for (i = 0; i < 8; i++)
        {
                LLEnum(&arenaData->shipPropertyLists[i], afree); //can simply free all the Store structs
                count += LLCount(&arenaData->shipPropertyLists[i]);
                LLEmpty(&arenaData->shipPropertyLists[i]);
        }
        unlock();

        lm->LogA(L_DRIVEL, "hscore_database", arena, "Freed %i ship properties.", count);
}

local void UnloadItemListEnumCallback(const void *ptr)
{
    lock();

    Item *item = (Item*)ptr;

    LLEnum(&item->propertyList, afree);
    LLEmpty(&item->propertyList);

    LLEnum(&item->eventList, afree);
    LLEmpty(&item->eventList);

    LLEnum(&item->itemTypeEntries, afree);
    LLEmpty(&item->itemTypeEntries);

    afree(item);

    unlock();
}

local void UnloadItemList()
{
    lock();
    LLEnum(&itemList, UnloadItemListEnumCallback);

    lm->Log(L_DRIVEL, "<hscore_database> Freed %i items.", LLCount(&itemList));

    LLEmpty(&itemList);
    unlock();
}

local void UnloadItemTypeList()
{
    lock();
    LLEnum(&itemTypeList, afree); //can simply free all the ItemType structs

    lm->Log(L_DRIVEL, "<hscore_database> Freed %i item types.", LLCount(&itemTypeList));

    LLEmpty(&itemTypeList);
    unlock();
}

local void UnloadAllPerArenaData()
{
    Arena *arena;
    Link *link;

    aman->Lock();
    FOR_EACH_ARENA(arena)
    {
            UnloadStoreList(arena);
            UnloadCategoryList(arena);
            UnloadShipPropertyLists(arena);
    }
    aman->Unlock();
}

local void UnloadAllPerPlayerData()
{
    Player *p;
    Link *link;
    pd->Lock();

    FOR_EACH_PLAYER(p) {
        if (isWalletLoaded(p)) {
            UnloadPlayerShips(p);
            UnloadPlayerWallet(p);
        }
    }

    pd->Unlock();
}

//+------------------+
//|                  |
//|  Load Functions  |
//|                  |
//+------------------+

local void LoadPlayerWallet(Player *p, Arena *arena) //fetch wallet from MySQL
{
    if (!p || !arena)
        return;

    PlayerReference *ref = getPlayerReference(p, arena);
    PerPlayerData *pdata = getPerPlayerData(p);

    if (isWalletLoaded(p)) {
        lm->LogP(L_WARN, "hscore_database", p, "Player wallet already loaded for arena %s.", pdata->warena);
    }

    mysql->Query(loadPlayerWalletQueryCallback, ref, 1, "SELECT id, money, exp, money_give, money_grant, money_buysell, money_kill, money_flag, money_ball, money_event FROM hs_players WHERE name = ? AND arena = ?", p->name, getArenaIdentifier(arena));
}

local void LoadPlayerShipItems(Player *p, Arena *arena) //fetch ship items from MySQL
{
    if (!p || !arena)
        return;

    if (areShipsLoaded(p))
    {
        PlayerReference *ref = getPlayerReference(p, arena);

        mysql->Query(loadPlayerShipItemsQueryCallback, ref, 1, "SELECT `psi`.`item_id`, `psi`.`count`, `psi`.`data`, `ps`.`ship` FROM `hs_player_ship_items` `psi` JOIN `hs_player_ships` `ps` ON `psi`.`ship_id` = `ps`.`id` JOIN `hs_players` `p` ON `ps`.`player_id` = `p`.`id` WHERE `p`.`name` = ? AND `p`.`arena` = ?", p->name, getArenaIdentifier(arena));
    }
    else
    {
        lm->LogP(L_ERROR, "hscore_database", p, "Asked to load ship items before loading ships.");
    }
}

local void LoadPlayerShips(Player *p, Arena *arena) //fetch ships from MySQL. Will call LoadPlayerShipItems()
{
    if (!p || !arena)
        return;

    PlayerReference *ref = getPlayerReference(p, arena);
    PerPlayerData *pdata = getPerPlayerData(p);

    if (areShipsLoaded(p)) {
        lm->LogP(L_WARN, "hscore_database", p, "Player ships already loaded for arena %s.", pdata->sarena);
    }

    mysql->Query(loadPlayerShipsQueryCallback, ref, 1, "SELECT `ps`.`id`, `ps`.`ship` FROM `hs_player_ships` `ps` JOIN `hs_players` `p` ON `ps`.`player_id` = `p`.`id` WHERE `p`.`name` = ? AND `p`.`arena` = ?", p->name, getArenaIdentifier(arena));
}

local void LoadCategoryItems(Arena *arena)
{
    mysql->Query(loadCategoryItemsQueryCallback, arena, 1, "SELECT item_id, category_id FROM hs_category_items, hs_categories WHERE category_id = id AND arena = ? ORDER BY hs_category_items.order ASC", getArenaIdentifier(arena));
}

local void LoadCategoryList(Arena *arena) //leads to LoadCategoryItems() being called
{
    mysql->Query(loadArenaCategoriesQueryCallback, arena, 1, "SELECT id, name, description, hidden FROM hs_categories WHERE arena = ? ORDER BY hs_categories.order ASC", getArenaIdentifier(arena));
}

local void LoadStoreItems(Arena *arena)
{
    mysql->Query(loadStoreItemsQueryCallback, arena, 1, "SELECT item_id, store_id FROM hs_store_items, hs_stores WHERE store_id = id AND arena = ? ORDER BY hs_store_items.order ASC", getArenaIdentifier(arena));
}

local void LoadStoreList(Arena *arena) //leads to LoadStoreItems() being called
{
    mysql->Query(loadArenaStoresQueryCallback, arena, 1, "SELECT id, name, description, region FROM hs_stores WHERE arena = ? ORDER BY hs_stores.order ASC", getArenaIdentifier(arena));
}

local void LoadShipPropertyLists(Arena *arena)
{
    mysql->Query(loadShipPropertyListsQueryCallback, arena, 1, "SELECT ship, name, value, absolute, arena FROM hs_ship_properties WHERE arena = ?", getArenaIdentifier(arena));
}

local void LoadEvents()
{
    mysql->Query(loadEventsQueryCallback, NULL, 1, "SELECT item_id, event, action, data, message FROM hs_item_events");
}

local void LoadProperties()
{
    mysql->Query(loadPropertiesQueryCallback, NULL, 1, "SELECT item_id, name, value, absolute, ignore_count FROM hs_item_properties");
}

local void LoadItemList() //will call LoadProperties() and LoadEvents() when finished
{
    mysql->Query(loadItemsQueryCallback, NULL, 1, "SELECT id, name, short_description, long_description, buy_price, sell_price, exp_required, ships_allowed, max, delay_write, ammo, needs_ammo, min_ammo, affects_sets, resend_sets FROM hs_items");
}

local void LoadItemTypeList() //will call LoadItemList() when finished loading
{
    mysql->Query(loadItemTypesQueryCallback, NULL, 1, "SELECT id, name, max FROM hs_item_types");
}

local void AssociateItemTypes()
{
    mysql->Query(loadItemTypeAssocQueryCallback, NULL, 1, "SELECT item_id, type_id, qty FROM hs_item_type_assoc ORDER BY item_id ASC");
}

//+-------------------+
//|                   |
//|  Store Functions  |
//|                   |
//+-------------------+

local void StorePlayerWallet(Player *p) //store player globals. MUST FINISH IN ONE QUERY LEVEL
{
    PerPlayerData *playerData = getPerPlayerData(p);

    if (isWalletLoaded(p))
    {
        lock();

        mysql->Query(NULL, NULL, 0, "UPDATE hs_players SET money = #, exp = #, money_give = #, money_grant = #, money_buysell = #, money_kill = #, money_flag = #, money_ball = #, money_event = # WHERE id = #",
                playerData->money,
                playerData->exp,
                playerData->moneyType[MONEY_TYPE_GIVE],
                playerData->moneyType[MONEY_TYPE_GRANT],
                playerData->moneyType[MONEY_TYPE_BUYSELL],
                playerData->moneyType[MONEY_TYPE_KILL],
                playerData->moneyType[MONEY_TYPE_FLAG],
                playerData->moneyType[MONEY_TYPE_BALL],
                playerData->moneyType[MONEY_TYPE_EVENT],
                playerData->id);

        unlock();
    }
    else
    {
        lm->LogP(L_ERROR, "hscore_database", p, "Tried to store unloaded wallet.");
    }
}

local void StorePlayerShips(Player *p, Arena *arena) //store player ships. MUST FINISH IN ONE QUERY LEVEL
{
        PerPlayerData *playerData = getPerPlayerData(p);

        if (arena == NULL)
                return;

        if (areShipsLoaded(p))
        {
                int i;
                lock();

                for (i = 0; i < HSCORE_MAX_HULLS; i++)
                {
                        if (playerData->hull[i] != NULL)
                        {
                                Link *link;
                                LinkedList *inventoryList = &playerData->hull[i]->inventoryEntryList;

                                int shipID = playerData->hull[i]->id;

                                if (shipID == -1)
                                {
                                        lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id load");

                                        while (shipID == -1)
                                        {
                                                shipID = playerData->hull[i]->id; //fixme: bad
                                        }
                                }

                                for (link = LLGetHead(inventoryList); link; link = link->next)
                                {
                                        InventoryEntry *entry = link->data;

                                        if (entry->item->delayStatusWrite)
                                        {
                                                mysql->Query(NULL, NULL, 0, "REPLACE INTO hs_player_ship_items VALUES (#,#,#,#)", shipID, entry->item->id, entry->count, entry->data);
                                        }
                                }
                        }
                }

                unlock();
        }
        else
        {
                lm->LogP(L_ERROR, "hscore_database", p, "asked to store unloaded ships");
        }
}

local void StoreAllPerPlayerData()
{
        Player *p;
        Link *link;
        pd->Lock();
        FOR_EACH_PLAYER(p)
                if (isWalletLoaded(p))
                {
                        if (areShipsLoaded(p))
                        {
                                StorePlayerShips(p, p->arena);
                        }
                        StorePlayerWallet(p);
                }
        pd->Unlock();
}

//+---------------------+
//|                     |
//|  Command Functions  |
//|                     |
//+---------------------+

local helptext_t reloadItemsHelp =
"Targets: none\n"
"Args: none\n"
"This command will reload all current and new items from the database.\n"
"It will also reload all arenas stores, categories and ship properties.\n"
"NOTE: This command will *NOT* remove items no longer in the database.\n";

local void reloadItemsCommand(const char *command, const char *params, Player *p, const Target *target)
{
        Link *link;
        Arena *a;
        chat->SendMessage(p, "Reloading items...");

        LoadItemTypeList();
        chat->SendMessage(p, "...Ran items queries.");

        aman->Lock();
        FOR_EACH_ARENA(a)
        {
                LoadStoreList(a);
                LoadCategoryList(a);
                LoadShipPropertyLists(a);
                chat->SendMessage(p, "...Ran stores/categories/ship properties queries for arena '%s'", a->name);
        }
        aman->Unlock();

        chat->SendMessage(p, "Ran all queries.");
}

local helptext_t refundHelp =
"Targets: none\n"
"Args: none\n"
"This command will check all item ship limitations and refund\n"
"max(buy price, sell price). It checks both online and offline players.\n";

local void refundCommand(const char *command, const char *params, Player *p, const Target *target)
{
        LinkedList list;
        Link *link;
        Player *i;

        chat->SendMessage(p, "Refunding items of online players...");

        LLInit(&list);

        pd->Lock();
        lock();
        FOR_EACH_PLAYER(i)
                if (isWalletLoaded(i))
                {
                        if (areShipsLoaded(i))
                        {
                                PerPlayerData *playerData = getPerPlayerData(i);

                                int shipset, ship, hid;

                                hid = 0;
                                for (shipset = 0; shipset < HSCORE_MAX_SHIPSETS; ++shipset) {
                                  for (ship = SHIP_WARBIRD; ship < SHIP_SPEC; ++ship) {

                                        if (playerData->hull[hid] != NULL)
                                        {
                                                LinkedList *inventoryList = &playerData->hull[hid]->inventoryEntryList;

                                                Link *item_link;
                                                for (item_link = LLGetHead(inventoryList); item_link; item_link = item_link->next)
                                                {
                                                        InventoryEntry *entry = item_link->data;
                                                        if (!(entry->item->shipsAllowed & 1 << hid))
                                                        {
                                                                //not allowed
                                                                int price = entry->item->buyPrice > entry->item->sellPrice?entry->item->buyPrice:entry->item->sellPrice; //max
                                                                playerData->money += price * entry->count;
                                                                playerData->moneyType[MONEY_TYPE_BUYSELL] += price * entry->count;
                                                                LLAdd(&list, entry);
                                                        }
                                                }

                                                //now remove all of the items in the list
                                                for (item_link = LLGetHead(&list); item_link; item_link = item_link->next)
                                                {
                                                        InventoryEntry *entry = item_link->data;
                                                        Item *item = entry->item;

                                                        int shipID = playerData->hull[hid]->id;
                                                        int oldCount = entry->count;

                                                        mysql->Query(NULL, NULL, 0, "DELETE FROM hs_player_ship_items WHERE ship_id = # AND item_id = #", shipID, item->id);

                                                        LLRemove(inventoryList, entry);
                                                        afree(entry);

                                                        DO_CBS(CB_ITEM_COUNT_CHANGED, i->arena, ItemCountChanged, (i, playerData->hull[hid], item, NULL, 0, oldCount));
                                                }

                                                LLEmpty(&list);
                                        }

                                    ++hid;
                                  }
                                }
                        }
                }
        unlock();
        pd->Unlock();

        chat->SendMessage(p, "Refunding items of offline players...");

        mysql->Query(NULL, NULL, 0, "UPDATE hs_players, (SELECT player_id, SUM(price) as money FROM (SELECT hs_players.id as player_id, GREATEST(hs_items.buy_price, hs_items.sell_price) as price, hs_items.id as item_id FROM hs_players INNER JOIN (hs_player_ships, hs_player_ship_items, hs_items) ON (hs_player_ships.player_id = hs_players.id AND hs_player_ship_items.ship_id = hs_player_ships.id AND hs_player_ship_items.item_id = hs_items.id) WHERE ((1 << hs_player_ships.ship) & hs_items.ships_allowed) = 0) as temp GROUP BY player_id) AS to_update SET hs_players.money = hs_players.money + to_update.money, hs_players.money_buysell = hs_players.money.buy_sell + to_update.money WHERE hs_players.id = to_update.player_id;");
        mysql->Query(NULL, NULL, 0, "DELETE FROM hs_player_ship_items USING hs_items, hs_players, hs_player_ship_items, hs_player_ships WHERE hs_player_ship_items.ship_id = hs_player_ships.id AND hs_players.id = hs_player_ships.player_id AND hs_items.id = hs_player_ship_items.item_id AND ((1 << hs_player_ships.ship) & hs_items.ships_allowed) = 0;");

        chat->SendMessage(p, "Done!");
}

local helptext_t storeAllHelp =
"Targets: none\n"
"Args: none\n"
"Stores all loaded player information back into MySQL.\n";

local void storeAllCommand(const char *command, const char *params, Player *p, const Target *target)
{
    StoreAllPerPlayerData();
    chat->SendMessage(p, "Executed.");
}

local helptext_t resetHelp =
"Targets: none or player\n"
"Args: none\n"
"Resets money, exp, items, ships and score. No undo!\n";

local void resetCommand(const char *command, const char *params, Player *p, const Target *target)
{
        Player *t = (target->type == T_PLAYER) ? target->u.p : p;
        PerPlayerData *playerData = getPerPlayerData(t);
        Istats *stats;
        int i;

        if (isWalletLoaded(t))
        {
                if(t->p_ship == SHIP_SPEC)
                {
                        lock();
                        //do money+exp first

                        //insert a new player into MySQL and then get it
                        /* cfghelp: Hyperspace:InitialMoney, global, int, mod: hscore_database
                         * The amount of money that is given to a new player. */
                        playerData->money = cfg->GetInt(GLOBAL, "hyperspace", "initialmoney", 1000);
                        /* cfghelp: Hyperspace:InitialExp, global, int, mod: hscore_database
                         * The amount of exp that is given to a new player. */
                        playerData->exp = cfg->GetInt(GLOBAL, "hyperspace", "initialexp", 0);

                        playerData->moneyType[MONEY_TYPE_GIVE] = 0;
                        playerData->moneyType[MONEY_TYPE_GRANT] = 0;
                        playerData->moneyType[MONEY_TYPE_BUYSELL] = 0;
                        playerData->moneyType[MONEY_TYPE_KILL] = 0;
                        playerData->moneyType[MONEY_TYPE_FLAG] = 0;
                        playerData->moneyType[MONEY_TYPE_BALL] = 0;
                        playerData->moneyType[MONEY_TYPE_EVENT] = 0;

                        mysql->Query(NULL, NULL, 0, "UPDATE hs_players SET money = #, exp = #, money_give = 0, money_grant = 0, money_buysell = 0, money_kill = 0, money_flag = 0, money_ball = 0, money_event = 0 WHERE id = #",
                                playerData->money,
                                playerData->exp,
                                playerData->id);

                        //do ships and items now
                        mysql->Query(NULL, NULL, 0, "DELETE hs_player_ships, hs_player_ship_items FROM hs_player_ships, hs_player_ship_items WHERE hs_player_ships.id = hs_player_ship_items.ship_id AND hs_player_ships.player_id = #", playerData->id);

                        //unload current ships
                        for (i = 0; i < HSCORE_MAX_HULLS; i++)
                        {
                                if (playerData->hull[i] != NULL)
                                {
                                        ShipHull *ship = playerData->hull[i];

                                        LLEnum(&ship->inventoryEntryList, afree);
                                        LLEmpty(&ship->inventoryEntryList);
                                        HashEnum(ship->propertySums, hash_enum_afree, NULL);
                                        HashFree(ship->propertySums);

                                        afree(ship);

                                        playerData->hull[i] = NULL;
                                }
                        }

                        unlock();

                        //reset score too

                        stats = mm->GetInterface(I_STATS, t->arena);
                        if (stats != NULL)
                        {
                                stats->ScoreReset(t, INTERVAL_RESET);
                                stats->SendUpdates(NULL);
                                mm->ReleaseInterface(stats);
                        }

                        if (t != p)
                        {
                                chat->SendMessage(t, "Reset!");
                                chat->SendMessage(p, "Player %s reset!", t->name);
                        }
                        else
                        {
                                chat->SendMessage(p, "Reset!");
                        }
                }
                else
                {
                        chat->SendMessage(p, "Must be in spec");
                }
        }
        else
        {
                chat->SendMessage(p, "Data not loaded!");
        }
}

//+----------------------+
//|                      |
//|  Callback functions  |
//|                      |
//+----------------------+

local void allocatePlayerCallback(Player *p, int allocating)
{
        if (allocating) //player is being allocated
        {
                //make sure we "zero out" the necessary data
                InitPerPlayerData(p);
        }
        else //p is being deallocated
        {
                //already taken care of on disconnect
        }
}

local void playerActionCallback(Player *p, int action, Arena *arena)
{
  switch (action) {
    case PA_ENTERARENA:
      //the player is entering an arena.
      LoadPlayerWallet(p, arena);

      // Ships will be loaded automatically after the player's wallet is loaded.
      //LoadPlayerShips(p, arena);
      break;

    case PA_LEAVEARENA:
      StorePlayerShips(p, arena);
      UnloadPlayerShips(p);

      StorePlayerWallet(p);
      UnloadPlayerWallet(p);
      break;
  }
}

local void arenaActionCallback(Arena *arena, int action)
{
  switch (action) {
    case AA_CREATE:
      //arena is being created
      InitPerArenaData(arena);

      //in no special order...
      LoadStoreList(arena);
      LoadCategoryList(arena);
      LoadShipPropertyLists(arena);
      break;

    case AA_DESTROY:
      //arena is being destroyed
      UnloadStoreList(arena); //in no special order...
      UnloadCategoryList(arena);
      UnloadShipPropertyLists(arena);

      //no need to deallocate the lists, as they weren't allocated
      break;
  }
}





local int periodicStoreTimer(void *param)
{
    lm->Log(L_INFO, "<hscore_database> Storing player data.");
    StoreAllPerPlayerData();
    return 1;
}

//+-----------------------+
//|                       |
//|  Interface Functions  |
//|                       |
//+-----------------------+

local int getPlayerWalletId(Player *p)
{
    PerPlayerData *playerData = getPerPlayerData(p);
    return playerData->walletLoaded ? playerData->id : -1;
}

local int isWalletLoaded(Player *p)
{
    PerPlayerData *playerData = getPerPlayerData(p);
    return playerData->walletLoaded;
}

local int areShipsLoaded(Player *p)
{
    PerPlayerData *playerData = getPerPlayerData(p);
    return playerData->shipsLoaded;
}

local LinkedList * getItemList()
{
        return &itemList;
}

local LinkedList * getStoreList(Arena *arena)
{
        PerArenaData *arenaData = getPerArenaData(arena);
        return &arenaData->storeList;
}

local LinkedList * getCategoryList(Arena *arena)
{
        PerArenaData *arenaData = getPerArenaData(arena);
        return &arenaData->categoryList;
}

local LinkedList * getShipPropertyList(Arena *arena, int ship)
{
        if (ship < 0 || 7 < ship)
        {
                lm->LogA(L_ERROR, "hscore_database", arena, "request for ship property list for ship %i.", ship);
                return NULL;
        }

        PerArenaData *arenaData = getPerArenaData(arena);
        return &arenaData->shipPropertyLists[ship];
}

local void lock()
{
        pthread_mutex_lock(&db_mutex);
}

local void unlock()
{
        pthread_mutex_unlock(&db_mutex);
}

local void updateItem(Player *p, int ship, Item *item, int newCount, int newData)
{
  if (ship < SHIP_WARBIRD || ship > SHIP_SHARK) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update item on ship %i", ship);
    return;
  }

  updateItemOnHull(p, getPlayerShipHull(p, ship), item, newCount, newData);
}

local void updateItemOnShipSet(Player *p, int ship, int shipset, Item *item, int newCount, int newData)
{
  if (ship < SHIP_WARBIRD || ship > SHIP_SHARK) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update item on ship %i", ship);
    return;
  }

  if (shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update item on shipset %i.", shipset);
    return;
  }

  updateItemOnHull(p, getPlayerHull(p, ship, shipset), item, newCount, newData);
}

local void updateItemOnHull(Player *p, ShipHull *hull, Item *item, int newCount, int newData)
{
  Link *link;
  LinkedList *inventoryList;

  if (!areShipsLoaded(p)) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update item on unloaded ships");
    return;
  }

  if (!hull) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update item on NULL hull.");
    return;
  }

  lock();

        inventoryList = &hull->inventoryEntryList;

        for (link = LLGetHead(inventoryList); link; link = link->next)
        {
                InventoryEntry *entry = link->data;

                if (entry->item == item)
                {
                        if (newCount != 0)
                        {
                                int oldCount = entry->count;
                                if (oldCount == newCount && entry->data == newData)
                                {
                                        lm->LogP(L_ERROR, "hscore_database", p, "asked to update item %s with no change", item->name);
                                }
                                else
                                {
                                        entry->count = newCount;
                                        entry->data = newData;

                                        if (!item->delayStatusWrite)
                                        {
                                                int shipID = hull->id;

                                                if (shipID == -1)
                                                {
                                                        lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id load");

                                                        while (shipID == -1)
                                                        {
                                                                shipID = hull->id; //fixme: bad?
                                                        }
                                                }

                                                mysql->Query(NULL, NULL, 0, "REPLACE INTO hs_player_ship_items VALUES (#,#,#,#)", shipID, item->id, newCount, newData);
                                        }
                                }

                                if (newCount != oldCount)
                                {
                                        DO_CBS(CB_ITEM_COUNT_CHANGED, p->arena, ItemCountChanged, (p, hull, item, entry, newCount, oldCount));
                                }
                        }
                        else
                        {
                                int shipID = hull->id;
                                int oldCount = entry->count;

                                if (shipID == -1)
                                {
                                        lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id load");

                                        while (shipID == -1)
                                        {
                                                shipID = hull->id; //fixme: bad?
                                        }
                                }

                                mysql->Query(NULL, NULL, 0, "DELETE FROM hs_player_ship_items WHERE ship_id = # AND item_id = #", shipID, item->id);

                                LLRemove(inventoryList, entry);
                                afree(entry);

                                DO_CBS(CB_ITEM_COUNT_CHANGED, p->arena, ItemCountChanged, (p, hull, item, NULL, 0, oldCount));
                        }

                        unlock();
                        return;
                }
        }

        //if we leave the for loop, we'll have to add it.

        if (newCount != 0)
        {
                InventoryEntry *entry;
                int shipID = hull->id;

                if (shipID == -1)
                {
                        lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id load");

                        while (shipID == -1)
                        {
                                shipID = hull->id; //fixme: bad
                        }
                }

                mysql->Query(NULL, NULL, 0, "REPLACE INTO hs_player_ship_items VALUES (#,#,#,#)", shipID, item->id, newCount, newData);

                entry = amalloc(sizeof(*entry));

                entry->count = newCount;
                entry->data = newData;
                entry->item = item;

                LLAdd(inventoryList, entry);

                //do item changed callback
                DO_CBS(CB_ITEM_COUNT_CHANGED, p->arena, ItemCountChanged, (p, hull, item, entry, newCount, 0));
        }
        else
        {
                lm->LogP(L_ERROR, "hscore_database", p, "asked to remove item %s not on hull.", item->name);
        }

    unlock();
}

local void updateInventory(Player *p, int ship, InventoryEntry *entry, int newCount, int newData)
{
  if ((ship < SHIP_WARBIRD || ship > SHIP_SHARK)) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update inventory on ship %i", ship);
    return;
  }

  updateInventoryOnHull(p, getPlayerShipHull(p, ship), entry, newCount, newData);
}

local void updateInventoryOnShipSet(Player *p, int ship, int shipset, InventoryEntry *entry, int newCount, int newData)
{
  if ((ship < SHIP_WARBIRD || ship > SHIP_SHARK)) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update inventory on ship %i (shipset: %i)", ship, shipset);
    return;
  }

  if (shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update inventory on shipset %i.", shipset);
    return;
  }

  updateInventoryOnHull(p, getPlayerHull(p, ship, shipset), entry, newCount, newData);
}

local void updateInventoryOnHull(Player *p, ShipHull *hull, InventoryEntry *entry, int newCount, int newData)
{
  LinkedList *inventoryList;

  if (!areShipsLoaded(p)) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update inventory on unloaded ships");
    return;
  }

  if (!hull) {
    lm->LogP(L_ERROR, "hscore_database", p, "asked to update inventory on NULL hull.");
    return;
  }

  lock();

        inventoryList = &hull->inventoryEntryList;

        if (newCount != 0)
        {
                if (entry->count == newCount && entry->data == newData)
                {
                        lm->LogP(L_ERROR, "hscore_database", p, "asked to update inventory entry %s with no change", entry->item->name);
                }
                else
                {
                        int oldCount = entry->count;
                        entry->count = newCount;
                        entry->data = newData;

                        if (!entry->item->delayStatusWrite)
                        {
                                int shipID = hull->id;

                                if (shipID == -1)
                                {
                                        lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id load");

                                        while (shipID == -1)
                                        {
                                                shipID = hull->id; //fixme: bad
                                        }
                                }

                                mysql->Query(NULL, NULL, 0, "REPLACE INTO hs_player_ship_items VALUES (#,#,#,#)", shipID, entry->item->id, newCount, newData);
                        }

                        if (newCount != oldCount)
                        {
                                DO_CBS(CB_ITEM_COUNT_CHANGED, p->arena, ItemCountChanged, (p, hull, entry->item, entry, newCount, oldCount));
                        }
                }
        }
        else
        {
                int shipID = hull->id;

                Item *item = entry->item; //save it for later
                int oldCount = entry->count; //save it for later

                if (shipID == -1)
                {
                        lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id load");

                        while (shipID == -1)
                        {
                                shipID = hull->id; //fixme: bad
                        }
                }

                mysql->Query(NULL, NULL, 0, "DELETE FROM hs_player_ship_items WHERE ship_id = # AND item_id = #", shipID, entry->item->id);

                LLRemove(inventoryList, entry);
                afree(entry);

                DO_CBS(CB_ITEM_COUNT_CHANGED, p->arena, ItemCountChanged, (p, hull, item, NULL, 0, oldCount));
        }

    unlock();
}

local void addShip(Player *p, int ship) //the ships id may not be valid until later
{
  addShipToShipSet(p, ship, getPlayerShipSet(p));
}

local void removeShip(Player *p, int ship)
{
  removeShipFromShipSet(p, ship, getPlayerShipSet(p));
}

local void addShipToShipSet(Player *p, int ship, int shipset)
{
  PerPlayerData *pdata;
  PerArenaData *adata;
  int hid;

  if (p && (pdata = getPerPlayerData(p)) && (adata = getPerArenaData(p->arena))) {
    if ((ship < SHIP_WARBIRD || ship > SHIP_SHARK) || (shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS)) {
      lm->LogP(L_ERROR, "hscore_database", p, "Asked to add ship %i to shipset %i.", ship, shipset);
      return;
    }

    if (!isWalletLoaded(p)) {
      lm->LogP(L_ERROR, "hscore_database", p, "Asked to add ship for a player without a loaded wallet.");
      return;
    }

    if (pdata->hull[(hid = ship + shipset * 8)]) {
      lm->LogP(L_ERROR, "hscore_database", p, "Asked to add owned ship %i to shipset %i (hull id: %i).", ship, shipset, hid);
      return;
    }

    lock();

    ShipHull *hull = amalloc(sizeof(*hull));

    LLInit(&hull->inventoryEntryList);
    hull->propertySums = HashAlloc();
    hull->ship = ship;
    hull->id = -1;

    hull->propertyList = &adata->shipPropertyLists[ship];
    pdata->hull[hid] = hull;

    mysql->Query(NULL, NULL, 0, "INSERT INTO hs_player_ships VALUES (NULL, #, #)", pdata->id, hid);

    PlayerReference *ref = getPlayerReference(p, p->arena);
    mysql->Query(loadShipIDQueryCallback, ref, 1, "SELECT id, ship FROM hs_player_ships WHERE player_id = # AND ship = #", pdata->id, hid);

    unlock();
  }
}

local void removeShipFromShipSet(Player *p, int ship, int shipset)
{
  PerPlayerData *pdata;
  int sid, hid;

  if (p && (pdata = getPerPlayerData(p))) {
    if ((ship < SHIP_WARBIRD || ship > SHIP_SHARK) || (shipset < 0 || shipset > HSCORE_MAX_SHIPSETS)) {
      lm->LogP(L_ERROR, "hscore_database", p, "Asked to remove ship %i from shipset %i.", ship, shipset);
      return;
    }

    if (!isWalletLoaded(p)) {
      lm->LogP(L_ERROR, "hscore_database", p, "Asked to remove ship for an unloaded player.");
      return;
    }

    if (!pdata->hull[(hid = ship + shipset * 8)]) {
      lm->LogP(L_ERROR, "hscore_database", p, "Asked to removed unowned ship %i from shipset %i.", ship, shipset);
      return;
    }

    // We're good to go!
    if ((sid = pdata->hull[hid]->id) == -1) {
      lm->LogP(L_DRIVEL, "hscore_database", p, "waiting for ship id %i load", sid);

      // Impl note:
      // This seems like a terrible terrible idea, but I'm trying to minimize my changes here. -C
      while (sid == -1) {
        sid = pdata->hull[hid]->id;
      }
    }

    // Empty the ship
    mysql->Query(NULL, NULL, 0, "DELETE FROM hs_player_ship_items WHERE ship_id = #", sid);

    // Delete the ship
    mysql->Query(NULL, NULL, 0, "DELETE FROM hs_player_ships WHERE id = #", sid);

    lock();
    UnloadPlayerShip(pdata->hull[hid]);
    unlock();

    pdata->hull[hid] = NULL;
  }
}



local void addMoney(Player *p, MoneyType type, int amount)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);

        pdata->money += amount;
        pdata->moneyType[type] += amount;
    } else if (p->type != T_FAKE) {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to add money before wallet has loaded.");
    }
}

local void setMoney(Player *p, MoneyType type, int amount)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);
        int diff = amount - pdata->money;

        pdata->money = amount;
        pdata->moneyType[type] += diff;
    } else if (p->type != T_FAKE) {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to set money before wallet has loaded.");
    }
}

local int getMoney(Player *p)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);

        return pdata->money;
    } else if (p->type != T_FAKE) {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to get money before wallet has loaded.");
    }

    return 0;
}

local int getMoneyType(Player *p, MoneyType type)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);

        return pdata->moneyType[type];
    } else {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to get money before wallet has loaded.");
    }

    return 0;
}

local void addExp(Player *p, int amount)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);

        pdata->exp += amount;
    } else if (p->type != T_FAKE) {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to add experience before wallet has loaded.");
    }
}

local void setExp(Player *p, int amount)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);

        pdata->exp = amount;
    } else if (p->type != T_FAKE) {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to set experience before wallet has loaded.");
    }
}

local int getExp(Player *p)
{
    if (isWalletLoaded(p)) {
        PerPlayerData *pdata = getPerPlayerData(p);

        return pdata->exp;
    } else if (p->type != T_FAKE) {
        lm->LogP(L_WARN, "hscore_database", p, "Tried to get experience before wallet has loaded.");
    }

    return 0;
}




//getPerPlayerData declared elsewhere

local int getPlayerShipSet(Player *p)
{
  PerPlayerData *pdata;
  return (p && (pdata = getPerPlayerData(p))) ? pdata->shipset : -1;
}

local int setPlayerShipSet(Player *p, int shipset)
{
  PerPlayerData *pdata;

  if (p && (pdata = getPerPlayerData(p)) && (shipset > -1 && shipset < HSCORE_MAX_SHIPSETS)) {
    int prev = pdata->shipset;
    pdata->shipset = shipset;

    DO_CBS(CB_SHIPSET_CHANGED, p->arena, ShipSetChanged, (p, prev, pdata->shipset));

    return prev;
  }

  return -1;
}

local ShipHull* getPlayerHull(Player *p, int ship, int shipset)
{
  PerPlayerData *pdata;

  if (p && (pdata = getPerPlayerData(p)) && (ship >= SHIP_WARBIRD && ship <= SHIP_SHARK) && (shipset >= 0 && shipset < HSCORE_MAX_SHIPSETS)) {
    if (areShipsLoaded(p)) {
      int hid = ship + (shipset * 8);
      return (hid >= 0 && hid < HSCORE_MAX_HULLS) ? pdata->hull[hid] : NULL;
    }
  }

  return NULL;
}

local ShipHull* getPlayerShipHull(Player *p, int ship)
{
  PerPlayerData *pdata;
  return (p && (pdata = getPerPlayerData(p))) ? getPlayerHull(p, ship, pdata->shipset) : NULL;
}

local ShipHull* getPlayerCurrentHull(Player *p)
{
  PerPlayerData *pdata;
  return (p && (pdata = getPerPlayerData(p))) ? getPlayerHull(p, p->p_ship, pdata->shipset) : NULL;
}





local Ihscoredatabase interface =
{
  INTERFACE_HEAD_INIT(I_HSCORE_DATABASE, "hscore_database")
  getPlayerWalletId, isWalletLoaded, areShipsLoaded,
  getItemList, getStoreList, getCategoryList, getShipPropertyList,
  lock, unlock,
  updateItem, updateItemOnShipSet, updateItemOnHull,
  updateInventory, updateInventoryOnShipSet, updateInventoryOnHull,
  addShip, addShipToShipSet,
  removeShip, removeShipFromShipSet,

  addMoney, setMoney, getMoney, getMoneyType,
  addExp, setExp, getExp,

  // Shipset additions
  getPlayerShipSet, setPlayerShipSet,
  getPlayerHull, getPlayerShipHull, getPlayerCurrentHull
};


EXPORT const char info_hscore_database[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_database(int action, Imodman *_mm, Arena *arena)
{
    // All of this shit should be per-arena. :/

        if (action == MM_LOAD)
        {
                mm = _mm;

                lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
                chat = mm->GetInterface(I_CHAT, ALLARENAS);
                cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
                cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
                mysql = mm->GetInterface(I_HSCORE_MYSQL, ALLARENAS);
                aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
                pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
                ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

                if (!lm || !chat || !cfg || !cmd || !mysql || !aman || !pd || !ml)
                {
                        goto fail;
                }

                // Lock allocation.
                if (pthread_mutexattr_init(&db_mutex_attr)) {
                    goto fail;
                }

                // Set lock to be reentrant so we don't need modules to care about lock state.
                pthread_mutexattr_settype(&db_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
                if (pthread_mutex_init(&db_mutex, &db_mutex_attr)) {
                    pthread_mutexattr_destroy(&db_mutex_attr);
                    goto fail;
                }

                playerDataKey = pd->AllocatePlayerData(sizeof(PerPlayerData));
                if (playerDataKey == -1)
                {
                        goto fail;
                }

                arenaDataKey = aman->AllocateArenaData(sizeof(PerArenaData));
                if (arenaDataKey == -1)
                {
                        pd->FreePlayerData(playerDataKey);
                        goto fail;
                }




                LLInit(&itemList);
                LLInit(&itemTypeList);

                initTables();

                LoadItemTypeList();

                mm->RegCallback(CB_NEWPLAYER, allocatePlayerCallback, ALLARENAS);
                mm->RegCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);
                mm->RegCallback(CB_ARENAACTION, arenaActionCallback, ALLARENAS);

                mm->RegInterface(&interface, ALLARENAS);

                cmd->AddCommand("reloaditems", reloadItemsCommand, ALLARENAS, reloadItemsHelp);
                cmd->AddCommand("storeall", storeAllCommand, ALLARENAS, storeAllHelp);
                cmd->AddCommand("resetyesiknowwhatimdoing", resetCommand, ALLARENAS, resetHelp);
                cmd->AddCommand("refund", refundCommand, ALLARENAS, refundHelp);

                ml->SetTimer(periodicStoreTimer, 30000, 30000, NULL, NULL);

                return MM_OK;

        fail:
                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(chat);
                mm->ReleaseInterface(cfg);
                mm->ReleaseInterface(cmd);
                mm->ReleaseInterface(mysql);
                mm->ReleaseInterface(aman);
                mm->ReleaseInterface(pd);
                mm->ReleaseInterface(ml);

                return MM_FAIL;

        }
        else if (action == MM_UNLOAD)
        {
                if (mm->UnregInterface(&interface, ALLARENAS))
                {
                        return MM_FAIL;
                }

                ml->ClearTimer(periodicStoreTimer, NULL);

                cmd->RemoveCommand("reloaditems", reloadItemsCommand, ALLARENAS);
                cmd->RemoveCommand("storeall", storeAllCommand, ALLARENAS);
                cmd->RemoveCommand("resetyesiknowwhatimdoing", resetCommand, ALLARENAS);
                cmd->RemoveCommand("refund", refundCommand, ALLARENAS);

                mm->UnregCallback(CB_NEWPLAYER, allocatePlayerCallback, ALLARENAS);
                mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);
                mm->UnregCallback(CB_ARENAACTION, arenaActionCallback, ALLARENAS);

                StoreAllPerPlayerData();

                UnloadAllPerPlayerData();
                UnloadAllPerArenaData();

                UnloadItemList();
                UnloadItemTypeList();

                pd->FreePlayerData(playerDataKey);
                aman->FreeArenaData(arenaDataKey);

                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(chat);
                mm->ReleaseInterface(cfg);
                mm->ReleaseInterface(cmd);
                mm->ReleaseInterface(mysql);
                mm->ReleaseInterface(aman);
                mm->ReleaseInterface(pd);
                mm->ReleaseInterface(ml);

                // Release lock and lock attributes
                if (pthread_mutex_destroy(&db_mutex) || pthread_mutexattr_destroy(&db_mutex_attr)) {
                    return MM_FAIL;
                }

                return MM_OK;
        }
        return MM_FAIL;

}
