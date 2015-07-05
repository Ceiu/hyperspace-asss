#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_shipnames.h"

//modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Iconfig *cfg;
local Icmdman *cmd;
local Ihscoreitems *items;
local Ihscoredatabase *database;

typedef struct KeyCacheEntryPair
{
	const char *key;
	PropertyCacheEntry *cacheEntry;
} KeyCacheEntryPair;

local int sortKeyCacheEntryPair(const void *_a, const void *_b)
{
	const KeyCacheEntryPair *a = (const KeyCacheEntryPair *)_a;
	const KeyCacheEntryPair *b = (const KeyCacheEntryPair *)_b;

	return (strcmp(a->key, b->key) < 0);
}

local helptext_t shipsHelp =
"Targets: none or player\n"
"Args: [-ss <shipset #>]\n"
"Displays what ships you own in this arena.\n";

local void shipsCommand(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;


	int shipset;
	if (strncmp(params, "-ss ", 4) || (shipset = atoi(params + 4) - 1) == -1) {
	  shipset = database->getPlayerShipSet(t);
	}

	if (shipset < 0 || shipset > HSCORE_MAX_SHIPSETS) {
		chat->SendMessage(p, "Shipset out of range. Please choose a ship from 1 to %i.", HSCORE_MAX_SHIPSETS);
		return;
	}


	if (database->areShipsLoaded(t))
	{
		char unowned[] = " ";
		char unaval[] = "Free";
		char *status[8];

		//+---------+---------+--------+-----------+---------+--------+-----------+--------+
		//| Warbird | Javelin | Spider | Leviathan | Terrier | Weasel | Lancaster | Shark  |
		//+---------+---------+--------+-----------+---------+--------+-----------+--------+


		for (int i = 0; i < 8; i++)
		{
			if (database->getPlayerHull(t, i, shipset) != NULL)
			{
				status[i] = shipNames[i];
			}
			else
			{
				if (cfg->GetInt(p->arena->cfg, shipNames[i], "BuyPrice", 0) == 0)
				{
					//not for sale
					status[i] = unaval;
				}
				else
				{
					//unowned
					status[i] = unowned;
				}
			}
		}

		chat->SendMessage(p, "+---------+---------+--------+-----------+---------+--------+-----------+--------+");
		chat->SendMessage(p, "| %-7s | %-7s | %-6s | %-9s | %-7s | %-6s | %-9s | %-6s |", status[0], status[1], status[2], status[3], status[4], status[5], status[6], status[7]);
		chat->SendMessage(p, "+---------+---------+--------+-----------+---------+--------+-----------+--------+");
	}
	else
	{
		if (p == t)
		{
			chat->SendMessage(p, "No ships loaded.");
		}
		else
		{
			chat->SendMessage(p, "No ships loaded for %s.", t->name);
		}
	}
}

local int printCacheEntry(const char *key, void *val, void *clos)
{
	PropertyCacheEntry *cacheEntry = val;
	Player *p = clos;

	if (cacheEntry != NULL)
	{
		if (cacheEntry->absolute)
		{
			chat->SendMessage(p, "| %-16s | =%-13i |", key, cacheEntry->value);
		}
		else if (cacheEntry->value != 0)
		{
			chat->SendMessage(p, "| %-16s | %+-14i |", key, cacheEntry->value);
		}
	}

	return 0;
}

local int cacheEntriesToList(const char *key, void *val, void *clos)
{
	PropertyCacheEntry *cacheEntry = (PropertyCacheEntry *)val;
	LinkedList *list = (LinkedList *)clos;

	if (cacheEntry != NULL)
	{
		if (cacheEntry->absolute || cacheEntry->value != 0)
		{
			KeyCacheEntryPair *pair = amalloc(sizeof(*pair));
			pair->key = key;
			pair->cacheEntry = cacheEntry;
			LLAdd(list, pair);
		}
	}
	return 0;
}

local helptext_t cacheHelp =
"Targets: none or player\n"
"Args: [ship number]\n"
"Displays the players current property cache.\n"
"For debugging purposes only.\n";

local void cacheCommand(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;

	int ship = atoi(params);
	if (ship == 0)
	{
		ship = t->p_ship;
	}
	else
	{
		ship--; //warbird is 0, not 1
	}

	if (ship == SHIP_SPEC)
	{
		chat->SendMessage(p, "Spectators do not have items. Please use ?shipitems <ship> to check items on a certain hull.");
		return;
	}

	if (ship >= 8 || ship < 0)
	{
		chat->SendMessage(p, "Ship out of range. Please choose a ship from 1 to 8.");
		return;
	}

	if (database->areShipsLoaded(t))
	{
		ShipHull *hull = database->getPlayerShipHull(t, ship);

		if (hull != NULL)
		{
			chat->SendMessage(p, "+------------------+");
			chat->SendMessage(p, "| %-16s |", shipNames[ship]);
			chat->SendMessage(p, "+------------------+----------------+");
			chat->SendMessage(p, "| Property Name    | Property Value |");
			chat->SendMessage(p, "+------------------+----------------+");

			HashEnum(hull->propertySums, printCacheEntry, p);

			chat->SendMessage(p, "+------------------+----------------+");
		}
		else
		{
			int buyPrice = cfg->GetInt(p->arena->cfg, shipNames[ship], "BuyPrice", 0);

			if (buyPrice == 0)
			{
				chat->SendMessage(p, "No items can be loaded onto a %s in this arena.", shipNames[ship]);
			}
			else
			{
				if (p == t)
					chat->SendMessage(p, "You do not own a %s.", shipNames[ship]);
				else
					chat->SendMessage(p, "Player %s does not own a %s.", t->name, shipNames[ship]);
			}
		}
	}
	else
	{
		if (p == t)
			chat->SendMessage(p, "Unexpected error: Your ships are not loaded.");
		else
			chat->SendMessage(p, "Unexpected error: %s's ships are not loaded.", t->name);
	}
}


local helptext_t shipItemsHelp =
"Targets: none or player\n"
"Args: [-ss <shipset #>] [ship number]\n"
"Displays a short list of the ship's items.\n"
"If no ship is specified, current ship is assumed.\n";

local void shipItemsCommand(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;
	char *next;

	int ship = t->p_ship;
	int shipset = database->getPlayerShipSet(t);

	while (params) {
	  switch (*params) {
		case '-':
		  ++params;
		  if (!strncmp(params, "ss", 2)) {
			params = strchr(params, ' ');
			shipset = strtol(params, &next, 0) - 1;

			if (next == params) {
			  chat->SendMessage(p, "shipstatus: bad shipset value.");
			  return;
			}

			params = next;
		  }
		  break;

		case ' ':
		  ++params; // Skip!
		  break;

		default:
		  if ((ship = atoi(params))) {
			--ship;
		  } else {
			ship = t->p_ship;
		  }

		  params = NULL;
	  }
	}

	if (ship == SHIP_SPEC)
	{
		chat->SendMessage(p, "Spectators do not have items. Please use ?shipitems <ship> to check items on a certain hull.");
		return;
	}

	if (ship < SHIP_WARBIRD || ship > SHIP_SHARK)
	{
		chat->SendMessage(p, "Ship out of range. Please choose a ship from 1 to 8.");
		return;
	}

	if (shipset < 0 || shipset > HSCORE_MAX_SHIPSETS) {
		chat->SendMessage(p, "Shipset out of range. Please choose a ship from 1 to %i.", HSCORE_MAX_SHIPSETS);
		return;
	}

	if (database->areShipsLoaded(t))
	{
		ShipHull *hull = database->getPlayerHull(t, ship, shipset);
		Link *link;

		if (hull != NULL)
		{
			int first = 1;
			char line[100];
			char buffer[100];
			int lineLen = 0;
			int bufferLen;

			line[0] = '\0';

			chat->SendMessage(p, "+------------------+");
			chat->SendMessage(p, "| %-16s |", shipNames[ship]);
			chat->SendMessage(p, "+------------------+--------------------------------------------------------------------------+");

			database->lock();
			for (link = LLGetHead(&hull->inventoryEntryList); link; link = link->next)
			{
				InventoryEntry *entry = link->data;
				Item *item = entry->item;
				int count = entry->count;
				int last = (link->next == NULL);

				if (first && last)
				{
					first = 0;

					if (count == 1)
					{
						sprintf(buffer, "%s", item->name);
					}
					else
					{
						sprintf(buffer, "%d %s", count, item->name);
					}
				}
				else if (first)
				{
					first = 0;

					if (count == 1)
					{
						sprintf(buffer, "%s,", item->name);
					}
					else
					{
						sprintf(buffer, "%d %s,", count, item->name);
					}
				}
				else if (last)
				{
					if (lineLen == 0)
					{
						if (count == 1)
						{
							sprintf(buffer, "%s", item->name);
						}
						else
						{
							sprintf(buffer, "%d %s", count, item->name);
						}
					}
					else
					{
						if (count == 1)
						{
							sprintf(buffer, " %s", item->name);
						}
						else
						{
							sprintf(buffer, " %d %s", count, item->name);
						}
					}
				}
				else
				{
					if (lineLen == 0)
					{
						if (count == 1)
						{
							sprintf(buffer, "%s,", item->name);
						}
						else
						{
							sprintf(buffer, "%d %s,", count, item->name);
						}
					}
					else
					{
						if (count == 1)
						{
							sprintf(buffer, " %s,", item->name);
						}
						else
						{
							sprintf(buffer, " %d %s,", count, item->name);
						}
					}
				}

				//buffer is now filled with the properly formatted string.
				bufferLen = strlen(buffer);
				if (lineLen + bufferLen <= 91)
				{
					strcat(line, buffer);
					lineLen = strlen(line);
				}
				else
				{
					//need a new line
					chat->SendMessage(p, "| %-91s |", line);

					*line = '\0';
					if (*buffer == ' ')
					{
						strcat(line, buffer + 1);
					}
					else
					{
						strcat(line, buffer);
					}

					lineLen = strlen(line);
				}
			}
			database->unlock();

			//check if there's still stuff left in the line
			if (lineLen != 0)
			{
				chat->SendMessage(p, "| %-91s |", line);
			}

			chat->SendMessage(p, "+---------------------------------------------------------------------------------------------+");
		}
		else
		{
			int buyPrice = cfg->GetInt(p->arena->cfg, shipNames[ship], "BuyPrice", 0);

			if (buyPrice == 0)
			{
				chat->SendMessage(p, "No items can be loaded onto a %s in this arena.", shipNames[ship]);
			}
			else
			{
				if (p == t)
					chat->SendMessage(p, "You do not own a %s on shipset %i.", shipNames[ship], shipset + 1);
				else
					chat->SendMessage(p, "Player %s does not own a %s on shipset %i.", t->name, shipNames[ship], shipset + 1);
			}
		}
	}
	else
	{
		if (p == t)
			chat->SendMessage(p, "Unexpected error: Your ships are not loaded.");
		else
			chat->SendMessage(p, "Unexpected error: %s's ships are not loaded.", t->name);
	}
}

local helptext_t shipStatusHelp =
"Targets: none or player\n"
"Args: [-ss <shipset #>] [-v] [ship number]\n"
"Displays the specified ship's inventory.\n"
"If no ship is specified, current ship is assumed.\n"
"If no shipset is specified, current shipset is assumed.\n"
"The -v flag will give you a list of all properties set on your ship.\n";

local void shipStatusCommand(const char *command, const char *params, Player *p, const Target *target)
{
	Player *t = (target->type == T_PLAYER) ? target->u.p : p;
	char *next;

	int verbose = 0;
	int ship = t->p_ship;
	int shipset = database->getPlayerShipSet(t);

	while (params) {
	  switch (*params) {
		case '-':
		  ++params;
		  if (!strncmp(params, "ss", 2)) {
			params = strchr(params, ' ');
			shipset = strtol(params, &next, 0) - 1;

			if (next == params) {
			  chat->SendMessage(p, "shipstatus: bad shipset value.");
			  return;
			}

			params = next;
		  } else if (*params == 'v') {
			verbose = 1;
			++params;
		  }
		  break;

		case ' ':
		  ++params; // Skip!
		  break;

		default:
		  if ((ship = atoi(params))) {
			--ship;
		  } else {
			ship = t->p_ship;
		  }

		  params = NULL;
	  }
	}

	if (ship == SHIP_SPEC) {
		chat->SendMessage(p, "Spectators do not have a ship status. Please use ?shipstatus <ship> to check the status on a certain hull.");
		return;
	}

	if (ship < SHIP_WARBIRD || ship > SHIP_SHARK)
	{
		chat->SendMessage(p, "Ship out of range. Please choose a ship from 1 to 8.");
		return;
	}

	if (shipset < 0 || shipset > HSCORE_MAX_SHIPSETS) {
		chat->SendMessage(p, "Shipset out of range. Please choose a ship from 1 to %i.", HSCORE_MAX_SHIPSETS);
		return;
	}

	if (database->areShipsLoaded(t))
	{
		ShipHull *hull = database->getPlayerHull(t, ship, shipset);

		if (hull != NULL)
		{
			chat->SendMessage(p, "+------------------+");
			chat->SendMessage(p, "| %-16s |", shipNames[ship]);
			chat->SendMessage(p, "+------------------+-------+--------+-----------------------------------------------------+");
			chat->SendMessage(p, "| Item Name        | Count | Ammo   | Item Types                                          |");
			chat->SendMessage(p, "+------------------+-------+--------+-----------------------------------------------------+");

			Link *link;

			database->lock();
			for (link = LLGetHead(&hull->inventoryEntryList); link; link = link->next)
			{
				InventoryEntry *entry = link->data;
				Item *item = entry->item;
				int first = 1;
				char itemTypes[256];
				char buf[256];
				char ammoString[20];
				Link *itemTypeLink;

				sprintf(ammoString, "      ");

				if (item->ammo != NULL)
				{
					Link *ammoLink;
					for (ammoLink = LLGetHead(&hull->inventoryEntryList); ammoLink; ammoLink = ammoLink->next)
					{
						InventoryEntry *ammoEntry = ammoLink->data;

						if (ammoEntry->item == item->ammo)
						{
							sprintf(ammoString, "%6i", ammoEntry->count);
							break;
						}
					}
				}

				itemTypes[0] = '\0';
				for (itemTypeLink = LLGetHead(&item->itemTypeEntries); itemTypeLink; itemTypeLink = itemTypeLink->next)
				{
					ItemTypeEntry *entry = itemTypeLink->data;

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

				chat->SendMessage(p, "| %-16s | %5i | %s | %-51s |", item->name, entry->count, ammoString, itemTypes);
			}

			if (!verbose)
			{
				chat->SendMessage(p, "+------------------+-------+--------+-----------------------------------------------------+");
			}
			else
			{
				LinkedList propertiesList;
				LLInit(&propertiesList);
				Link *pairsLink;

				chat->SendMessage(p, "+------------------+-------+--------+-----------------------------------------------------+");
				chat->SendMessage(p, "| Property Name    | Property Value |");
				chat->SendMessage(p, "+------------------+----------------+");

				items->recalculateEntireCacheForShipSet(t, ship, shipset);
				//unsorted method
				//HashEnum(hull->propertySums, printCacheEntry, p);

				//new sorted method
				HashEnum(hull->propertySums, cacheEntriesToList, &propertiesList);
				LLSort(&propertiesList, sortKeyCacheEntryPair);

				for (pairsLink = LLGetHead(&propertiesList); pairsLink; pairsLink = pairsLink->next)
				{
					KeyCacheEntryPair *pair = (KeyCacheEntryPair *)pairsLink->data;
					if (pair->cacheEntry->absolute)
					{
						chat->SendMessage(p, "| %-16s | =%-13i |", pair->key, pair->cacheEntry->value);
					}
					else
					{
						chat->SendMessage(p, "| %-16s | %+-14i |", pair->key, pair->cacheEntry->value);
					}
				}
				LLEnum(&propertiesList, afree);
				LLEmpty(&propertiesList);
				//end new sorted method

				chat->SendMessage(p, "+------------------+----------------+");
			}
			database->unlock();
		}
		else
		{
			int buyPrice = cfg->GetInt(p->arena->cfg, shipNames[ship], "BuyPrice", 0);

			if (buyPrice == 0)
			{
				chat->SendMessage(p, "No items can be loaded onto a %s in this arena.", shipNames[ship]);
			}
			else
			{
				if (p == t)
					chat->SendMessage(p, "You do not own a %s on shipset %i.", shipNames[ship], shipset + 1);
				else
					chat->SendMessage(p, "Player %s does not own a %s on shipset %i.", t->name, shipNames[ship], shipset + 1);
			}
		}
	}
	else
	{
		if (p == t)
			chat->SendMessage(p, "Unexpected error: Your ships are not loaded.");
		else
			chat->SendMessage(p, "Unexpected error: %s's ships are not loaded.", t->name);
	}
}

local helptext_t shipInfoHelp =
"Targets: none\n"
"Args: [ship number]\n"
"Displays info about the specified ship.\n"
"If no ship is specified, current ship is assumed.\n";

local void shipInfoCommand(const char *command, const char *params, Player *p, const Target *target)
{
	int ship;

	ship = p->p_ship;
	if (params != NULL)
	{
		ship = atoi(params);
		if (ship == 0)
		{
			ship = p->p_ship;
		}
		else
		{
			ship--; //warbird is 0, not 1
		}
	}

	if (ship == SHIP_SPEC)
	{
		chat->SendMessage(p, "Spectators do not have any ship information. Please use ?shipinfo <#> to check the info on a certain hull.");
		return;
	}

	if (ship >= 8 || ship < 0)
	{
		chat->SendMessage(p, "Ship out of range. Please choose a ship from 1 to 8.");
		return;
	}
	else
	{
		char *shipname = shipNames[ship];
		ConfigHandle conf = p->arena->cfg;

		int initEnergy = cfg->GetInt(conf, shipname, "InitialEnergy", 0);
		int maxEnergy = cfg->GetInt(conf, shipname, "MaximumEnergy", 0);
		int upgradeEnergy = cfg->GetInt(conf, shipname, "UpgradeEnergy", 0);
		int initRecharge = cfg->GetInt(conf, shipname, "InitialRecharge", 0);
		int maxRecharge = cfg->GetInt(conf, shipname, "MaximumRecharge", 0);
		int upgradeRecharge = cfg->GetInt(conf, shipname, "UpgradeRecharge", 0);
		int initSpeed = cfg->GetInt(conf, shipname, "InitialSpeed", 0);
		int maxSpeed = cfg->GetInt(conf, shipname, "MaximumSpeed", 0);
		int upgradeSpeed = cfg->GetInt(conf, shipname, "UpgradeSpeed", 0);
		int initThrust = cfg->GetInt(conf, shipname, "InitialThrust", 0);
		int maxThrust = cfg->GetInt(conf, shipname, "MaximumThrust", 0);
		int upgradeThrust = cfg->GetInt(conf, shipname, "UpgradeThrust", 0);
		int initRotation = cfg->GetInt(conf, shipname, "InitialRotation", 0);
		int maxRotation = cfg->GetInt(conf, shipname, "MaximumRotation", 0);
		int upgradeRotation = cfg->GetInt(conf, shipname, "UpgradeRotation", 0);
		int bulletSpeed = cfg->GetInt(conf, shipname, "BulletSpeed", 0);
		int bombSpeed = cfg->GetInt(conf, shipname, "BombSpeed", 0);
		int shrapnelRate = cfg->GetInt(conf, shipname, "ShrapnelRate", 0);
		int afterburnerEnergy = cfg->GetInt(conf, shipname, "AfterburnerEnergy", 0);
		int cloakEnergy = cfg->GetInt(conf, shipname, "CloakEnergy", 0);
		int stealthEnergy = cfg->GetInt(conf, shipname, "StealthEnergy", 0);
		int antiwarpEnergy = cfg->GetInt(conf, shipname, "AntiWarpEnergy", 0);
		int xradarEnergy = cfg->GetInt(conf, shipname, "XRadarEnergy", 0);
		int radius = cfg->GetInt(conf, shipname, "Radius", 0);


		LinkedList *shipProperties = database->getShipPropertyList(p->arena, ship);
		Link *link;

		chat->SendMessage(p, "+------------------+");
		chat->SendMessage(p, "| %-16s |", shipname);
		chat->SendMessage(p, "+------------------+---+----------------------+------------------------+---------------------------+");
		chat->SendMessage(p, "| Init Energy:   %5d | Init Recharge: %5d | Init Speed:      %5d | Init Thrust:        %5d |", initEnergy, initRecharge, initSpeed, initThrust);
		chat->SendMessage(p, "| Max Energy:    %5d | Max Recharge:  %5d | Max Speed:       %5d | Max Thrust:         %5d |", maxEnergy, maxRecharge, maxSpeed, maxThrust);
		chat->SendMessage(p, "| Upgrade:       %5d | Upgrade:       %5d | Upgrade:         %5d | Upgrade:            %5d |", upgradeEnergy, upgradeRecharge, upgradeSpeed, upgradeThrust);
		chat->SendMessage(p, "+----------------------+----------------------+------------------------+---------------------------+");
		chat->SendMessage(p, "| Init Rotation: %5d | Bullet Speed:  %5d | Cloak Energy:    %5d | XRadar Energy:      %5d |", initRotation, bulletSpeed, cloakEnergy, xradarEnergy);
		chat->SendMessage(p, "| Max Rotation:  %5d | Bomb Speed:    %5d | Stealth Energy:  %5d | Afterburner Energy: %5d |", maxRotation, bombSpeed, stealthEnergy, afterburnerEnergy);
		chat->SendMessage(p, "| Upgrade:       %5d | Shrapnel Rate: %5d | AntiWarp Energy: %5d | Radius:             %5d |", upgradeRotation, shrapnelRate, antiwarpEnergy, radius);
		chat->SendMessage(p, "+----------------------+----------------------+------------------------+---------------------------+");

		int propertyCount = 0;
		for (link = LLGetHead(shipProperties); link; link = link->next)
		{
			if (!propertyCount) {
				chat->SendMessage(p, "| Property Name        | Property Value       |");
				chat->SendMessage(p, "+----------------------+----------------------+");
			}

			Property *prop = link->data;
			if (prop->absolute)
			{
					chat->SendMessage(p, "| %-20s | =%-19i |", prop->name, prop->value);
			}
			else
			{
					chat->SendMessage(p, "| %-20s | %+-20i |", prop->name, prop->value);
			}
			propertyCount++;
		}

		if (propertyCount) {
			chat->SendMessage(p, "+----------------------+----------------------+");
		}
	}
}

EXPORT const char info_hscore_commands[] = "v1.1 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_commands(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !chat || !cfg || !cmd || !items || !database)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(database);

			return MM_FAIL;
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		cmd->AddCommand("ships", shipsCommand, arena, shipsHelp);
		cmd->AddCommand("shipstatus", shipStatusCommand, arena, shipStatusHelp);
		cmd->AddCommand("shipitems", shipItemsCommand, arena, shipItemsHelp);
		cmd->AddCommand("shipinfo", shipInfoCommand, arena, shipInfoHelp);
		cmd->AddCommand("cache", cacheCommand, arena, cacheHelp);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		cmd->RemoveCommand("ships", shipsCommand, arena);
		cmd->RemoveCommand("shipstatus", shipStatusCommand, arena);
		cmd->RemoveCommand("shipitems", shipItemsCommand, arena);
		cmd->RemoveCommand("shipinfo", shipInfoCommand, arena);
		cmd->RemoveCommand("cache", cacheCommand, arena);

		return MM_OK;
	}
	return MM_FAIL;
}
