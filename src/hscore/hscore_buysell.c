#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_storeman.h"
#include "hscore_database.h"
#include "hscore_shipnames.h"
#include "hscore_buysell.h"
#include "hscore_spawner2.h"

#define SEE_HIDDEN_CATEGORIES "seehiddencat"


////////////////////////////////////////
// TEMP STUFFS

#define MIN_COMMAND_DELAY 300

local int bsDataKey;

typedef struct BuySellData {

    int lastCommand;

} BuySellData;

// TEMP STUFFS
////////////////////////////////////////


//modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Iconfig *cfg;
local Icmdman *cmd;
local Icapman *capman;
local Iplayerdata *pd;
local Ihscoreitems *items;
local Ihscoredatabase *database;

local void printAllCategories(Player *p)
{
	LinkedList *categoryList = database->getCategoryList(p->arena);
	Link *link;

	chat->SendMessage(p, "+----------------------------------+------------------------------------------------------------------+");
	chat->SendMessage(p, "| Category Name                    | Category Description                                             |");
	chat->SendMessage(p, "+----------------------------------+------------------------------------------------------------------+");
	chat->SendMessage(p, "| Ships                            | All the ship hulls you can buy in this arena.                    |");

	database->lock();
	for (link = LLGetHead(categoryList); link; link = link->next)
	{
		Category *category = link->data;
		if (category->hidden && capman->HasCapability(p, SEE_HIDDEN_CATEGORIES))
		{
			chat->SendMessage(p, "| (%-30s) | %-64s |", category->name, category->description);
		}
		else if (!category->hidden)
		{
			chat->SendMessage(p, "| %-32s | %-64s |", category->name, category->description);
		}
	}
	database->unlock();

	chat->SendMessage(p, "+----------------------------------+------------------------------------------------------------------+");
}

local void compressPrice(char * buffer, int price)
{
	double digits;
	char power;
	if (price >= 1000000000)
	{
		// billion
		power = 'B';
		digits = ((double)price) / 1000000000L;
	}
	if (price >= 1000000)
	{
		// million
		power = 'M';
		digits = ((double)price) / 1000000L;
	}
	else if (price >= 1000)
	{
		// thousand
		power = 'k';
		digits = ((double)price) / 1000L;
	}
	else
	{
		sprintf(buffer, "%i", price);
		return;
	}

	sprintf(buffer, "%lg%c", digits, power);
}

local void printCategoryItems(Player *p, Category *category) //call with lock held
{
	Link *link;
	Link plink = {NULL, p};
	LinkedList lst = { &plink, &plink };
	char messageType;
	char buyString[16];
	char sellString[16];

	chat->SendMessage(p, "+----------------------------------+");
	chat->SendMessage(p, "| %-32s |", category->name);
	chat->SendMessage(p, "+------------------+--------+------+-+-------+----------+-----+------+----------------------------------+");
	chat->SendMessage(p, "| Item Name        | Buy    | Sell   | Exp   | Ships    | Max | Ammo | Item Description                 |");
	chat->SendMessage(p, "+------------------+--------+--------+-------+----------+-----+------+----------------------------------+");

	if (!category->hidden || capman->HasCapability(p, SEE_HIDDEN_CATEGORIES))
	{
		for (link = LLGetHead(&category->itemList); link; link = link->next)
		{
			int i;
			Item *item = link->data;

			char shipMask[] = "12345678";

			for (i = 0; i < 8; i++)
			{
				if (!((item->shipsAllowed >> i) & 0x1))
				{
					shipMask[i] = ' ';
				}
			}

			compressPrice(buyString, item->buyPrice);
			compressPrice(sellString, item->sellPrice);

			if (database->getMoney(p) < item->buyPrice || database->getExp(p) < item->expRequired)
			{
				messageType =  MSG_SYSOPWARNING;
			}
			else
			{
				if (p->p_ship == SHIP_SPEC || (item->shipsAllowed >> p->p_ship) & 0x1)
				{
					messageType = MSG_ARENA;
				}
				else
				{
					messageType = MSG_SYSOPWARNING;
				}
			}

			chat->SendAnyMessage(&lst, messageType, 0, NULL, "| %-16s | %-6s | %-6s | %-5i | %-8s | %-3i | %-4i | %-32s |", item->name, buyString, sellString, item->expRequired, shipMask, item->max, item->minAmmo, item->shortDesc);
		}
	}

	chat->SendMessage(p, "+------------------+--------+--------+-------+----------+-----+------+----------------------------------+");
}

local void printShipList(Player *p)
{
	int i;
	Link plink = {NULL, p};
	LinkedList lst = { &plink, &plink };
	char messageType;

	chat->SendMessage(p, "+-----------+-----------+------------+--------+----------------------------------------------------+");
	chat->SendMessage(p, "| Ship Name | Buy Price | Sell Price | Exp    | Ship Description                                   |");
	chat->SendMessage(p, "+-----------+-----------+------------+--------+----------------------------------------------------+");

	for (i = 0; i < 8; i++)
	{
		/* cfghelp: All:BuyPrice, arena, int, def: 0, mod: hscore_buysell
		 * Cost for buying the ship hull. Zero means no purchace needed. */
		int buyPrice = cfg->GetInt(p->arena->cfg, shipNames[i], "BuyPrice", 0);
		/* cfghelp: All:SellPrice, arena, int, def: 0, mod: hscore_buysell
		 * Money earned from selling the ship hull. */
		int sellPrice = cfg->GetInt(p->arena->cfg, shipNames[i], "SellPrice", 0);
		/* cfghelp: All:ExpRequired, arena, int, def: 0, mod: hscore_buysell
		 * Experience required to buy the ship hull. */
		int expRequired = cfg->GetInt(p->arena->cfg, shipNames[i], "ExpRequired", 0);

		/* cfghelp: All:Description, arena, string, mod: hscore_buysell
		 * Text to put in the ship description column. */
		const char *description = cfg->GetStr(p->arena->cfg, shipNames[i], "Description");

		if (description == NULL)
		{
			description = "<No description available>";
		}

		if (buyPrice == 0)
		{
			continue; //dont list the ship unless it can be bought.
		}

		if(database->getMoney(p) >= buyPrice && database->getExp(p) >= expRequired)
		{
			messageType = MSG_ARENA;
		}
		else
		{
			messageType = MSG_SYSOPWARNING;
		}

		chat->SendAnyMessage(&lst, messageType, 0, NULL, "| %-9s | $%-8i | $%-9i | %-6i | %-50s |", shipNames[i], buyPrice, sellPrice, expRequired, description);
	}


	chat->SendMessage(p, "+-----------+-----------+------------+--------+----------------------------------------------------+");
}

local void buyItem(Player *p, Item *item, int count, int ship)
{
	int origCount = count;

	if ((item->shipsAllowed >> ship) & 0x1)
	{
		if (item->buyPrice)
		{
			int maxCount = database->getMoney(p) / item->buyPrice;
			if (maxCount < count)
			{
				count = maxCount;
			}

			if (0 < count)
			{
				if (database->getExp(p) >= item->expRequired)
				{
					int maxCount = item->max - items->getItemCount(p, item, ship);
					if (item->max != 0 && maxCount < count)
					{
						count = maxCount;
					}

					if (0 < count)
					{
						Ihscorestoreman *storeman = mm->GetInterface(I_HSCORE_STOREMAN, p->arena);
						int storemanOk;
						LinkedList list;
						LLInit(&list);

						if (!storeman)
						{
							storemanOk = 1;
						}
						else
						{
							storemanOk = storeman->canSellItem(p, item);
							if (!storemanOk)
							{
								storeman->getStoreList(p, item, &list); //fills the linked list with stores that buy the item
							}
						}
						mm->ReleaseInterface(storeman);

						if (storemanOk)
						{
							int i;
							Link *link;
							database->lock();
							for (link = LLGetHead(&item->itemTypeEntries); link; link = link->next)
							{
								ItemTypeEntry *entry = link->data;

								int freeSpots = items->getFreeItemTypeSpots(p, entry->itemType, ship);
								int maxCount = freeSpots / entry->delta;
								if (0 < entry->delta && maxCount < count)
								{
									count = maxCount;
								}

								if (count <= 0) //have no free spots
								{
									chat->SendMessage(p, "You do not have enough free %s spots.", entry->itemType->name);
									database->unlock();
									return;
								}
							}
							database->unlock();


							LinkedList advisers = LL_INITIALIZER;
							Ahscorebuysell *adviser;
							int canBuy = 1;

							mm->GetAdviserList(A_HSCORE_BUYSELL, p->arena, &advisers);
							FOR_EACH(&advisers, adviser, link)
							{
								if (adviser->CanBuy)
								{
									if (!adviser->CanBuy(p, item, count, ship))
									{
										canBuy = 0;
									}
								}
							}
							mm->ReleaseAdviserList(&advisers);

							if (!canBuy)
							{
								return;
							}


							items->addItem(p, item, ship, count);

							database->addMoney(p, MONEY_TYPE_BUYSELL, -item->buyPrice * count);

							items->triggerEventOnItem(p, item, ship, "buy");

							for (i = 0; i < count; i++)
							{
								items->triggerEventOnItem(p, item, ship, "add");
							}

							chat->SendMessage(p, "You purchased %i of item %s for $%i.", count, item->name, item->buyPrice * count);
						}
						else
						{
							int storeCount = LLCount(&list);
							if (storeCount == 0)
							{
								chat->SendMessage(p, "You cannot buy item %s at your current location. No known stores sell it!", item->name);
							}
							else if (storeCount == 1)
							{
								Store *store = LLGetHead(&list)->data;
								chat->SendMessage(p, "You cannot buy item %s here. Go to %s to buy it!", item->name, store->name);
							}
							else
							{
								Link *link;
								chat->SendMessage(p, "You cannot buy item %s here. The following stores sell it:", item->name);
								for (link = LLGetHead(&list); link; link = link->next)
								{
									Store *store = link->data;

									chat->SendMessage(p, "%s", store->name);
								}
							}

							LLEmpty(&list);
						}
					}
					else
					{
						chat->SendMessage(p, "You may only have %i of item %s on your ship.", item->max, item->name);
					}
				}
				else
				{
					chat->SendMessage(p, "You need %i more experience to buy item %s.", item->expRequired - database->getExp(p), item->name);
				}
			}
			else
			{
				chat->SendMessage(p, "You do not have enough money to buy item %s. You need $%i more.", item->name, item->buyPrice * origCount - database->getMoney(p));
			}
		}
		else
		{
			chat->SendMessage(p, "Item %s is not for sale.", item->name);
		}
	}
	else
	{
		chat->SendMessage(p, "Item %s is not allowed on a %s.", item->name, shipNames[ship]);
	}
}

local void sellItem(Player *p, Item *item, int count, int ship)
{
	if (item->sellPrice)
	{
		if (database->getMoney(p) >= -item->sellPrice * count)
		{
			if (items->getItemCount(p, item, ship) >= count)
			{
				Ihscorestoreman *storeman = mm->GetInterface(I_HSCORE_STOREMAN, p->arena);
				int storemanOk;
				LinkedList list;
				LLInit(&list);

				if (!storeman)
				{
					storemanOk = 1;
				}
				else
				{
					storemanOk = storeman->canSellItem(p, item);
					if (!storemanOk)
					{
						storeman->getStoreList(p, item, &list); //fills the linked list with stores that buy the item
					}
				}
				mm->ReleaseInterface(storeman);

				if (storemanOk)
				{
					int i;
					Link *link;
					database->lock();
					for (link = LLGetHead(&item->itemTypeEntries); link; link = link->next)
					{
						ItemTypeEntry *entry = link->data;

						if (items->getFreeItemTypeSpots(p, entry->itemType, ship) + (entry->delta * count) < 0) //have no free spots
						{
							chat->SendMessage(p, "You do not have enough free %s spots.", entry->itemType->name);
							database->unlock();
							return;
						}
					}
					database->unlock();


					LinkedList advisers = LL_INITIALIZER;
					Ahscorebuysell *adviser;
					int canSell = 1;

					mm->GetAdviserList(A_HSCORE_BUYSELL, p->arena, &advisers);
					FOR_EACH(&advisers, adviser, link)
					{
						if (adviser->CanSell)
						{
							if (!adviser->CanSell(p, item, count, ship))
							{
								canSell = 0;
							}
						}
					}
					mm->ReleaseAdviserList(&advisers);

					if (!canSell)
					{
						return;
					}


					//trigger before it's sold!
					items->triggerEventOnItem(p, item, ship, "sell");

					items->addItem(p, item, ship, -count); //change the count BEFORE the "del" event

					for (i = 0; i < count; i++)
					{
						items->triggerEventOnItem(p, item, ship, "del");
					}

					database->addMoney(p, MONEY_TYPE_BUYSELL, item->sellPrice * count);

					chat->SendMessage(p, "You sold %i of item %s for $%i.", count, item->name, item->sellPrice * count);
				}
				else
				{
					int storeCount = LLCount(&list);
					if (storeCount == 0)
					{
						chat->SendMessage(p, "You cannot sell item %s at your current location. No known stores buy it!", item->name);
					}
					else if (storeCount == 1)
					{
						Store *store = LLGetHead(&list)->data;
						chat->SendMessage(p, "You cannot sell item %s here. Go to %s to sell it!", item->name, store->name);
					}
					else
					{
						Link *link;
						chat->SendMessage(p, "You cannot sell item %s here. The following stores buy it:", item->name);
						for (link = LLGetHead(&list); link; link = link->next)
						{
							Store *store = link->data;

							chat->SendMessage(p, "%s", store->name);
						}
					}

					LLEmpty(&list);
				}
			}
			else
			{
				if (count == 1)
				{
					chat->SendMessage(p, "You do not have any of item %s to sell", item->name);
				}
				else
				{
					chat->SendMessage(p, "You do not have that many of item %s to sell", item->name);
				}
			}
		}
		else
		{
			chat->SendMessage(p, "You do not have enough money to sell item %s.", item->name);
		}
	}
	else
	{
		chat->SendMessage(p, "Item %s cannot be sold.", item->name);
	}
}

local void buyShip(Player *p, int ship)
{
	// Prevent people from buying/selling ships in combat to force reshipping
    if (p->p_ship != SHIP_SPEC && !(p->position.status & STATUS_SAFEZONE)) {
    	chat->SendMessage(p, "Ships may only be purchased from a safe zone or spectator mode.");
    	return;
    }

    // Ignore extremely fast requests...
    BuySellData *objData = PPDATA(p, bsDataKey);
    if(current_ticks() - objData->lastCommand < MIN_COMMAND_DELAY)
    {
        chat->SendMessage(p, "Please wait a few moments between ship buy and sell requests");
        return; // Silently discard quick commands.
    } else {
        objData->lastCommand = current_ticks();
    }

	int buyPrice = cfg->GetInt(p->arena->cfg, shipNames[ship], "BuyPrice", 0);
	int expRequired = cfg->GetInt(p->arena->cfg, shipNames[ship], "ExpRequired", 0);

	if (buyPrice != 0)
	{
		if (database->areShipsLoaded(p))
		{
			IHSCoreSpawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER2, p->arena);
			int shipset;

			if (spawner) {
				shipset = spawner->hasPendingShipSetChange(p) ? spawner->getPendingShipSet(p) : database->getPlayerShipSet(p);
				mm->ReleaseInterface(spawner);
			} else {
				shipset = database->getPlayerShipSet(p);
			}

			if (!database->getPlayerHull(p, ship, shipset))
			{
				if (database->getMoney(p) >= buyPrice)
				{
					if (database->getExp(p) >= expRequired)
					{
						database->addShipToShipSet(p, ship, shipset);

						//database will call the ship_added callback for init items

						database->addMoney(p, MONEY_TYPE_BUYSELL, -buyPrice);

						chat->SendMessage(p, "You purchased a %s for $%i (shipset %i).", shipNames[ship],  buyPrice, shipset + 1);
					}
					else
					{
						chat->SendMessage(p, "You need %i more experience to buy a %s.", expRequired - database->getExp(p), shipNames[ship]);
					}
				}
				else
				{
					chat->SendMessage(p, "You do not have enough money to buy a %s. You need $%i more.", shipNames[ship], buyPrice - database->getMoney(p));
				}
			}
			else
			{
				chat->SendMessage(p, "You already own a %s on shipset %i.", shipNames[ship], shipset + 1);
			}
		}
		else
		{
			chat->SendMessage(p, "Your ships are not loaded.");
		}
	}
	else
	{
		chat->SendMessage(p, "%ss are not avalible for sale in this arena.", shipNames[ship]);
	}
}

local void sellShip(Player *p, int ship)
{
	// Prevent people from buying/selling ships in combat to force reshipping
    if (p->p_ship != SHIP_SPEC && !(p->position.status & STATUS_SAFEZONE)) {
    	chat->SendMessage(p, "Ships may only be sold from a safe zone or spectator mode.");
    	return;
    }
    // Ignore extremely fast requests...
    BuySellData *objData = PPDATA(p, bsDataKey);
    if(current_ticks() - objData->lastCommand < MIN_COMMAND_DELAY)
    {
        chat->SendMessage(p, "Please wait a few moments between ship buy and sell requests");
        return; // Silently discard quick commands.
    } else {
        objData->lastCommand = current_ticks();
    }

	int sellPrice = cfg->GetInt(p->arena->cfg, shipNames[ship], "SellPrice", 0);

	if (database->areShipsLoaded(p))
	{
		ShipHull *hull = database->getPlayerShipHull(p, ship);

		if (hull != NULL)
		{
			int itemPrices = 0;
			LinkedList *inventoryList = &hull->inventoryEntryList;
			Link *link;

			for (link = LLGetHead(inventoryList); link; link = link->next)
			{
				InventoryEntry *entry = link->data;
				itemPrices += entry->item->sellPrice * entry->count;
			}

			if (database->getMoney(p) >= -(sellPrice + itemPrices))
			{
				database->removeShip(p, ship);

				database->addMoney(p, MONEY_TYPE_BUYSELL, sellPrice + itemPrices);

				chat->SendMessage(p, "You sold your %s for $%i.", shipNames[ship], sellPrice + itemPrices);
			}
			else
			{
				chat->SendMessage(p, "You do not have enough money to sell your %s. You need $%i", shipNames[ship], sellPrice + itemPrices);
			}
		}
		else
		{
			chat->SendMessage(p, "You do not own a %s.", shipNames[ship]);
		}
	}
	else
	{
		chat->SendMessage(p, "Your ships are not loaded.");
	}
}

local helptext_t buyHelp =
"Targets: none\n"
"Args: none or <item> or <category> or <ship>\n"
"If there is no arugment, this command will display a list of this arena's buy categories.\n"
"If the argument is a category, this command will display all the items in that category.\n"
"If the argument is an item, this command will attemt to buy it for the buy price.\n";

local void buyCommand(const char *command, const char *params, Player *p, const Target *target)
{
	LinkedList *categoryList = database->getCategoryList(p->arena);
	Link *link;

	int count = 1;
	const char *newParams;

	char *next; //for strtol

	while (params != NULL) //get the flags
	{
		if (*params == '-')
		{
			params++;
			if (*params == '\0')
			{
				newParams = params;
				break;
			}
			if (*params == 'c')
			{
				params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Buy: invalid usage.");
					return;
				}

				count = strtol(params, &next, 0);

				if (next == params)
				{
					chat->SendMessage(p, "Buy: bad count.");
					return;
				}

				params = next;
			}
		}
		else if (*params == ' ')
		{
			params++;
		}
		else if (*params == '\0')
		{
			newParams = params;
			break;
		}
		else
		{
			newParams = params;
			break;
		}
	}

	//finished parsing
	if (count < 1)
	{
		chat->SendMessage(p, "Nice try.");
		return;
	}

	if (strcasecmp(newParams, "") == 0) //no params
	{
		printAllCategories(p);
	}
	else //has params
	{
		if (strcasecmp(newParams, "ships") == 0) //print ship list
		{
			printShipList(p);
		}
		else
		{
			int i;
			int matches;
			Category *category;

			//check if they're asking for a ship
			for (i = 0; i < 8; i++)
			{
				if (strcasecmp(newParams, shipNames[i]) == 0)
				{
					buyShip(p, i);
					return;
				}
			}

			//check if they're asking for a category
			database->lock();
			matches = 0;
			for (link = LLGetHead(categoryList); link; link = link->next)
			{
				Category *c = link->data;

				if (strncasecmp(c->name, newParams, strlen(newParams)) == 0)
				{
					category = c;
					matches++;
				}
			}

			if (matches == 1)
			{
				printCategoryItems(p, category);

				database->unlock();
				return;
			}
			else if (matches > 1)
			{
				chat->SendMessage(p, "Too many partial matches! Try typing more of the name!");

				database->unlock();
				return;
			}
			database->unlock();

			//not a category. check for an item
			Item *item = items->getItemByPartialName(newParams, p->arena);
			if (item != NULL)
			{
				if (p->p_ship != SHIP_SPEC)
				{
					if (database->getPlayerCurrentHull(p) != NULL)
					{
						//check - counts
						buyItem(p, item, count, p->p_ship);
					}
					else
					{
						chat->SendMessage(p, "No items can be loaded onto a %s in this arena.", shipNames[p->p_ship]);
					}
				}
				else
				{
					chat->SendMessage(p, "You cannot buy or sell items from spectator mode.");
				}

				return;
			}

			//neither an item nor a ship nor a category
			chat->SendMessage(p, "No item %s in this arena.", params);
		}
	}
}

local helptext_t sellHelp =
"Targets: none\n"
"Args: <item> or <ship>\n"
"Removes the item from your ship and refunds you the item's sell price.\n";

local void sellCommand(const char *command, const char *params, Player *p, const Target *target)
{
	int count = 1;
	const char *newParams;

	char *next; //for strtol

	while (params != NULL) //get the flags
	{
		if (*params == '-')
		{
			params++;
			if (*params == '\0')
			{
				newParams = params;
				break;
			}

			if (*params == 'f')
			{
				//force = 1;

				chat->SendMessage(p, "Force no longer accepted as a parameter!");
				return;

				/*params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Sell: invalid usage.");
					return;
				}*/
			}
			if (*params == 'c')
			{
				params = strchr(params, ' ');
				if (params) //check so that params can still == NULL
				{
					params++; //we want *after* the space
				}
				else
				{
					chat->SendMessage(p, "Sell: invalid usage.");
					return;
				}

				count = strtol(params, &next, 0);

				if (next == params)
				{
					chat->SendMessage(p, "Sell: bad count.");
					return;
				}

				params = next;
			}
		}
		else if (*params == ' ')
		{
			params++;
		}
		else if (*params == '\0')
		{
			newParams = params;
			break;
		}
		else
		{
			newParams = params;
			break;
		}
	}

	//finished parsing
	if (count < 1)
	{
		chat->SendMessage(p, "Nice try.");
		return;
	}

	if (strcasecmp(newParams, "") == 0) //no params
	{
		chat->SendMessage(p, "Please use ?buy to find the item you wish to sell");
	}
	else //has params
	{
		int i;
		//check if they're asking for a ship
		for (i = 0; i < 8; i++)
		{
			if (strcasecmp(newParams, shipNames[i]) == 0)
			{
				if (i != p->p_ship)
				{
					sellShip(p, i);
				}
				else
				{
					chat->SendMessage(p, "You cannot sell the ship you are using. Switch to spec first.");
				}

				return;
			}
		}

		//check for an item
		Item *item = items->getItemByName(newParams, p->arena);
		if (item != NULL)
		{
			if (p->p_ship != SHIP_SPEC)
			{
				if (database->getPlayerCurrentHull(p) != NULL)
				{
					//check - counts
					sellItem(p, item, count, p->p_ship);
				}
				else
				{
					chat->SendMessage(p, "No items can be loaded onto a %s in this arena.", shipNames[p->p_ship]);
				}
			}
			else
			{
				chat->SendMessage(p, "You cannot buy or sell items in spec.");
			}

			return;
		}

		//not a ship nor an item
		chat->SendMessage(p, "No item %s in this arena.", params);
	}
}

local void shipAddedCallback(Player *p, int ship, int shipset)
{
	/* cfghelp: All:InitItems, arena, string, mod: hscore_buysell
	 * Comma seperated list of items to add when the ship is bought.
	 * There should be no spaces between the commas ('item1,item2,...').*/
	const char *initItem = cfg->GetStr(p->arena->cfg, shipNames[ship], "InitItems");

	if (initItem != NULL) //only bother if there are items to add
	{
		const char *tmp = NULL;
		char word[64];
		while (strsplit(initItem, ",", word, sizeof(word), &tmp))
		{
			//items should be in the format "item name" or "item name:count"
			char *colonLoc = strchr(word, ':');
			if (colonLoc == NULL)
			{
				//no count included
				//word should be just "item name"
				Item *item = items->getItemByName(word, p->arena);
				if (item != NULL) {
					items->addItemToShipSet(p, item, ship, shipset, 1);
				} else {
					lm->LogP(L_ERROR, "hscore_buysell", p, "bad item %s", word);
				}
			}
			else
			{
				//null terminate the item name
				*colonLoc = '\0';

				//make sure a count follows the colon
				colonLoc++;
				if (*colonLoc != '\0')
				{
					int count = atoi(colonLoc);
					if (count > 0)
					{
						Item *item = items->getItemByName(word, p->arena);
						if (item != NULL) {
							items->addItemToShipSet(p, item, ship, shipset, count);
						} else {
							lm->LogP(L_ERROR, "hscore_buysell", p, "bad item %s", word);
						}
					}
					else
					{
						lm->LogP(L_ERROR, "hscore_buysell", p, "initial count of %d for %s", count, word);
					}
				}
				else
				{
					lm->LogP(L_ERROR, "hscore_buysell", p, "colon on item %s not followed by a string", word);
				}
			}
		}
	}
}

EXPORT const char info_hscore_buysell[] = "v1.0 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_buysell(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !chat || !cfg || !cmd || !capman || !pd || !items || !database)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(capman);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(database);

			return MM_FAIL;
		}

		////////////////////////////////////////
		// TEMP STUFFS

		bsDataKey = pd->AllocatePlayerData(sizeof(BuySellData));

		// TEMP STUFFS
		////////////////////////////////////////

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        ////////////////////////////////////////
        // TEMP STUFFS

        pd->FreePlayerData(bsDataKey);

        // TEMP STUFFS
        ////////////////////////////////////////

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		cmd->AddCommand("buy", buyCommand, arena, buyHelp);
		cmd->AddCommand("sell", sellCommand, arena, sellHelp);

		mm->RegCallback(CB_SHIP_ADDED, shipAddedCallback, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		cmd->RemoveCommand("buy", buyCommand, arena);
		cmd->RemoveCommand("sell", sellCommand, arena);

		mm->UnregCallback(CB_SHIP_ADDED, shipAddedCallback, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
