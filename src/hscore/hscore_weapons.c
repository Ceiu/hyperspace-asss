#include "asss.h"
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

#include "hscore.h"
#include "hscore_items.h"
#include "packets/ppk.h"

local Ihscoreitems *items;

local void edit_ppk(Player *p, struct C2SPosition *pos)
{
	if ((pos->weapon.type == W_BULLET) || (pos->weapon.type == W_BOUNCEBULLET))
	{
		int gunlevel = items->getPropertySum(p, p->p_ship, "gunlevel", 0);
		if (gunlevel == 4)
		{
			pos->weapon.level = 3;
		}
	}
	else if ((pos->weapon.type == W_BOMB) || (pos->weapon.type == W_PROXBOMB))
	{
		int bomblevel = items->getPropertySum(p, p->p_ship, "bomblevel", 0);
		if (bomblevel == 4)
		{
			pos->weapon.level = 3;
		}

		if (pos->weapon.shrap > 0)
		{
			int shraplevel = items->getPropertySum(p, p->p_ship, "shraplevel", 0);
			//0 -> shrap level = defshraplevel or, if that's 0 as well, no modification
			//else = new level (represented in packet as level-1)
			if (shraplevel > 0)
			{
				pos->weapon.shraplevel = min(3, shraplevel-1);
			}
			else
			{
				int defshraplevel = items->getPropertySum(p, p->p_ship, "defshraplevel", 0);
				if (defshraplevel > 0)
				{
					pos->weapon.shraplevel = min(3, defshraplevel-1);
				}
			}
		}
	}
}

local Appk myadv =
{
	ADVISER_HEAD_INIT(A_PPK)
	edit_ppk, NULL
};

EXPORT const char info_hscore_weapons[] = "v1.0 Arnk Kilo Dylie <kilodylie@rshl.org>";

EXPORT int MM_hscore_weapons(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);

		if (!items)
		{
			return MM_FAIL;
		}

		mm->RegAdviser(&myadv, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregAdviser(&myadv, ALLARENAS);

		mm->ReleaseInterface(items);

		return MM_OK;
	}
	return MM_FAIL;
}

