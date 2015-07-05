#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_shipnames.h"
#include "kill.h"
#include "selfpos.h"
#include "balls.h"

#define KILLER_NAME "<engine failure>"

typedef struct Transwarp
{
	int x1, y1, x2, y2;

	Region *dest;
	const char *regionName;

	int noFlags;

	int level; //0 = public
	int eventCount; //number of times a transwarp event happens
} Transwarp;

typedef struct adata
{
	int on;

	Killer *killer;

	Target target;

	int bounty[8];

	// ?tw stuff
	int x;
	int y;
	int v_x;
	int v_y;
	int noFlags;
	int level; //0 = public
	int eventCount; //number of times a transwarp event happens

	// graphics stuff
	int lastId;
	int imageBase;
	int imageCount;
	int enterImageID;
	int enterImageWidth;
	int enterImageHeight;
	int exitImageID;
	int exitImageWidth;
	int exitImageHeight;

	LinkedList allowTWRegion;
	LinkedList blockTWRegion;
	//Region *twOut;

	int warpCount;
	Transwarp *warpList;
	int cfg_AllowPriv;
} adata;

typedef struct pdata
{
	ticks_t nextWarp;
	short destX;
	short destY;
	short destRadius;
	int attemptingWarp : 1;
	int padding : 7;
} pdata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Iarenaman *aman;
local Iconfig *cfg;
local Iplayerdata *pd;
local Ihscoreitems *items;
local Iflagcore *fc;
local Iselfpos *selfpos;
local Ikill *kill;
local Imapdata *mapdata;
local Icmdman *cmdman;
local Iobjects *objects;
local Ichat *chat;
local Iballs *balls;
local Imainloop *ml;

local int pdkey;
local int adkey;
local void tryWarp(Player *p);
local int graphicsTimer(void *_p);

local int isBallCarrier(Arena *a, Player *p)
{
	ArenaBallData *abd;
	struct BallData bd;
	int i;
	int result = 0;

	abd = balls->GetBallData(a);
	for (i = 0; i < abd->ballcount; ++i)
	{
		bd = abd->balls[i];
		if (bd.carrier == p && bd.state == BALL_CARRIED)
		{
			result = 1;
			break;
		}
	}
	balls->ReleaseBallData(a);

	return result;
}


local helptext_t tw_help =
"Targets: none\n"
"Args: none\n"
"Opens a transwarp conduit between your sector and the hub.\n";

local void Ctw(const char *command, const char *params, Player *p, const Target *target)
{
	int level;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	Link *l;
	Region *checkRegion;
	int allow = 0;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if(!ad->cfg_AllowPriv && p->p_freq >= 100)
	{
		chat->SendMessage(p, "Private Frequencies are not allowed to leave center.");
		return;
	}

	if (current_ticks() < data->nextWarp)
	{
		chat->SendMessage(p, "TW in cool down phase. Please wait a moment.");
		return;
	}

	level = items->getPropertySum(p, p->p_ship, "transwarp", 0);

	if (level == 0 && ad->level != 0)
	{
		chat->SendMessage(p, "You do not have an item capable of creating a transwarp conduit.");
		return;
	}

	if (level < ad->level)
	{
		chat->SendMessage(p, "You do not own enough items to create a transwarp conduit.");
		return;
	}

	/*if (!ad->twOut)
	{
		lm->LogP(L_WARN, "hs_tw2", p, "Invalid twOut region");
		return;
	}

    if (!mapdata->Contains(ad->twOut, p->position.x >> 4, p->position.y >> 4))
	{
		chat->SendMessage(p, "You cannot open a conduit from your current location. Try moving into an open space.");
		return;
	}*/
	//see if the user is allowed to TW here according to Allowed regions
	FOR_EACH(&ad->allowTWRegion, checkRegion, l)
	{
		if (mapdata->Contains(checkRegion, p->position.x>>4, p->position.y>>4))
		{
			allow = 1;
			break;
		}
	}
	//see if the user is in an excepted Blocked region.
	if (allow)
	{
		FOR_EACH(&ad->blockTWRegion, checkRegion, l)
		{
			if (mapdata->Contains(checkRegion, p->position.x>>4, p->position.y>>4))
			{
				allow = 0;
				break;
			}
		}
	}

	if (!allow)
	{
		chat->SendMessage(p, "Unable to open a transwarp conduit from this location. Try moving into open space.");
		return;
	}

	//check for flags
	if (ad->noFlags == 0 || fc->CountPlayerFlags(p) == 0)
	{
		int imagetime = items->getPropertySum(p, p->p_ship, "twimage", 75);
		int warmup = items->getPropertySum(p, p->p_ship, "twwarmup", 200);

		// cool down
		data->nextWarp = current_ticks() + warmup;

		//set up timers
		ml->SetTimer(graphicsTimer, imagetime, imagetime, p, p);
		data->attemptingWarp = 1;
		data->destX = 0;
		data->destY = 0;
		data->destRadius = 0;
	}
	else
	{
		kill->Kill(p, ad->killer, 0, 0);
	}
}

local int graphicsTimer(void *_p)
{
	Player *p = (Player *)_p;
	Arena *a = p->arena;
	struct adata *ad = P_ARENA_DATA(a, adkey);
	pdata *data = PPDATA(p, pdkey);
	int dx, dy, timeAway;
	int imageId = ad->lastId + 1;

	if (!data->attemptingWarp)
	{
		return 0;
	}

	if (imageId > ad->imageBase + ad->imageCount);
	{
		imageId = ad->imageBase;
	}
	ad->lastId = imageId;

	timeAway = items->getPropertySum(p, p->p_ship, "twwarmup", 200) - items->getPropertySum(p, p->p_ship, "twimage", 75);
	if (timeAway < 0)
		timeAway = 0;

	dx = p->position.x + ((int)p->position.xspeed * timeAway) / 1000;
	dy = p->position.y + ((int)p->position.yspeed * timeAway) / 1000;

	data->destX = (short)dx;
	data->destY = (short)dy;
	data->destRadius = items->getPropertySum(p, p->p_ship, "twarea", 56);

	objects->Move(&(ad->target), imageId, data->destX - (ad->enterImageWidth / 2), data->destY - (ad->enterImageHeight / 2), 0, 0);
	objects->Image(&(ad->target), imageId, ad->enterImageID);
	objects->Toggle(&(ad->target), imageId, 1);
	return 0;
}

local void tryWarp(Player *p)
{
	Arena *a = p->arena;
	Player *k;
	Link *link;
	struct adata *ad = P_ARENA_DATA(a, adkey);
	pdata *data = PPDATA(p, pdkey);
	int i;

	if (!data->attemptingWarp)
	{
		return;
	}
	data->attemptingWarp = 0;

	if (isBallCarrier(p->arena, p))
	{
		chat->SendMessage(p, "You cannot carry a powerball into the transwarp.");
		return;
	}

	// remove turreters
	pd->Lock();
	FOR_EACH_PLAYER(k)
	{
		if (k->arena == a && k->p_attached == p->pid && k->p_ship != SHIP_SPEC)
		{
			kill->Kill(k, ad->killer, 0, 0);
		}
	}
	pd->Unlock();

	if (ad->noFlags == 0 || fc->CountPlayerFlags(p) == 0)
	{
		// TODO: clean this bounty stuff up
		p->position.bounty = ad->bounty[p->p_ship];
		selfpos->WarpPlayer(p, ad->x << 4, ad->y << 4, ad->v_x, ad->v_y, p->position.rotation, 0);

		// event
		for (i = 0; i < ad->eventCount; i++)
		{
			items->triggerEvent(p, p->p_ship, "transwarp");
		}
	}
	else
	{
		kill->Kill(p, ad->killer, 0, 0);
	}

	return;
}

local int warpPlayer(Player *p, Transwarp *warp, struct C2SPosition *pos, int radius)
{
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	int imageId;
	int i;

	if (warp->noFlags == 0 || fc->CountPlayerFlags(p) == 0)
	{
		Player *k;
		Link *link;

		int v_x = pos->xspeed;
		int v_y = pos->yspeed;

		int newx;
		int newy;

		mapdata->FindRandomPoint(warp->dest, &newx, &newy);

		if (newx == -1 || newy == -1)
		{
			lm->LogP(L_WARN, "hs_tw2", p, "Error warping");
			return 0;
		}

		imageId = ad->lastId + 1;
		if (imageId > ad->imageBase + ad->imageCount);
		{
			imageId = ad->imageBase;
		}
		ad->lastId = imageId;

		objects->Move(&(ad->target), imageId, (newx << 4) - (ad->exitImageWidth / 2), (newy << 4) - (ad->exitImageHeight / 2), 0, 0);
		objects->Image(&(ad->target), imageId, ad->exitImageID);
		objects->Toggle(&(ad->target), imageId, 1);

		p->position.bounty = ad->bounty[p->p_ship];
		selfpos->WarpPlayer(p, newx << 4, newy << 4, v_x, v_y, p->position.rotation, 0);
		data->nextWarp = current_ticks() + items->getPropertySum(p, p->p_ship, "twcooldown", 300);

		// remove turreters
		pd->Lock();
		FOR_EACH_PLAYER(k)
		{
			if (k->arena == p->arena && k->p_attached == p->pid && k->p_ship != SHIP_SPEC)
			{
				kill->Kill(k, ad->killer, 0, 0);
			}
		}
		pd->Unlock();

		for (i = 0; i < warp->eventCount; i++)
		{
			items->triggerEvent(p, p->p_ship, "transwarp");
		}

		return 1;
	}
	else
	{
		kill->Kill(p, ad->killer, 0, 0);
		return 1;
	}
}

local int inSquare(int sx, int sy, int r, int x, int y)
{
	if (x < (sx - r))
		return 0;
	if (x > (sx + r))
		return 0;
	if (y < (sy - r))
		return 0;
	if (y > (sy + r))
		return 0;

	return 1;
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	int i;
	int radius;
	int playerLevel;

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if (data->attemptingWarp)
	{
		if (pos->weapon.type != W_NULL)
		{
			chat->SendMessage(p, "Firing weapons and items prevents the transwarp from working!");
			data->attemptingWarp = 0;
			return;
		}
		else if (data->destRadius)
		{
			if (current_ticks() > data->nextWarp + 10)
			{
				chat->SendMessage(p, "You did not make it to the warp point in time!");
				data->attemptingWarp = 0;
				return;
			}
			else if (inSquare(data->destX, data->destY, data->destRadius, p->position.x, p->position.y))
			{
				tryWarp(p);
				return;
			}
		}
	}

	//at this point, we are done checking about moving us to the hub, and will check if we're in an exit point

	if (current_ticks() < data->nextWarp)
		return; //cooldown hasn't expired

	playerLevel = items->getPropertySum(p, p->p_ship, "transwarp", 0);
	radius = cfg->GetInt(arena->cfg, shipNames[p->p_ship], "Radius", 14);
	if (radius == 0) radius = 14;

	for (i = 0; i < ad->warpCount; i++)
	{
		Transwarp *warp = &(ad->warpList[i]);

		if (warp->dest == NULL)
			continue;

		if (warp->level > playerLevel)
			continue;

		if ((warp->x1 << 4) - radius <= pos->x && pos->x < ((warp->x2 + 1) << 4) + radius)
		{
			if ((warp->y1 << 4) - radius <= pos->y && pos->y < ((warp->y2 + 1) << 4) + radius)
			{
				//warp them
				if (warpPlayer(p, warp, pos, radius))
					return; //don't warp them more than once
			}
		}
	}
}

local void loadArenaConfig(Arena *a)
{
	char buf[256];
	int i;
	struct adata *ad = P_ARENA_DATA(a, adkey);
	//const char *twOutName;
	const char *attr;
	const char *tmp = 0;

	// Get the bounty first
	for (i = 0; i < 8; i++)
	{
		ad->bounty[i] = cfg->GetInt(a->cfg, shipNames[i], "InitialBounty", 0);
	}

	/* cfghelp: TW2:X, arena, int, def: 228, mod: hs_tw2
	 * The x coord of the hub */
	ad->x = cfg->GetInt(a->cfg, "TW2", "x", 369);
	/* cfghelp: TW2:Y, arena, int, def: 369, mod: hs_tw2
	 * The y coord of the hub */
	ad->y = cfg->GetInt(a->cfg, "TW2", "y", 228);
	/* cfghelp: TW2:VX, arena, int, def: 0, mod: hs_tw2
	 * The x velocity of the hub exit */
	ad->v_x = cfg->GetInt(a->cfg, "TW2", "VX", 0);
	/* cfghelp: TW2:VY, arena, int, def: 0, mod: hs_tw2
	 * The y velocity of the hub exit */
	ad->v_y = cfg->GetInt(a->cfg, "TW2", "VY", 0);
	/* cfghelp: TW2:Level, arena, int, def: 1, mod: hs_tw2
	 * The level of ?tw */
	ad->level = cfg->GetInt(a->cfg, "TW2", "Level", 1);
	/* cfghelp: TW2:EventCount, arena, int, def: 0, mod: hs_tw2
	 * Event count for ?tw */
	ad->eventCount = cfg->GetInt(a->cfg, "TW2", "EventCount", 0);
	/* cfghelp: TW2:NoFlags, arena, int, def: 0, mod: hs_tw2
	 * If ?tw allows flags */
	ad->noFlags = cfg->GetInt(a->cfg, "TW2", "NoFlags", 0);

	ad->cfg_AllowPriv = cfg->GetInt(a->cfg, "Hyperspace", "PrivFTL", 1);


	/* cfghelp: TW2:TWOutRegion, arena, int, def: 0, mod: hs_tw2
	 * The map region to warp the player into */
	/*twOutName = cfg->GetStr(a->cfg, "TW2", "TWOutRegion");
	if (twOutName != NULL)
	{
		ad->twOut = mapdata->FindRegionByName(a, twOutName);
		if (ad->twOut == NULL)
			lm->LogA(L_WARN, "hs_tw2", a, "could not find twOut region %s", twOutName);
	}*/
	LLInit(&ad->allowTWRegion);
	attr = mapdata->GetAttr(a, "twallowlist");
	if (attr)
	{
		while (strsplit(attr, ",;", buf, sizeof(buf), &tmp))
		{
			Region *rgn = mapdata->FindRegionByName(a, buf);
			if (!rgn)
				lm->LogA(L_WARN, "hs_tw2", a, "could not find requested allow region %s", buf);
			else
				LLAdd(&ad->allowTWRegion, rgn);
		}
	}
	else
	{
		if (cfg->GetInt(a->cfg, "TW2", "UseTWOutRegion", 1))
		{
			Region *rgn = mapdata->FindRegionByName(a, "twout");
			if (!rgn)
				lm->LogA(L_WARN, "hs_tw2", a, "could not find requested allow region twout");
			else
				LLAdd(&ad->allowTWRegion, rgn);
		}
	}

	tmp = 0;

	LLInit(&ad->blockTWRegion);
	attr = mapdata->GetAttr(a, "twblocklist");
	if (attr)
	{
		while (strsplit(attr, ",;", buf, sizeof(buf), &tmp))
		{
			Region *rgn = mapdata->FindRegionByName(a, buf);
			if (!rgn)
				lm->LogA(L_WARN, "hs_tw2", a, "could not find requested block region %s", buf);
			else
				LLAdd(&ad->blockTWRegion, rgn);
		}
	}

	/* cfghelp: TW2:ImageBase, arena, int, def: 980, mod: hs_tw2
	 * Image base */
	ad->lastId = ad->imageBase = cfg->GetInt(a->cfg, "TW2", "ImageBase", 980);
	/* cfghelp: TW2:ImageCount, arena, int, def: 20, mod: hs_tw2
	 * Image count */
	ad->imageCount = cfg->GetInt(a->cfg, "TW2", "ImageCount", 20);
	/* cfghelp: TW2:EnterImageID, arena, int, def: 1, mod: hs_tw2
	 * Image ID of enter image */
	ad->enterImageID = cfg->GetInt(a->cfg, "TW2", "EnterImageID", 1);
	/* cfghelp: TW2:EnterImageWidth, arena, int, def: 300, mod: hs_tw2
	 * Width of enter image */
	ad->enterImageWidth = cfg->GetInt(a->cfg, "TW2", "EnterImageWidth", 300);
	/* cfghelp: TW2:EnterImageHeight, arena, int, def: 300, mod: hs_tw2
	 * Height of enter image */
	ad->enterImageHeight = cfg->GetInt(a->cfg, "TW2", "EnterImageHeight", 300);
	/* cfghelp: TW2:ExitImageID, arena, int, def: 2, mod: hs_tw2
	 * Image ID of exit image */
	ad->exitImageID = cfg->GetInt(a->cfg, "TW2", "ExitImageID", 2);
	/* cfghelp: TW2:ExitImageWidth, arena, int, def: 300, mod: hs_tw2
	 * Width of exit image */
	ad->exitImageWidth = cfg->GetInt(a->cfg, "TW2", "ExitImageWidth", 300);
	/* cfghelp: TW2:ExitImageHeight, arena, int, def: 300, mod: hs_tw2
	 * Height of exit image */
	ad->exitImageHeight = cfg->GetInt(a->cfg, "TW2", "ExitImageHeight", 300);


	/* cfghelp: TW2:WarpCount, arena, int, def: 0, mod: hs_tw2
	 * The number of transwarps to load from the config file */
	ad->warpCount = cfg->GetInt(a->cfg, "TW2", "WarpCount", 0);

	//init the array
	ad->warpList = amalloc(sizeof(Transwarp) * ad->warpCount);

	for (i = 0; i < ad->warpCount; i++)
	{
		/* cfghelp: TW2:Warp0x1, arena, int, def: 0, mod: hs_tw2
		 * Top left corner of warp */
		sprintf(buf, "Warp%dx1", i);
		ad->warpList[i].x1 = cfg->GetInt(a->cfg, "TW2", buf, 1);
		/* cfghelp: TW2:Warp0y1, arena, int, def: 0, mod: hs_tw2
		 * Top left corner of warp */
		sprintf(buf, "Warp%dy1", i);
		ad->warpList[i].y1 = cfg->GetInt(a->cfg, "TW2", buf, 1);
		/* cfghelp: TW2:Warp0x2, arena, int, def: 0, mod: hs_tw2
		 * Bottom right corner of warp */
		sprintf(buf, "Warp%dx2", i);
		ad->warpList[i].x2 = cfg->GetInt(a->cfg, "TW2", buf, 2);
		/* cfghelp: TW2:Warp0y2, arena, int, def: 0, mod: hs_tw2
		 * Bottom right corner of warp */
		sprintf(buf, "Warp%dy2", i);
		ad->warpList[i].y2 = cfg->GetInt(a->cfg, "TW2", buf, 2);
		/* cfghelp: TW2:Warp0DestRegion, arena, int, def: 0, mod: hs_tw2
		 * The map region to warp the player into */
		sprintf(buf, "Warp%dDestRegion", i);
		ad->warpList[i].regionName = cfg->GetStr(a->cfg, "TW2", buf);
		/* cfghelp: TW2:Warp0Level, arena, int, def: 0, mod: hs_tw2
		 * Level of 'transwarp' needed to use */
		sprintf(buf, "Warp%dLevel", i);
		ad->warpList[i].level = cfg->GetInt(a->cfg, "TW2", buf, 0);
		/* cfghelp: TW2:Warp0EventCount, arena, int, def: level, mod: hs_tw2
		 * Number of 'transwarp' events generated */
		sprintf(buf, "Warp%dEventCount", i);
		ad->warpList[i].eventCount = cfg->GetInt(a->cfg, "TW2", buf, ad->warpList[i].level);
		/* cfghelp: TW2:Warp0NoFlags, arena, int, def: 1, mod: hs_tw2
		 * If flags are allowed in this warp */
		sprintf(buf, "Warp%dNoFlags", i);
		ad->warpList[i].noFlags = cfg->GetInt(a->cfg, "TW2", buf, 1);
	}

	for (i = 0; i < ad->warpCount; i++)
	{
		const char *regionName = ad->warpList[i].regionName;
		if (regionName != NULL)
		{
			Region *rgn = mapdata->FindRegionByName(a, regionName);
			ad->warpList[i].dest = rgn; /* could be null */

			if (rgn == NULL)
				lm->LogA(L_WARN, "hs_tw2", a, "could not find region %s", regionName);
		}
	}
}

local void unloadArenaConfig(Arena *a)
{
	struct adata *ad = P_ARENA_DATA(a, adkey);

	LLEmpty(&ad->allowTWRegion);
	LLEmpty(&ad->blockTWRegion);
	afree(ad->warpList);
}

local void playeraction(Player *p, int action, Arena *a)
{
	pdata *data = PPDATA(p, pdkey);
	if (action == PA_ENTERARENA)
	{
		data->attemptingWarp = 0;
		data->nextWarp = 0;
		data->destX = 0;
		data->destY = 0;
		data->destRadius = 0;
	}
	else if (action == PA_LEAVEARENA || action == PA_DISCONNECT)
	{
		ml->ClearTimer(graphicsTimer, p);
		data->destX = 0;
		data->destY = 0;
		data->destRadius = 0;
		data->attemptingWarp = 0;
	}
}
local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	pdata *data = PPDATA(p, pdkey);
	ml->ClearTimer(graphicsTimer, p);
	data->destX = 0;
	data->destY = 0;
	data->destRadius = 0;
	data->attemptingWarp = 0;
	data->nextWarp = current_ticks() + cfg->GetInt(p->arena->cfg, "Kill", "EnterDelay", 0) + 100;
}
local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	pdata *data = PPDATA(killed, pdkey);
	ml->ClearTimer(graphicsTimer, killed);
	data->destX = 0;
	data->destY = 0;
	data->destRadius = 0;
	data->attemptingWarp = 0;
}

EXPORT const char info_hs_tw2[] = "v6.2 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_tw2(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		kill = mm->GetInterface(I_KILL, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
		objects = mm->GetInterface(I_OBJECTS, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!lm || !net || !aman || !cfg || !pd || !items || !fc || !selfpos || !kill || !mapdata || !cmdman || !objects || !chat || !balls || !ml) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);
		mm->RegCallback(CB_PLAYERACTION, playeraction, arena);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_PLAYERACTION, playeraction, arena);

		ml->ClearTimer(graphicsTimer, 0);

		net->RemovePacket(C2S_POSITION, Pppk);
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(fc);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(kill);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(cmdman);
		mm->ReleaseInterface(objects);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(ml);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 1;

		ad->target.type = T_ARENA;
		ad->target.u.arena = arena;

		ad->killer = kill->LoadKiller(KILLER_NAME, arena, 0, 9999);

		loadArenaConfig(arena);

		cmdman->AddCommand("tw", Ctw, arena, tw_help);

		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->RegCallback(CB_KILL, playerkill, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_KILL, playerkill, arena);

		cmdman->RemoveCommand("tw", Ctw, arena);

		ad->on = 0;

		kill->UnloadKiller(ad->killer);

		unloadArenaConfig(arena);

		return MM_OK;
	}
	return MM_FAIL;
}
