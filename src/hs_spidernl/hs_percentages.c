#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_spawner.h"

//Interfaces
local Imodman *mm;
local Ihscoreitems *items;

//Prototypes
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value);

local void getInterfaces();
local bool checkInterfaces();
local void releaseInterfaces();

//Advisers
local int getOverrideValueCB(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    if (strcmp(prop, "bulletdelay") == 0)
    {
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "bulletdelaypct", 100) / 100.0);
    }
    else if (strcmp(prop, "multidelay") == 0)
    {
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "multidelaypct", 100) / 100.0);
    }
    else if (strcmp(prop, "bombdelay") == 0)
    {
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "bombdelaypct", 100) / 100.0);
    }
    else if (strcmp(prop, "bulletenergy") == 0)
    {
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "bulletenergypct", 100) / 100.0);
    }
    else if (strcmp(prop, "multienergy") == 0)
    {
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "multienergypct", 100) / 100.0);
    }
    else if (strcmp(prop, "bombenergy") == 0)
    {
        return init_value * (items->getPropertySumOnShipSet(p, ship, shipset, "bombenergypct", 100) / 100.0);
    }

    return init_value;
}

local Ahscorespawner overrideAdviser =
{
	ADVISER_HEAD_INIT(A_HSCORE_SPAWNER)

	getOverrideValueCB
};

//Used interfaces, etc. -> Module stuff..
local void getInterfaces()
{
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
}
local bool checkInterfaces()
{
    if (items) return true;
    return false;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(items);
}

EXPORT const char info_hs_percentages[] = "hs_percentages v1.0 by Spidernl\n";
EXPORT int MM_hs_percentages(int action, Imodman *mm_, Arena *arena)
{
    if (action == MM_LOAD)
	{
		mm = mm_;

		getInterfaces();
		if (!checkInterfaces())
		{
            releaseInterfaces();
            return MM_FAIL;
        }

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        releaseInterfaces();

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        mm->RegAdviser(&overrideAdviser, arena);

        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        mm->UnregAdviser(&overrideAdviser, arena);

        return MM_OK;
    }

	return MM_FAIL;
}
