#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_shipnames.h"
#include "hscore_spawner.h"
#include "selfpos.h"

typedef struct RemovalEntry
{
	int count; //to remove, negative for add
	Item *item;
} RemovalEntry;

//modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Icmdman *cmd;
local Iplayerdata *pd;
local Igame *game;
local Ihscoredatabase *database;
local Imainloop *ml;
local Iconfig *cfg;
local Iselfpos *selfpos;

//a few internal functions
local void recalculateEntireCache(Player *p, int ship);
local void recalculateEntireCacheForShipSet(Player *p, int ship, int shipset);
local void recalculateEntireCacheForHull(Player *p, ShipHull *hull);

//interface prototypes
local int getItemCount(Player *p, Item *item, int ship);
local int getItemCountOnShipSet(Player *p, Item *item, int ship, int shipset);
local int getItemCountOnHull(Player *p, Item *item, ShipHull* hull);

local int addItem(Player *p, Item *item, int ship, int amount);
local int addItemToShipSet(Player *p, Item *item, int ship, int shipset, int amount);
local int addItemToHull(Player *p, Item *item, ShipHull *hull, int amount);

local int addItemCheckLimits(Player *p, Item *item, int ship, int amount);
local int addItemToShipSetCheckLimits(Player *p, Item *item, int ship, int shipset, int amount);
local int addItemToHullCheckLimits(Player *p, Item *item, ShipHull *hull, int amount);

local Item * getItemByName(const char *name, Arena *arena);
local Item * getItemByPartialName(const char *name, Arena *arena);

local int getPropertySum(Player *p, int ship, const char *prop, int def);
local int getPropertySumOnShipSet(Player *p, int ship, int shipset, const char *prop, int def);
local int getPropertySumOnHull(Player *p, ShipHull *hull, const char *prop, int default_value);

local void triggerEvent(Player *p, int ship, const char *event);
local void triggerEventOnShipSet(Player *p, int ship, int shipset, const char *event);
local void triggerEventOnHull(Player *p, ShipHull *hull, const char *event);
local void triggerEventOnItem(Player *p, Item *item, int ship, const char *event);
local void triggerEventOnShipSetItem(Player *p, Item *item, int ship, int shipset, const char *event);
local void triggerEventOnHullItem(Player *p, Item *item, ShipHull *hull, const char *event);

local int getFreeItemTypeSpots(Player *p, ItemType *type, int ship);
local int getFreeItemTypeSpotsOnShipSet(Player *p, ItemType *type, int ship, int shipset);
local int getFreeItemTypeSpotsOnHull(Player *p, ItemType *type, ShipHull *hull);

local int hasItemsLeftOnShip(Player *p, int ship);
local int hasItemsLeftOnShipSetShip(Player *p, int ship, int shipset);
local int hasItemsLeftOnHull(Player *p, ShipHull *hull);





local helptext_t itemInfoHelp =
"Targets: none\n"
"Args: <item>\n"
"Shows you information about the specified item.\n";

local void itemInfoCommand(const char *command, const char *params, Player *p, const Target *target)
{
	Item *item;
	char shipMask[] = "12345678";
	int i;

	char itemTypes[256];
	int first = 1;

	char buf[256];
	char ammoString[32];
	char propString[32];
	Link *link;

	if (*params == '\0')
	{
		chat->SendMessage(p, "Please use the ?buy menu to look up items.");
		return;
	}

	item = getItemByPartialName(params, p->arena);

	if (item == NULL)
	{
		chat->SendMessage(p, "No item named %s in this arena", params);
		return;
	}

	//FIXME: add ammo

	chat->SendMessage(p, "+----------------------------------+");
	chat->SendMessage(p, "| %-32s |", item->name);
	chat->SendMessage(p, "+-----------+------------+-------+-+--------+-----+------------------+--------------------------------+");
	chat->SendMessage(p, "| Buy Price | Sell Price | Exp   | Ships    | Max | Ammo             | Item Types                     |");

	//calculate ship mask
	for (i = 0; i < 8; i++)
	{
		if (!((item->shipsAllowed >> i) & 0x1))
		{
			shipMask[i] = ' ';
		}
	}

	//get item type string
	database->lock();
	itemTypes[0] = '\0';
	for (link = LLGetHead(&item->itemTypeEntries); link; link = link->next)
	{
		ItemTypeEntry *entry = link->data;

		if (first == 1)
		{
			first = 0;

			if (entry->delta == 1)
			{
				sprintf(buf, "%s", entry->itemType->name);
			}
			else
			{
				sprintf(buf, "%d %s", entry->delta, entry->itemType->name);
			}
		}
		else
		{
			if (entry->delta == 1)
			{
				sprintf(buf, ", %s", entry->itemType->name);
			}
			else
			{
				sprintf(buf, ", %d %s", entry->delta, entry->itemType->name);
			}
		}

		strcat(itemTypes, buf);
	}
	database->unlock();

	//get the ammo string
	if (item->ammo != NULL)
	{
		if (item->minAmmo != 1)
		{
			sprintf(ammoString, "%d %s", item->minAmmo, item->ammo->name);
		}
		else
		{
			sprintf(ammoString, "%s", item->ammo->name);
		}
	}
	else
	{
		sprintf(ammoString, "None");
	}

	chat->SendMessage(p, "| $%-8i | $%-9i | %-5i | %s | %-3i | %-16s | %-30s |", item->buyPrice, item->sellPrice, item->expRequired, shipMask, item->max, ammoString, itemTypes);
	chat->SendMessage(p, "+-----------+------------+-------+----------+-----+------------------+--------------------------------+");

	//print description
	for (char *pos = item->longDesc; pos;) {
		char *brk = strstr(pos, "\\n");
		chat->SendMessage(p, "| %-99.99s |", pos);

		if (brk && (brk - pos < 99)) {
			pos += (brk ? brk - pos : 99);
		} else if (brk || strlen(pos) > 99) {
			pos += 99;
		} else {
			pos = 0;
		}
	}

	database->lock();
	link = LLGetHead(&item->propertyList);
	if (link)
	{
		//print item properties
		chat->SendMessage(p, "+----------------------------------+------------------+-----------------------------------------------+");
		chat->SendMessage(p, "| Property Name                    | Value            |");
		chat->SendMessage(p, "+----------------------------------+------------------+");
		while (link)
		{
			Property *prop = link->data;
			char ignoreCountChar = prop->ignoreCount ? '!' : ' ';

			if (prop->absolute)
			{
				sprintf(propString, "=%i%c", prop->value, ignoreCountChar);
			}
			else
			{
				sprintf(propString, "%+i%c", prop->value, ignoreCountChar);
			}

			chat->SendMessage(p, "| %-32s | %-16s |", prop->name, propString);
			link = link->next;
		}
		chat->SendMessage(p, "+----------------------------------+------------------+");
	}
	else
	{
		//no item properties
		chat->SendMessage(p, "+-----------------------------------------------------------------------------------------------------+");
	}
	database->unlock();
}

local helptext_t grantItemHelp =
"Targets: player or freq or arena\n"
"Args: [-f] [-q] [-i] [-c <amount>] [-s <ship #>] [-ss <shipset #>] <item>\n"
"Adds the specified item to the inventory of the targeted players.\n"
"If {-s} is not specified, the item is added to the player's current ship.\n"
"If {-ss} is not specified, the item is added to the player's current shipset.\n"
"Will only effect speccers if {-s} is used. The added amount defaults to 1.\n"
"For typo safety, the {-f} must be specified when granting to more than one player.\n"
"The {-i} flag ignores ship requirements (single receipient only).";

local void grantItemCommand(const char *command, const char *params, Player *p, const Target *target)
{
	int force = 0;
	int quiet = 0;
	int count = 1;
	int ship = 0;
	int ignore = 0;
	int shipset = 0;
	const char *itemName;
	Item *item;
	int newItemCount;

	char *next; //for strtol

	while (params != NULL) //get the flags
	{
		if (*params == '-')
		{
			params++;
			if (*params == '\0')
			{
				chat->SendMessage(p, "Grantitem: invalid usage.");
				return;
			}

			else if (*params == 'f')
			{
				force = 1;

				params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Grantitem: invalid usage.");
					return;
				}
			}
			else if (*params == 'i')
			{
				ignore = 1;

				params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Grantitem: invalid usage.");
					return;
				}
			}
			else if (*params == 'q')
			{
				quiet = 1;

				params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Grantitem: invalid usage.");
					return;
				}
			}
			else if (*params == 'c')
			{
				params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Grantitem: invalid usage.");
					return;
				}

				count = strtol(params, &next, 0);

				if (next == params)
				{
					chat->SendMessage(p, "Grantitem: bad count.");
					return;
				}

				params = next;
			}
			else if (*params == 's')
			{
				switch (*(++params)) {
				  case ' ':
					// ship argument
					ship = strtol(params, &next, 0);

					if (next == params) {
					  chat->SendMessage(p, "Grantitem: bad ship value.");
					  return;
					}

					params = next;
					break;

				  case 's':
					params = strchr(params, ' ');
					// shipset argument

					if (params)
					  ++params;

					shipset = strtol(params, &next, 0);

					if (next == params) {
					  chat->SendMessage(p, "Grantitem: bad shipset value.");
					  return;
					}

					params = next;
					break;
				}

			}
		}
		else if (*params == ' ')
		{
			params++;
		}
		else if (*params == '\0')
		{
			chat->SendMessage(p, "Grantitem: invalid usage.");
			return;
		}
		else
		{
			itemName = params;
			break;
		}
	}

	//finished parsing

	//set ship from 0-7
	--ship;
	--shipset;

	//verify ship
	if (ship < -1 || 7 < ship) //-1 is special, means current ship.
	{
		chat->SendMessage(p, "Grantitem: Ship out of range. Please choose a ship from 1 to 8.");
		return;
	}

	if (shipset < -1 || shipset >= HSCORE_MAX_SHIPSETS) {
	  chat->SendMessage(p, "Grantitem: Shipset out of range. Please choose a shipset from 1 to %i, inclusive.", HSCORE_MAX_SHIPSETS);
	  return;
	}

	//verify count
	if (count == 0)
	{
		chat->SendMessage(p, "Grantitem: Bad count.");
		return;
	}

	//verify item
	item = getItemByName(itemName, p->arena);
	if (item == NULL)
	{
		chat->SendMessage(p, "Grantitem: No item %s in this arena.", itemName);
		return;
	}

	if (target->type == T_PLAYER) //private command
	{
		Player *t = target->u.p;

		if (!force)
		{
			if (database->areShipsLoaded(t))
			{
				if (ship == -1) {
					if (t->p_ship != SHIP_SPEC) {
						ship = t->p_ship;
					} else {
						chat->SendMessage(p, "Cannot grant item: player %s is in spec.", t->name);
						return;
					}
				}

				if (shipset == -1) {
				  shipset = database->getPlayerShipSet(t);
				}

				newItemCount = getItemCount(t, item, ship) + count;
				if (ignore || item->max == 0 || newItemCount <= item->max)
				{
					if (ignore || (item->shipsAllowed >> ship) & 0x1)
					{
						Link *link;
						database->lock();
						for (link = LLGetHead(&item->itemTypeEntries); link; link = link->next)
						{
							ItemTypeEntry *entry = link->data;

							if (entry->itemType != NULL)
							{
								if (!ignore && getFreeItemTypeSpots(t, entry->itemType, ship) - (entry->delta * count) < 0) //have no free spots
								{
									chat->SendMessage(p, "Does not have enough free %s spots.", entry->itemType->name);

									database->unlock();
									return;
								}
							}
						}
						database->unlock();


						if (!ignore)
						{
							LinkedList advisers = LL_INITIALIZER;
							Ahscoreitems *adviser;
							int canGrant = 1;

							mm->GetAdviserList(A_HSCORE_ITEMS, p->arena, &advisers);
							FOR_EACH(&advisers, adviser, link)
							{
								if (adviser->CanGrantItem)
								{
									if (!adviser->CanGrantItem(p, t, item, ship, shipset, count))
									{
										canGrant = 0;
									}
								}
							}
							mm->ReleaseAdviserList(&advisers);

							if (!canGrant)
							{
								return;
							}
						}


						addItemToShipSet(t, item, ship, shipset, count);
						if (ship == t->p_ship)
						{
							if (count > 0) {
								int i;
								for (i = 0; i < -count; i++) {
									triggerEventOnShipSetItem(t, item, ship, shipset, "add");
								}
							} else {
								int i;
								for (i = 0; i < -count; i++) {
									triggerEventOnShipSetItem(t, item, ship, shipset, "del");
								}
							}
						}

						if (!quiet)
						{
							chat->SendMessage(t, "You were granted %i of item %s on your %s (shipset %i).", count, item->name, shipNames[ship], shipset + 1);
							chat->SendMessage(p, "You granted %i of item %s to player %s (shipset %i)", count, item->name, t->name, shipset + 1);
						}
						else
						{
							chat->SendMessage(p, "You quietly granted %i of item %s to player %s (shipset %i).", count, item->name, t->name, shipset + 1);
						}
					}
					else
					{
						chat->SendMessage(p, "Item %s not allowed on ship %d.", item->name, ship + 1);
					}
				}
				else
				{
					chat->SendMessage(p, "%i would exceed the max of %i for item %s.", newItemCount, item->max, item->name);
				}
			}
			else
			{
				chat->SendMessage(p, "Player %s has no ships loaded.", t->name);
			}
		}
		else
		{
			chat->SendMessage(p, "Whoa there, bud. The -f is only for arena and freq targets.");
		}
	}
	else //not private
	{
		if (ship != -1 || shipset != -1) {
		  chat->SendMessage(p, "Invalid usage: A ships and shipsets cannot be use with multi-player item grants.");
		  return;
		}

		if (force)
		{
			LinkedList set = LL_INITIALIZER;
			Link *link;
			int playerCount;
			pd->TargetToSet(target, &set);

			for (link = LLGetHead(&set); link; link = link->next)
			{
				Player *t = link->data;
				int ok = 1;

				ship = t->p_ship;
				shipset = database->getPlayerShipSet(t);

				if (database->areShipsLoaded(t))
				{
					if (ship != SHIP_SPEC)
					{
						int newItemCount = getItemCountOnShipSet(t, item, ship, shipset) + count;
						if (item->max == 0 || newItemCount <= item->max)
						{
							Link *link;
							database->lock();
							for (link = LLGetHead(&item->itemTypeEntries); link; link = link->next)
							{
								ItemTypeEntry *entry = link->data;

								if (entry->itemType != NULL)
								{
									if (!ignore && getFreeItemTypeSpotsOnShipSet(t, entry->itemType, ship, shipset) - (entry->delta * count) < 0) //have no free spots
									{
										chat->SendMessage(p, "Player %s does not have enough free %s spots.", t->name, entry->itemType->name);

										ok = 0;
										break;
									}
								}
							}
							database->unlock();

							if (!ignore && !((item->shipsAllowed >> ship) & 0x1))
							{
								chat->SendMessage(p, "Player %s cannot hold item %s on ship %d (shipset %i).", t->name, item->name, ship + 1, shipset + 1);
								ok = 0;
							}

							if (!ignore)
							{
								LinkedList advisers = LL_INITIALIZER;
								Ahscoreitems *adviser;

								mm->GetAdviserList(A_HSCORE_ITEMS, p->arena, &advisers);
								FOR_EACH(&advisers, adviser, link)
								{
									if (adviser->CanGrantItem && !adviser->CanGrantItem(p, t, item, ship, shipset, count)) {
										ok = 0;
									}
								}
								mm->ReleaseAdviserList(&advisers);
							}

							if (ok)
							{
								addItem(t, item, ship, count);
								if (!quiet)
								{
									chat->SendMessage(t, "You were granted %i of item %s on your %s (shipset %i).", count, item->name, shipNames[t->p_ship], shipset + 1);
								}
							}
						}
						else
						{
							chat->SendMessage(p, "Giving Player %s %i of item %s would exceed the max of %i.", t->name, newItemCount, item->name, item->max);
						}
					}
					else
					{
						chat->SendMessage(p, "Player %s is in spec.", t->name);
					}
				}
				else
				{
					chat->SendMessage(p, "Player %s has no ships loaded.", t->name);
				}
			}

			playerCount = LLCount(&set);

			LLEmpty(&set);

			chat->SendMessage(p, "You gave %i of item %s to %i players.", count, item->name, playerCount);
		}
		else
		{
			chat->SendMessage(p, "For typo safety, the -f must be specified for arena and freq targets.");
		}
	}
}

local int timer_callback(void *clos)
{
	Player *p = clos;

	Target t;
	//Ihscorespawner *spawner;
	t.type = T_PLAYER;
	t.u.p = p;

	// Impl note:
	// Shipresets now trigger respawning, so we don't need to
	// explicitly ask for it.
	game->ShipReset(&t);


	// Temporarily disabled until we know for certain
	// the changes in spawner have no long-term negatives.
	/*
	spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
	if (spawner)
	{
		spawner->respawn(p);
		mm->ReleaseInterface(spawner);
	}
	*/

	return FALSE;
}

local void doEvent(Player *p, InventoryEntry *entry, Event *event, LinkedList *updateList) //called with lock held
{
	int action = event->action;

	//do the message
	if (event->message[0] != '\0')
	{
		if (action == ACTION_ARENA_MESSAGE)
		{
			chat->SendArenaMessage(p->arena, "%s", event->message);
		}
		else
		{
			chat->SendMessage(p, "%s", event->message);
		}
	}

	//do the action
	if (action == ACTION_NO_ACTION) //do nothing
	{
		//nothing :)
	}
	else if (action == ACTION_REMOVE_ITEM) //removes event->data amount of the items from the ship's inventory
	{
		if (entry != NULL)
		{
			//add to the list for removal
			RemovalEntry *re = amalloc(sizeof(*re));
			re->count = event->data;
			re->item = entry->item;
			LLAdd(updateList, re);
		}
		else
		{
			lm->LogP(L_ERROR, "hscore_items", p, "Item tried to run event %d with null entry.", action);
		}
	}
	else if (action == ACTION_REMOVE_ITEM_AMMO) //removes event->data amount of the item's ammo type from inventory
	{
		if (entry != NULL)
		{
			RemovalEntry *re = amalloc(sizeof(*re));
			re->count = event->data;
			re->item = entry->item->ammo;
			LLAdd(updateList, re);
		}
		else
		{
			lm->LogP(L_ERROR, "hscore_items", p, "Item tried to run event %d with null entry.", action);
		}
	}
	else if (action == ACTION_PRIZE) //sends prize #event->data to the player
	{
		int prize = event->data % 100;
		int count = abs(event->data / 100);
		Target t;

		if (count == 0)
		{
			count = 1;
		}


		t.type = T_PLAYER;
		t.u.p = p;

		game->GivePrize(&t, prize, count);
	}
	else if (action == ACTION_SET_INVENTORY_DATA) //sets the item's inventory data to event->data.
	{
		if (entry != NULL)
		{
			database->updateInventory(p, p->p_ship, entry, entry->count, event->data);
		}
		else
		{
			lm->LogP(L_ERROR, "hscore_items", p, "Item tried to run event %d with null entry.", action);
		}
	}
	else if (action == 	ACTION_INCREMENT_INVENTORY_DATA) //does a ++ on inventory data.
	{
		if (entry != NULL)
		{
			database->updateInventory(p, p->p_ship, entry, entry->count, entry->data + 1);
		}
		else
		{
			lm->LogP(L_ERROR, "hscore_items", p, "Item tried to run event %d with null entry.", action);
		}
	}
	else if (action == ACTION_DECREMENT_INVENTORY_DATA) //does a -- on inventory data. A "datazero" event may be generated as a result.
	{
		if (entry != NULL)
		{
			int newData = entry->data - 1;
			if (newData < 0)
			{
				newData = 0;
			}

			database->updateInventory(p, p->p_ship, entry, entry->count, newData);

			if (newData == 0)
			{
				Link *eventLink = LLGetHead(&entry->item->eventList);
				while (eventLink != NULL)
				{
					Event *eventToCheck = eventLink->data;
					eventLink = eventLink->next;

					if (strcmp(eventToCheck->event, "datazero") == 0)
					{
						doEvent(p, entry, eventToCheck, updateList);
					}
				}
			}
		}
		else
		{
			lm->LogP(L_ERROR, "hscore_items", p, "Item tried to run event %d with null entry.", action);
		}
	}
	else if (action == ACTION_SPEC) //Specs the player.
	{
		game->SetShipAndFreq(p, SHIP_SPEC, p->arena->specfreq);
	}
	else if (action == ACTION_SHIP_RESET) //sends a shipreset packet and reprizes all items (antideath, really)
	{
		int respawnTime = cfg->GetInt(p->arena->cfg, "Kill", "EnterDelay", 0);
		int delay = respawnTime - 50;
		if (delay < 1) delay = 1;

		ml->ClearTimer(timer_callback, p);
		ml->SetTimer(timer_callback, delay, delay, p, p);
	}
	else if (action == ACTION_CALLBACK) //calls a callback passing an eventid of event->data.
	{
		DO_CBS(CB_EVENT_ACTION, p->arena, eventActionFunction, (p, event->data));
	}
	else if (action == ACTION_ARENA_MESSAGE)
	{
		// Do nothing. it was already taken care of above.
	}
	else if (action == ACTION_SET_BOUNTY)
	{
		selfpos->SetBounty(p, event->data);
	}
	else if (action == ACTION_ADD_BOUNTY)
	{
		if (p->p_ship != SHIP_SPEC)
		{
			int newBounty = event->data + p->position.bounty;
			int initialBounty = cfg->GetInt(p->arena->cfg, shipNames[p->p_ship], "InitialBounty", 0);

			if (newBounty < initialBounty)
				newBounty = initialBounty;

			if (newBounty != p->position.bounty)
				selfpos->SetBounty(p, newBounty);
		}
	}
	else if (action == ACTION_IGNORE_PRIZE)
	{
		Ihscorespawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
		if (spawner)
		{
			spawner->ignorePrize(p, event->data);
			mm->ReleaseInterface(spawner);
		}
		else
		{
			lm->LogP(L_ERROR, "hscore_items", p, "Can't locate spawner module for ignore prize action");
		}
	}
	else
	{
		lm->LogP(L_ERROR, "hscore_items", p, "Unknown action code %i", action);
	}
}

local int getItemCount(Player *p, Item *item, int ship) //call without lock held
{
	if (ship < 0 || 7 < ship) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item count on ship %i", ship);
		return 0;
	}

	return getItemCountOnHull(p, item, database->getPlayerShipHull(p, ship));
}

local int getItemCountOnShipSet(Player *p, Item *item, int ship, int shipset)
{
	if (ship < 0 || 7 < ship || shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item count on ship %i (shipset %i)", ship, shipset);
		return 0;
	}

	return getItemCountOnHull(p, item, database->getPlayerHull(p, ship, shipset));
}

local int getItemCountOnHull(Player *p, Item *item, ShipHull *hull)
{
	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item count on a player with unloaded ships");
		return 0;
	}

	if (!hull) {
		lm->LogP(L_ERROR, "hscore_items", p, "getItemCountOnHull: Requested item count on null hull.");
		return 0;
	}

	if (!item) {
		lm->LogP(L_ERROR, "hscore_items", p, "getItemCountOnHull: Requested item count for null item.");
		return 0;
	}

	LinkedList *inventoryList;
	Link *link;
	int count = 0;

	database->lock();

	inventoryList = &hull->inventoryEntryList;

	for (link = LLGetHead(inventoryList); link; link = link->next) {
		InventoryEntry *entry = link->data;

		if (entry->item == item) {
			count = entry->count;
			break;
		}
	}

	database->unlock();
	return count;
}

local int deleteCacheEntry(const char *key, void *val, void *clos)
{
	afree(val);
	return 1;
}

local int addItem(Player *p, Item *item, int ship, int amount)
{
	if (ship < 0 || 7 < ship) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to ship %i", ship);
		return 0;
	}

	return addItemToHull(p, item, database->getPlayerShipHull(p, ship), amount);
}

local int addItemToShipSet(Player *p, Item *item, int ship, int shipset, int amount)
{
	if (ship < 0 || 7 < ship || shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to ship %i (shipset %i)", ship, shipset);
		return 0;
	}

	return addItemToHull(p, item, database->getPlayerHull(p, ship, shipset), amount);
}

local int addItemToHull(Player *p, Item *item, ShipHull *hull, int amount)
{
	Link *link;
	Link *propLink;
	LinkedList *inventoryList;
	int data = 0;
	int count = 0;
	int doInit = 0;
	int oldCount = 0;
	Link *ammoLink;

	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to a player with unloaded ships");
		return 0;
	}

	if (!hull) {
		lm->LogP(L_ERROR, "hscore_items", p, "addItemToHull: Requested item count on null hull.");
		return 0;
	}

	if (!item) {
		lm->LogP(L_ERROR, "hscore_items", p, "addItemToHull: Requested item count for null item.");
		return 0;
	}

	if (amount == 0) {
		lm->LogP(L_DRIVEL, "hscore_items", p, "addItemToHull: asked to add 0 of item %s", item->name);
		//return 0;
	}

	database->lock();

	inventoryList = &hull->inventoryEntryList;

	for (link = LLGetHead(inventoryList); link; link = link->next)
	{
		InventoryEntry *entry = link->data;

		if (entry->item == item)
		{
			data = entry->data;
			count = entry->count;
			oldCount = entry->count;
			break;
		}
	}

	if (count == 0 && amount != 0)
	{
		//we need to do an init event.
		doInit = 1;
	}

	count += amount;

	if (count < 0)
	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to set item %s count to %i. Setting to 0.", item->name, count);
		count = 0; //no negative counts make sense
	}

	if (item->ammo == NULL)
	{
		//recalc the related entries
		for (propLink = LLGetHead(&item->propertyList); propLink; propLink = propLink->next)
		{
			Property *prop = propLink->data;

			//cache it
			PropertyCacheEntry *propertySum = (PropertyCacheEntry*)HashGetOne(hull->propertySums, prop->name);
			if (propertySum != NULL)
			{
				if (prop->absolute || prop->ignoreCount)
				{
					// there's no good way to update these cases, so remove it and it'll get recalculated
					HashRemove(hull->propertySums, prop->name, propertySum);
				}
				else
				{
					int propDifference = prop->value * amount;
					propertySum->value += propDifference;
				}
			}
			else
			{
				//not in cache already

				//it's too much work to generate the entire entry only to update it.
				//instead we'll just leave it out of the cache, and it can be fully generated when needed
			}
		}
	}
	else
	{
		// Empty the cache. it'll get rebuilt later
		HashEnum(hull->propertySums, deleteCacheEntry, 0);
	}

	database->updateItemOnHull(p, hull, item, count, data);

	//check other items that use this item as ammo
	for (ammoLink = LLGetHead(&item->ammoUsers); ammoLink; ammoLink = ammoLink->next)
	{
		Item *user = ammoLink->data;
		int userCount;

		if (!user->needsAmmo)
			continue;

		userCount = getItemCountOnHull(p, user, hull);
		if (userCount != 0)
		{
			recalculateEntireCacheForHull(p, hull);

			if (oldCount < user->minAmmo && count >= user->minAmmo)
			{
				DO_CBS(CB_AMMO_ADDED, p->arena, ammoAddedFunction, (p, hull, user));
			}
			else if (oldCount >= user->minAmmo && count < user->minAmmo)
			{
				DO_CBS(CB_AMMO_REMOVED, p->arena, ammoRemovedFunction, (p, hull, user));
			}
		}
	}

  DO_CBS(CB_ITEMS_CHANGED, p->arena, ItemsChangedFunction, (p, hull));

	if (doInit)
	{
		triggerEventOnHullItem(p, item, hull, "init");
	}

	database->unlock();
	return count ? 0 : 1;
}

local int addItemCheckLimits(Player *p, Item *item, int ship, int amount)
{
	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to a player with unloaded ships");
		return 0;
	}

	if (ship < 0 || 7 < ship) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to ship %i", ship);
		return 0;
	}

  return addItemToHullCheckLimits(p, item, database->getPlayerShipHull(p, ship), amount);
}

local int addItemToShipSetCheckLimits(Player *p, Item *item, int ship, int shipset, int amount)
{
	if (ship < 0 || 7 < ship || shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to ship %i (shipset %i)", ship, shipset);
		return 0;
	}

  return addItemToHullCheckLimits(p, item, database->getPlayerHull(p, ship, shipset), amount);
}

local int addItemToHullCheckLimits(Player *p, Item *item, ShipHull *hull, int amount)
{
	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to add item to a player with unloaded ships");
		return 0;
	}

	if (!hull) {
		lm->LogP(L_ERROR, "hscore_items", p, "addItemToHullCheckLimits: Requested item count on null hull.");
		return 0;
	}

	if (!item) {
		lm->LogP(L_ERROR, "hscore_items", p, "addItemToHullCheckLimits: Requested item count for null item.");
		return 0;
	}

	if (amount == 0) {
		lm->LogP(L_DRIVEL, "hscore_items", p, "addItemToHullCheckLimits: asked to add 0 of item %s", item->name);
		//return 0;
	}

	int currentAmount;
	int result = 0;
	database->lock();

	if (item->max != 0 && (currentAmount = getItemCountOnHull(p, item, hull)) + amount > item->max)
	{
		int excess = item->max - (currentAmount + amount);
		lm->LogP(L_DRIVEL, "hscore_items", p, "asked to add %i of item %s, but that exceeds the max by %i. truncating.", amount, item->name, excess);
		amount = item->max - currentAmount;
	}

	//if amount < 0 then uh.. whatever. for now
	if (amount > 0)
	{
		Link *link;

		for (link = LLGetHead(&item->itemTypeEntries); link; link = link->next)
		{
			int freeSpots;
			ItemTypeEntry *entry = link->data;

			if ((freeSpots = getFreeItemTypeSpotsOnHull(p, entry->itemType, hull)) - (entry->delta * amount) < 0) //have no free spots
			{
				int excess = (entry->delta * amount) - freeSpots;
				lm->LogP(L_DRIVEL, "hscore_items", p, "asked to add %i of item %s, but that exceeds available %s by %i. truncating.", amount, item->name, entry->itemType->name, excess);
				amount -= excess;
				if (amount <= 0)
					break;
			}
		}


		if (amount < 0)
			amount = 0;
	}

	result = amount ? addItemToHull(p, item, hull, amount) : 0;

	database->unlock();
	return result;
}

local Item * getItemByName(const char *name, Arena *arena)
{
	LinkedList *categoryList = database->getCategoryList(arena);
	Link *catLink;

	//to deal with the fact that an item name is only unique per arena,
	//we scan the items in the categories rather than the item list
	database->lock();
	for (catLink = LLGetHead(categoryList); catLink; catLink = catLink->next)
	{
		Category *category = catLink->data;
		Link *itemLink;

		for (itemLink = LLGetHead(&category->itemList); itemLink; itemLink = itemLink->next)
		{
			Item *item = itemLink->data;

			if (strcasecmp(item->name, name) == 0)
			{
				database->unlock();
				return item;
			}
		}
	}
	database->unlock();

	return NULL;
}

local Item * getItemByPartialName(const char *name, Arena *arena)
{
	int matches = 0;
	Item *item;
	LinkedList *categoryList = database->getCategoryList(arena);
	Link *catLink;

	//to deal with the fact that an item name is only unique per arena,
	//we scan the items in the categories rather than the item list
	database->lock();
	for (catLink = LLGetHead(categoryList); catLink; catLink = catLink->next)
	{
		Category *category = catLink->data;
		Link *itemLink;

		for (itemLink = LLGetHead(&category->itemList); itemLink; itemLink = itemLink->next)
		{
			Item *i = itemLink->data;

			if (strncasecmp(i->name, name, strlen(name)) == 0)
			{
				item = i;
				matches++;
			}
		}
	}
	database->unlock();

	if (matches == 1)
	{
		return item;
	}
	else
	{
		return NULL;
	}
}

local int getPropertySum(Player *p, int ship, const char *propString, int def)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get prop %s on ship %i", propString, ship);
		return 0;
	}

  return getPropertySumOnHull(p, database->getPlayerShipHull(p, ship), propString, def);
}

local int getPropertySumOnShipSet(Player *p, int ship, int shipset, const char *propString, int def)
{
	if (ship < 0 || 7 < ship || shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get prop %s on ship %i (shipset %i).", propString, ship, shipset);
		return 0;
	}

  return getPropertySumOnHull(p, database->getPlayerHull(p, ship, shipset), propString, def);
}

local int getPropertySumOnHull(Player *p, ShipHull *hull, const char *propString, int def)
{
	PropertyCacheEntry *propertySum;
	Link *link;
	LinkedList *inventoryList;
	int count = 0;
	int absolute = 0;

	if (!database->areShipsLoaded(p))
	{
		//lm->LogP(L_ERROR, "hscore_items", p, "asked to get props from a player with unloaded ships");
		return 0;
	}

	if (!hull) {
		//not unusual or dangerous
		//lm->LogP(L_ERROR, "hscore_items", p, "asked to get props from unowned ship %i", ship);
		return 0;
	}

	if (propString == NULL)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get props for NULL string.");
		return 0;
	}

	database->lock();

	//check if it's in the cache
	propertySum = (PropertyCacheEntry*)HashGetOne(hull->propertySums, propString);
	if (propertySum != NULL)
	{
		if (propertySum->absolute)
		{
			def = 0;
		}

		database->unlock();
		return propertySum->value + def;
	}

	//not in the cache, look it up
	for (link = LLGetHead(hull->propertyList); link; link = link->next)
	{
		Property *prop = link->data;

		if (strcmp(prop->name, propString) == 0)
		{
			count += prop->value;
			absolute = absolute || prop->absolute;
			break;
		}
	}

	inventoryList = &hull->inventoryEntryList;
	for (link = LLGetHead(inventoryList); link; link = link->next)
	{
		Link *propLink;

		InventoryEntry *entry = link->data;
		Item *item = entry->item;

		if (item->ammo != NULL && item->needsAmmo)
		{
			int itemCount = getItemCountOnHull(p, item->ammo, hull);

			if (itemCount < item->minAmmo)
			{
				continue; //out of ammo, ignore the item.
			}
		}

		for (propLink = LLGetHead(&item->propertyList); propLink; propLink = propLink->next)
		{
			Property *prop = propLink->data;

			if (strcmp(prop->name, propString) == 0)
			{
				if (prop->ignoreCount)
				{
					count += prop->value;
				}
				else
				{
					count += prop->value * entry->count;
				}

				absolute = absolute || prop->absolute;
				break;
			}
		}
	}

	//cache it
	propertySum = amalloc(sizeof(*propertySum));
	propertySum->value = count;
	propertySum->absolute = absolute;
	HashAdd(hull->propertySums, propString, propertySum);

	if (absolute)
	{
		def = 0;
	}

	//then return
	database->unlock();
	return count + def;
}

local void processUpdateList(Player *p, ShipHull *hull, LinkedList *updateList)
{
	database->lock();

	Link *link;
	for (link = LLGetHead(updateList); link; link = link->next)
	{
		RemovalEntry *re = link->data;

		addItemToHull(p, re->item, hull, -re->count);

		if (re->count > 0)
		{
			//removing
			int i;
			for (i = 0; i < re->count; i++)
			{
				triggerEventOnHullItem(p, re->item, hull, "del");
			}
		}
		else
		{
			//adding
			int i;
			for (i = 0; i < -re->count; i++)
			{
				triggerEventOnHullItem(p, re->item, hull, "add");
			}
		}

		afree(re);
	}

	LLEmpty(updateList);
	database->unlock();
}

local void triggerEvent(Player *p, int ship, const char *eventName)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger event on ship %i", ship);
		return;
	}

  triggerEventOnHull(p, database->getPlayerShipHull(p, ship), eventName);
}

local void triggerEventOnShipSet(Player *p, int ship, int shipset, const char *eventName)
{
	if (ship < 0 || 7 < ship || shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger event on ship %i (shipset %i)", ship, shipset);
		return;
	}

  triggerEventOnHull(p, database->getPlayerHull(p, ship, shipset), eventName);
}

local void triggerEventOnHull(Player *p, ShipHull *hull, const char *eventName)
{
	LinkedList *inventoryList;
	LinkedList updateList;
	Link *link;

	if (p->type == T_FAKE)
		return;

	if (!database->areShipsLoaded(p))
	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger event on a player with unloaded ships");
		return;
	}

	if (eventName == NULL)
	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger event with NULL string.");
		return;
	}

	if (!hull)
	{
		//not unusual or dangerous
		return;
	}

	database->lock();
	LLInit(&updateList);
	inventoryList = &hull->inventoryEntryList;

	link = LLGetHead(inventoryList);
	while (link != NULL)
	{
		Link *eventLink;
		InventoryEntry *entry = link->data;
		Item *item = entry->item;
		link = link->next;

		if (item->ammo && item->needsAmmo && getItemCountOnHull(p, item->ammo, hull) < item->minAmmo)
		{
			continue;
		}

		for (eventLink = LLGetHead(&item->eventList); eventLink; eventLink = eventLink->next)
		{
			Event *event = eventLink->data;

			if (strcmp(event->event, eventName) == 0)
			{
				doEvent(p, entry, event, &updateList);
			}
		}
	}

	processUpdateList(p, hull, &updateList);
	database->unlock();
}

local void triggerEventOnItem(Player *p, Item *triggerItem, int ship, const char *eventName)
{
	if (ship < 0 || 7 < ship) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger item event on ship %i", ship);
		return;
	}

  triggerEventOnHullItem(p, triggerItem, database->getPlayerShipHull(p, ship), eventName);
}

local void triggerEventOnShipSetItem(Player *p, Item *triggerItem, int ship, int shipset, const char *eventName)
{
	if (ship < 0 || 7 < ship || shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger item event on ship %i (shipset %i)", ship, shipset);
		return;
	}

  triggerEventOnHullItem(p, triggerItem, database->getPlayerHull(p, ship, shipset), eventName);
}

local void triggerEventOnHullItem(Player *p, Item *triggerItem, ShipHull *hull, const char *eventName)
{
	int foundItem = 0;
	LinkedList *inventoryList;
	LinkedList updateList;
	Link *link;

	if (eventName == NULL)
	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger item event with NULL string.");
		return;
	}

	if (triggerItem == NULL)
	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger item event with NULL item.");
		return;
	}

	if (!database->areShipsLoaded(p))
	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to trigger item event on a player with unloaded ships");
		return;
	}

	if (!hull)
	{
		//not unusual or dangerous
		return;
	}

	database->lock();

	LLInit(&updateList);
	inventoryList = &hull->inventoryEntryList;

	link = LLGetHead(inventoryList);
	while (link != NULL)
	{
		InventoryEntry *entry = link->data;
		Item *item = entry->item;
		link = link->next; //do event might destroy current node

		if (item == triggerItem)
		{
			Link *eventLink;
			foundItem = 1;

			if (item->ammo && item->needsAmmo && getItemCountOnHull(p, item->ammo, hull) < item->minAmmo)
			{
				break;
			}

			for (eventLink = LLGetHead(&item->eventList); eventLink; eventLink = eventLink->next)
			{
				Event *event = eventLink->data;

				if (strcmp(event->event, eventName) == 0)
				{
					doEvent(p, entry, event, &updateList);
				}
			}

			break; //found the item, stop looping
		}
	}

	//check if we didn't find the item
	if (!foundItem)
	{
		Link *eventLink;

		if (triggerItem->ammo && triggerItem->needsAmmo && getItemCountOnHull(p, triggerItem->ammo, hull) < triggerItem->minAmmo)
		{
			//nothing
		}
		else
		{
			for (eventLink = LLGetHead(&triggerItem->eventList); eventLink; eventLink = eventLink->next)
			{
				Event *event = eventLink->data;

				if (strcmp(event->event, eventName) == 0)
				{
					doEvent(p, NULL, event, &updateList);
				}
			}
		}
	}

	processUpdateList(p, hull, &updateList);

	DO_CBS(CB_TRIGGER_EVENT, p->arena, triggerEventFunction, (p, triggerItem, hull, eventName));
	database->unlock();
}

local int getFreeItemTypeSpots(Player *p, ItemType *type, int ship)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item type on ship %i", ship);
		return 0;
	}

  return getFreeItemTypeSpotsOnHull(p, type, database->getPlayerShipHull(p, ship));
}

local int getFreeItemTypeSpotsOnShipSet(Player *p, ItemType *type, int ship, int shipset)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item type on ship %i", ship);
		return 0;
	}

	if (shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to recalc entire cache on shipset %i", shipset);
		return 0;
	}

	return getFreeItemTypeSpotsOnHull(p, type, database->getPlayerHull(p, ship, shipset));
}

local int getFreeItemTypeSpotsOnHull(Player *p, ItemType *type, ShipHull *hull)
{
	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item type slots for a player with unloaded ships");
		return 0;
	}

	if (!hull) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get item type slots for NULL hull.");
		return 0;
	}

	if (type == NULL)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to get slots for NULL item type.");
		return 0;
	}

	Link *link;
	LinkedList *inventoryList;
	int count;

	database->lock();
	inventoryList = &hull->inventoryEntryList;

	count = type->max;

	for (link = LLGetHead(inventoryList); link; link = link->next)
	{
		InventoryEntry *entry = link->data;
		Item *item = entry->item;

		Link *itemTypeLink;
		for (itemTypeLink = LLGetHead(&item->itemTypeEntries); itemTypeLink; itemTypeLink = itemTypeLink->next)
		{
			ItemTypeEntry *typeEntry = itemTypeLink->data;

			if (typeEntry->itemType == type)
			{
				count -= entry->count * typeEntry->delta;
			}
		}
	}

	database->unlock();
	return count;
}

local int hasItemsLeftOnShip(Player *p, int ship)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to check items left on ship %i", ship);
		return 0;
	}

  return hasItemsLeftOnHull(p, database->getPlayerShipHull(p, ship));
}

local int hasItemsLeftOnShipSetShip(Player *p, int ship, int shipset)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to check items left on ship %i", ship);
		return 0;
	}

	if (shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to check items left on shipset %i", shipset);
		return 0;
	}

	return hasItemsLeftOnHull(p, database->getPlayerHull(p, ship, shipset));
}

local int hasItemsLeftOnHull(Player *p, ShipHull *hull)
{
	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to check items left from a player with unloaded ships");
		return 0;
	}

	if (!hull) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to check items left on NULL hull.");
		return 0;
	}

	LinkedList *inventoryList = &hull->inventoryEntryList;

	return (LLGetHead(inventoryList) == NULL) ? 0 : 1;
}

local int zeroCacheEntry(const char *key, void *val, void *clos)
{
	PropertyCacheEntry *cacheEntry = val;
	cacheEntry->value = 0;
	cacheEntry->absolute = 0;

	return 0;
}

local void recalculateEntireCache(Player *p, int ship)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to recalc entire cache on ship %i", ship);
		return;
	}

  recalculateEntireCacheForHull(p, database->getPlayerShipHull(p, ship));
}

local void recalculateEntireCacheForShipSet(Player *p, int ship, int shipset)
{
	if (ship < 0 || 7 < ship)	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to recalc entire cache on ship %i", ship);
		return;
	}

	if (shipset < 0 || shipset >= HSCORE_MAX_SHIPSETS) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to recalc entire cache on shipset %i", shipset);
		return;
	}

	recalculateEntireCacheForHull(p, database->getPlayerHull(p, ship, shipset));
}

local void recalculateEntireCacheForHull(Player *p, ShipHull *hull)
{
	if (!database->areShipsLoaded(p))	{
		lm->LogP(L_ERROR, "hscore_items", p, "asked to recalc entire cache for a player with unloaded ships");
		return;
	}

	if (!hull) {
		lm->LogP(L_ERROR, "hscore_items", p, "asked to recalc entire cache on NULL hull.");
		return;
	}

	HashTable *table;
	Link *link;
	LinkedList *inventoryList;

	database->lock();
	table = hull->propertySums;

	//first, set all cache entries to zero, so we can keep the 0 entries
	HashEnum(table, zeroCacheEntry, NULL);

	//then iterate every property and update the cache with it
	LinkedList *shipProperties = database->getShipPropertyList(p->arena, hull->ship);
	for (link = LLGetHead(shipProperties); link; link = link->next)
	{
		Property *prop = link->data;
		PropertyCacheEntry *propertySum;

		int propDifference = prop->value;

		//get it out of the cache
		propertySum = (PropertyCacheEntry*)HashGetOne(table, prop->name);
		if (propertySum != NULL)
		{
			propertySum->value += propDifference;
			propertySum->absolute = propertySum->absolute || prop->absolute;
		}
		else
		{
			//no cache entry; create one
			propertySum = amalloc(sizeof(*propertySum));
			propertySum->value = propDifference;
			propertySum->absolute = prop->absolute;
			HashAdd(hull->propertySums, prop->name, propertySum);
		}
	}

	inventoryList = &hull->inventoryEntryList;
	for (link = LLGetHead(inventoryList); link; link = link->next)
	{
		InventoryEntry *entry = link->data;
		Item *item = entry->item;
		Link *propLink;

		if (item->ammo != NULL && item->needsAmmo)
		{
			int itemCount = getItemCountOnHull(p, item->ammo, hull);

			if (itemCount < item->minAmmo)
			{
				continue; //out of ammo, ignore the item.
			}
		}

		for (propLink = LLGetHead(&item->propertyList); propLink; propLink = propLink->next)
		{
			Property *prop = propLink->data;
			PropertyCacheEntry *propertySum;

			int propDifference;

			if (prop->ignoreCount)
			{
				propDifference = prop->value;
			}
			else
			{
				propDifference = prop->value * entry->count;
			}

			//get it out of the cache
			propertySum = (PropertyCacheEntry*)HashGetOne(table, prop->name);
			if (propertySum != NULL)
			{
				propertySum->value += propDifference;
				propertySum->absolute = propertySum->absolute || prop->absolute;
			}
			else
			{
				//no cache entry; create one
				propertySum = amalloc(sizeof(*propertySum));
				propertySum->value = propDifference;
				propertySum->absolute = prop->absolute;
				HashAdd(hull->propertySums, prop->name, propertySum);
			}
		}
	}

	database->unlock();
}

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	if (killer != killed) {
		triggerEvent(killer, killer->p_ship, (killer->p_freq != killed->p_freq) ? "kill" : "teamkill");
	}

	triggerEvent(killed, killed->p_ship, "death");
}

local Ihscoreitems interface =
{
	INTERFACE_HEAD_INIT(I_HSCORE_ITEMS, "hscore_items")

	getItemCount, getItemCountOnShipSet, getItemCountOnHull,

	addItem, addItemToShipSet, addItemToHull,
	addItemCheckLimits, addItemToShipSetCheckLimits, addItemToHullCheckLimits,

	getItemByName, getItemByPartialName,

	getPropertySum, getPropertySumOnShipSet, getPropertySumOnHull,

	triggerEvent, triggerEventOnShipSet, triggerEventOnHull,
	triggerEventOnItem, triggerEventOnShipSetItem, triggerEventOnHullItem,

	getFreeItemTypeSpots, getFreeItemTypeSpotsOnShipSet, getFreeItemTypeSpotsOnHull,
	hasItemsLeftOnShip, hasItemsLeftOnShipSetShip, hasItemsLeftOnHull,

	recalculateEntireCache, recalculateEntireCacheForShipSet, recalculateEntireCacheForHull
};

EXPORT const char info_hscore_items[] = "v1.01 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_items(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);

		if (!lm || !chat || !cmd || !pd || !game || !database || !ml || !cfg || !selfpos)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(game);
			mm->ReleaseInterface(database);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(selfpos);

			return MM_FAIL;
		}

		mm->RegInterface(&interface, ALLARENAS);

		mm->RegCallback(CB_KILL, killCallback, ALLARENAS);

		cmd->AddCommand("iteminfo", itemInfoCommand, ALLARENAS, itemInfoHelp);
		cmd->AddCommand("grantitem", grantItemCommand, ALLARENAS, grantItemHelp);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&interface, ALLARENAS))
		{
			return MM_FAIL;
		}

		mm->UnregCallback(CB_KILL, killCallback, ALLARENAS);

		cmd->RemoveCommand("iteminfo", itemInfoCommand, ALLARENAS);
		cmd->RemoveCommand("grantitem", grantItemCommand, ALLARENAS);

		ml->ClearTimer(timer_callback, NULL);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(database);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(selfpos);

		return MM_OK;
	}
	return MM_FAIL;
}
