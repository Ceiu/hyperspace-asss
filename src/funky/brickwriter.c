
/* dist: public */

#include <string.h>

#include "asss.h"
#include "brickwriter.h"

#include "letters.inc"

local Ibricks *bricks;
local Icmdman *cmd;

local void BrickWrite(Arena *arena, int freq, int x, int y, const char *text)
{
	int i, wid;

	wid = 0;
	for (i = 0; i < (int)strlen(text); i++)
		if (text[i] >= ' ' && text[i] <= '~')
			wid += letterdata[(int)text[i] - ' '].width + 1;

	x -= wid / 2;
	y -= letterheight / 2;

	for (i = 0; i < (int)strlen(text); i++)
		if (text[i] >= ' ' && text[i] <= '~')
		{
			int c = text[i] - ' ';
			int bnum = letterdata[c].bricknum;
			struct bl_brick *brk = letterdata[c].bricks;
			for ( ; bnum ; bnum--, brk++)
			{
				bricks->DropBrick(
						arena,
						freq,
						x + brk->x1,
						y + brk->y1,
						x + brk->x2,
						y + brk->y2);
			}
			x += letterdata[c].width + 1;
		}
}

local void Cbrickwrite(const char *tc, const char *params, Player *p, const Target *target)
{
	BrickWrite(p->arena, p->p_freq, p->position.x >> 4, p->position.y >> 4, params);
}

local Ibrickwriter bwint =
{
	INTERFACE_HEAD_INIT(I_BRICKWRITER, "brickwriter-1")
	BrickWrite
};

EXPORT const char info_brickwriter[] = CORE_MOD_INFO("brickwriter");

EXPORT int MM_brickwriter(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		bricks = mm->GetInterface(I_BRICKS, ALLARENAS);

		if (!bricks) return MM_FAIL;

		cmd->AddCommand("brickwrite", Cbrickwrite, ALLARENAS, NULL);
		mm->RegInterface(&bwint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&bwint, ALLARENAS))
			return MM_FAIL;
		cmd->RemoveCommand("brickwrite", Cbrickwrite, ALLARENAS);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(bricks);
		return MM_OK;
	}
	return MM_FAIL;
}

