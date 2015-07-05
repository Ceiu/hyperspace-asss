#include "asss.h"

local Ilogman *lm;
local Imapdata *mapdata;
local Icmdman *cmd;
local Ichat *chat;

local helptext_t region_help =
"Targets: none\n"
"Args: <region>\n"
"Checks if you are in the given region.\n";

local void Cregion(const char *command, const char *params, Player *p, const Target *target)
{
	Region *region = mapdata->FindRegionByName(p->arena, params);
	if (region != NULL)
	{
		if (mapdata->Contains(region, p->position.x >> 4, p->position.y >> 4))
		{
			int x, y;
			mapdata->FindRandomPoint(region, &x, &y);
			chat->SendMessage(p, "You are in region %s (%d %d)", mapdata->RegionName(region), x, y);
		}
		else
		{
			chat->SendMessage(p, "You are not in region %s", mapdata->RegionName(region));
		}
	}
	else
	{
		chat->SendMessage(p, "No such region %s", params);
	}
}

EXPORT const char info_hs_regiontest[] = "v1.2 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_regiontest(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);

		if (!lm || !cmd || !chat || !mapdata)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(mapdata);
			return MM_FAIL;
		}

		cmd->AddCommand("region", Cregion, ALLARENAS, region_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("region", Cregion, ALLARENAS);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(mapdata);

		return MM_OK;
	}
	return MM_FAIL;
}

