#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "asss.h"
#include "objects.h"
#include "hscore.h"
#include "kill.h"
#include "selfpos.h"
#include "hscore_spawner.h"

#define KILLER_NAME "<engine failure>"

#define REGION_DELAY 100

typedef enum jump_point_state
{POWER_UP, ACTIVE, POWER_DOWN} jump_point_state;

typedef struct JumpPoint
{
	int source_x;
	int source_y;
	int dest_x;
	int dest_y;
	int radius;

	jump_point_state state;

	ticks_t last_change;

	int source_image_id;
	int dest_image_id;
} JumpPoint;

typedef struct adata
{
	int on;

	Killer *killer;

	int last_id;

	Target target;

	//Region *rgn_warp_out; // Region where id/jump is allowed.
	LinkedList allowJumpRegion;
	LinkedList blockJumpRegion;

	LinkedList jump_point_list;

	int powerUpTime;
	int activeTime;
	int powerDownTime;

	int imageBase;
	int imageCount;
	int imageHeight;
	int imageWidth;

	int radius;

	int sourcePowerUpID;
	int sourceActiveID;
	int sourcePowerDownID;
	int destPowerUpID;
	int destActiveID;
	int destPowerDownID;

	int cfg_AllowPriv;
} adata;

typedef struct pdata
{
	ticks_t next_warp;
	int energy;
} pdata;

//modules
local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Imapdata *mapdata;
local Ichat *chat;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmdman;
local Iconfig *cfg;
local Imainloop *ml;
local Iplayerdata *pd;
local Iobjects *obj;
local Ihscoreitems *items;
local Ikill *kill;
local Iprng *prng;
local Iselfpos *selfpos;

local int adkey;
local int pdkey;

local pthread_mutex_t point_mutex = PTHREAD_MUTEX_INITIALIZER;

#define LOCK pthread_mutex_lock(&point_mutex);
#define UNLOCK pthread_mutex_unlock(&point_mutex);

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	//make sure the killer can't reactivate jump engines for a bit
	pdata *data = PPDATA(killed, pdkey);
	int delay = cfg->GetInt(arena->cfg, "Kill", "EnterDelay", 0);
	data->next_warp = current_ticks() + delay + 100;
}

local void PowerUpPoint(Arena *arena, JumpPoint *point)
{
	adata *ad = P_ARENA_DATA(arena, adkey);

	point->state = POWER_UP;
	point->last_change = current_ticks();

	obj->Move(&(ad->target), point->dest_image_id, (point->dest_x  << 4) - (ad->imageWidth / 2), (point->dest_y  << 4) - (ad->imageHeight / 2), 0, 0);
	obj->Move(&(ad->target), point->source_image_id, (point->source_x  << 4) - (ad->imageWidth / 2), (point->source_y  << 4) - (ad->imageHeight / 2), 0, 0);

	obj->Image(&(ad->target), point->dest_image_id, ad->destPowerUpID);
	obj->Image(&(ad->target), point->source_image_id, ad->sourcePowerUpID);
	obj->Toggle(&(ad->target), point->dest_image_id, 1);
	obj->Toggle(&(ad->target), point->source_image_id, 1);
}

local void ActivatePoint(Arena *arena, JumpPoint *point)
{
	adata *ad = P_ARENA_DATA(arena, adkey);

	point->state = ACTIVE;
	point->last_change = current_ticks();

	obj->Image(&(ad->target), point->dest_image_id, ad->destActiveID);
	obj->Image(&(ad->target), point->source_image_id, ad->sourceActiveID);
	obj->Toggle(&(ad->target), point->dest_image_id, 1);
	obj->Toggle(&(ad->target), point->source_image_id, 1);
}

local void PowerDownPoint(Arena *arena, JumpPoint *point)
{
	adata *ad = P_ARENA_DATA(arena, adkey);

	point->state = POWER_DOWN;
	point->last_change = current_ticks();

	obj->Image(&(ad->target), point->dest_image_id, ad->destPowerDownID);
	obj->Image(&(ad->target), point->source_image_id, ad->sourcePowerDownID);
	obj->Toggle(&(ad->target), point->dest_image_id, 1);
	obj->Toggle(&(ad->target), point->source_image_id, 1);
}

local void RemovePoint(Arena *arena, JumpPoint *point)
{
	adata *ad = P_ARENA_DATA(arena, adkey);
	obj->Image(&(ad->target), point->dest_image_id, 0);
	obj->Image(&(ad->target), point->source_image_id, 0);
}

local int timer_callback(void *clos)
{
	Arena *a = clos;
	adata *ad = P_ARENA_DATA(a, adkey);
	LinkedList remove_list;
	Link *link;

	LLInit(&remove_list);

	LOCK
	for (link = LLGetHead(&(ad->jump_point_list)); link; link = link->next)
	{
		JumpPoint *point = link->data;

		ticks_t diff = current_ticks() - point->last_change;

		switch (point->state)
		{
			case POWER_UP:
				if (diff >= ad->powerUpTime)
					ActivatePoint(a, point);
				break;
			case ACTIVE:
				if (diff >= ad->activeTime)
					PowerDownPoint(a, point);
				break;
			case POWER_DOWN:
				if (diff >= ad->powerDownTime)
				{
					RemovePoint(a, point);
					LLAdd(&remove_list, point);
				}
				break;
		}
	}

	//now remove the dead points
	for (link = LLGetHead(&remove_list); link; link = link->next)
	{
		JumpPoint *point = link->data;
		LLRemove(&(ad->jump_point_list), point);
	}
	LLEnum(&remove_list, afree);
	LLEmpty(&remove_list);

	UNLOCK

	return TRUE;
}

void CreateJumpPoint(Player *p, int src_x, int src_y, int dest_x, int dest_y)
{
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);

	JumpPoint *point;

	point = amalloc(sizeof(*point));

	point->source_x = src_x;
	point->source_y = src_y;
	point->dest_x = dest_x;
	point->dest_y = dest_y;
	point->radius = ad->radius;
	point->state = POWER_UP;
	point->last_change = current_ticks();

	LOCK
	ad->last_id++;
	if (ad->last_id >= ad->imageBase + ad->imageCount)
		ad->last_id = ad->imageBase;

	point->source_image_id = ad->last_id;

	ad->last_id++;
	if (ad->last_id >= ad->imageBase + ad->imageCount)
		ad->last_id = ad->imageBase;

	point->dest_image_id = ad->last_id;

	LLAdd(&(ad->jump_point_list), point);
	PowerUpPoint(arena, point);
	UNLOCK
}

local Region * getDestRegion(Player *p)
{
	LinkedList src_regions;
	Link *link;
	void (*lladdpointer)(void *, Region *) = ((void (*)(void *, Region *)) LLAdd); //cast to avoid compiler warnings
	Region *src_rgn;
	char dest_name[100];
	int found = 0;

	LLInit(&src_regions);

	//find the player's current sector
	mapdata->EnumContaining(p->arena, p->position.x >> 4, p->position.y >> 4, lladdpointer, &src_regions);
	for (link = LLGetHead(&src_regions); link; link = link->next)
	{
		Region *region = link->data;
		const char *name = mapdata->RegionName(region);

		if (strncmp("sector", name, 6) == 0)
		{
			int len = strlen(name);

			// check for sectorX
			if (len == 7)
			{
				src_rgn = region;
				sprintf(dest_name, "sector%c_hs_in", name[6]);
				found = 1;
				break;
			}

			// check for sectorX_hs
			if (len == 10)
			{
				if (strncmp("_hs", name + 7, 3) == 0)
				{
					src_rgn = region;
					sprintf(dest_name, "sector%c_in", name[6]);
					found = 1;
					break;
				}
			}
		}
	}
	LLEmpty(&src_regions);

	if (found)
	{
		return mapdata->FindRegionByName(p->arena, dest_name);
	}
	else
	{
		return NULL;
	}
}


local int getFailure(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	local Ihscorespawner *spawner;
	int energyPercent;

	int maxtofail = items->getPropertySum(p, p->p_ship, "jumpmaxtofail", 0);

	spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
	if (spawner)
	{
		energyPercent = (data->energy * 100) / spawner->getFullEnergy(p);
	}
	else
	{
		return 0;
	}
	mm->ReleaseInterface(spawner);

	if (maxtofail < energyPercent)
	{
		return 0;
	}
	else
	{
		int newEnergy = energyPercent * 100 / maxtofail;
		int rand = prng->Number(1, 10000);
		return (rand > (newEnergy * newEnergy));
	}
}

local helptext_t jump_help =
"Targets: none\n"
"Args: none\n"
"Activates the jump engines on your ship and creates a jump point. "
"A jump point is a one way phenomenon that links hyperspace and normal space.\n"
"Both friendly and enemy players can use it.";

local void Cjump(const char *command, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	Region *dest_rgn = NULL;
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

	if (current_ticks() < data->next_warp)
	{
		chat->SendMessage(p, "Your jump engines have not recharged!");
		return;
	}

	if (game->IsAntiwarped(p, NULL))
	{
		chat->SendMessage(p, "You are antiwarped!");
		return;
	}

	if (!items->getPropertySum(p, p->p_ship, "jump", 0))
	{
		chat->SendMessage(p, "You do not have an item capable of creating a jump point.");
		return;
	}

	if (p->position.status & STATUS_SAFEZONE)
	{
		chat->SendMessage(p, "You cannot create a jump point inside a safe zone!");
		return;
	}

   /* if (!mapdata->Contains(ad->rgn_warp_out, p->position.x >> 4, p->position.y >> 4))
	{
		chat->SendMessage(p, "You cannot jump from your current location. Try moving into an open space.");
		return;
	}*/
	//see if the user is allowed to use jump here according to Allowed regions
	FOR_EACH(&ad->allowJumpRegion, checkRegion, l)
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
		FOR_EACH(&ad->blockJumpRegion, checkRegion, l)
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
		chat->SendMessage(p, "Unable to open a jump portal from this location. Try moving into open space.");
		return;
	}


	dest_rgn = getDestRegion(p);

	if (dest_rgn == NULL)
	{
		//can't find the sector to send them to
		chat->SendMessage(p, "Error locating destination!");
		return;
	}
	else
	{
		int x;
		int y;

		mapdata->FindRandomPoint(dest_rgn, &x, &y);

		if (x == -1 || y == -1)
		{
			//no dest
			chat->SendMessage(p, "Unknown destination sector!");
		}
		else
		{
			//check for engine failure!
			if (!getFailure(p))
			{
				//create jump point
				CreateJumpPoint(p, p->position.x >> 4, p->position.y >> 4, x, y);
				items->triggerEvent(p, p->p_ship, "jump");
				data->next_warp = current_ticks() + items->getPropertySum(p, p->p_ship, "jumpcool", 0);
			}
			else
			{
				int blast = items->getPropertySum(p, p->p_ship, "jumpblast", 0);
				int onlyTeam = items->getPropertySum(p, p->p_ship, "jumponlytk", 0);
				chat->SendMessage(p, "Jump engine failure! Your ship is destroyed by the unstable vortex!");
				kill->Kill(p, ad->killer, blast, onlyTeam);
			}
		}
	}
}

local void WarpPlayer(Player *p, int src_x, int src_y, JumpPoint *point)
{
	int dest_x = (src_x - (point->source_x << 4)) + (point->dest_x << 4);
	int dest_y = (src_y - (point->source_y << 4)) + (point->dest_y << 4);

	selfpos->WarpPlayer(p, dest_x, dest_y, p->position.xspeed, p->position.yspeed, p->position.rotation, 0);
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);
	Link *link;

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	data->energy = pos->energy;

	if (p->position.status & STATUS_SAFEZONE)
		return;

	LOCK
	for (link = LLGetHead(&(ad->jump_point_list)); link; link = link->next)
	{
		JumpPoint *point = link->data;

		if (point->state != ACTIVE)
			continue;

		if (point->source_x - point->radius < (pos->x>>4) && (pos->x>>4) < point->source_x + point->radius)
		{
			if (point->source_y - point->radius < (pos->y>>4) && (pos->y>>4) < point->source_y + point->radius)
			{
				//warp them
				WarpPlayer(p, pos->x, pos->y, point);
				break;
			}
		}
	}
	UNLOCK
}

local int load_regions(void *clos)
{
	Arena *arena = clos;
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	const char *attr;
	const char *tmp = 0;
	char buf[256];

	/*ad->rgn_warp_out = mapdata->FindRegionByName(arena, "warpout");

	if(!ad->rgn_warp_out)
		return TRUE; // oh noes!*/

	LLInit(&ad->allowJumpRegion);
	attr = mapdata->GetAttr(arena, "jumpallowlist");
	if (attr)
	{
		while (strsplit(attr, ",;", buf, sizeof(buf), &tmp))
		{
			Region *rgn = mapdata->FindRegionByName(arena, buf);
			if (!rgn)
				lm->LogA(L_WARN, "hs_jumpengines", arena, "could not find requested allow region %s", buf);
			else
				LLAdd(&ad->allowJumpRegion, rgn);
		}
	}
	else
	{
		if (cfg->GetInt(arena->cfg, "JumpPoint", "UseWarpOutRegion", 1))
		{
			Region *rgn = mapdata->FindRegionByName(arena, "warpout");
			if (!rgn)
				lm->LogA(L_WARN, "hs_jumpengines", arena, "could not find requested allow region warpout");
			else
				LLAdd(&ad->allowJumpRegion, rgn);
		}
	}

	tmp = 0;

	LLInit(&ad->blockJumpRegion);
	attr = mapdata->GetAttr(arena, "jumpblocklist");
	if (attr)
	{
		while (strsplit(attr, ",;", buf, sizeof(buf), &tmp))
		{
			Region *rgn = mapdata->FindRegionByName(arena, buf);
			if (!rgn)
				lm->LogA(L_WARN, "hs_jumpengines", arena, "could not find requested block region %s", buf);
			else
				LLAdd(&ad->blockJumpRegion, rgn);
		}
	}


	ad->on = 1;
	ad->killer = kill->LoadKiller(KILLER_NAME, arena, 0, 9999);

	return FALSE;
}

local void unload_regions(Arena *arena)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);

	LLEmpty(&ad->allowJumpRegion);
	LLEmpty(&ad->blockJumpRegion);
}

local void loadArenaConfig(Arena *a)
{
	struct adata *ad = P_ARENA_DATA(a, adkey);

	/* cfghelp: JumpPoint:PowerUpTime, arena, int, def: 200, mod: hs_id
	 * How long the power up cycle lasts */
	ad->powerUpTime = cfg->GetInt(a->cfg, "JumpPoint", "PowerUpTime", 200);
	/* cfghelp: JumpPoint:ActiveTime, arena, int, def: 1000, mod: hs_id
	 * How long the active cycle lasts */
	ad->activeTime = cfg->GetInt(a->cfg, "JumpPoint", "ActiveTime", 1000);
	/* cfghelp: JumpPoint:PowerDownTime, arena, int, def: 500, mod: hs_id
	 * How long the power down cycle lasts */
	ad->powerDownTime = cfg->GetInt(a->cfg, "JumpPoint", "PowerDownTime", 500);


	/* cfghelp: JumpPoint:ImageIDBase, arena, int, def: 900, mod: hs_id
	 * The object id to start counting from */
	ad->imageBase = cfg->GetInt(a->cfg, "JumpPoint", "ImageIDBase", 900);
	/* cfghelp: JumpPoint:ImageIDCount, arena, int, def: 20, mod: hs_id
	 * Number of object ids including ImageIDBase */
	ad->imageCount = cfg->GetInt(a->cfg, "JumpPoint", "ImageIDCount", 20);
	/* cfghelp: JumpPoint:ImageWidth, arena, int, def: 320, mod: hs_id
	 * Width of jump point image */
	ad->imageWidth = cfg->GetInt(a->cfg, "JumpPoint", "ImageWidth", 320);
	/* cfghelp: JumpPoint:ImageHeight, arena, int, def: 320, mod: hs_id
	 * Height of jump point image */
	ad->imageHeight = cfg->GetInt(a->cfg, "JumpPoint", "ImageHeight", 320);

	/* cfghelp: JumpPoint:Radius, arena, int, def: 10, mod: hs_id
	 * Radius in tiles of a jump point */
	ad->radius = cfg->GetInt(a->cfg, "JumpPoint", "Radius", 10);

	/* cfghelp: JumpPoint:SourcePowerUpID, arena, int, def: 1, mod: hs_id
	 * ID of the image displayed at the source while powering up */
	ad->sourcePowerUpID = cfg->GetInt(a->cfg, "JumpPoint", "SourcePowerUpID", 1);
	/* cfghelp: JumpPoint:SourceActiveID, arena, int, def: 2, mod: hs_id
	 * ID of the image displayed at the source while active */
	ad->sourceActiveID = cfg->GetInt(a->cfg, "JumpPoint", "SourceActiveID", 2);
	/* cfghelp: JumpPoint:SourcePowerDownID, arena, int, def: 3, mod: hs_id
	 * ID of the image displayed at the source while powering down */
	ad->sourcePowerDownID = cfg->GetInt(a->cfg, "JumpPoint", "SourcePowerDownID", 3);
	/* cfghelp: JumpPoint:DestPowerUpID, arena, int, def: 4, mod: hs_id
	 * ID of the image displayed at the destination while powering up */
	ad->destPowerUpID = cfg->GetInt(a->cfg, "JumpPoint", "DestPowerUpID", 4);
	/* cfghelp: JumpPoint:DestActiveID, arena, int, def: 5, mod: hs_id
	 * ID of the image displayed at the destination while active */
	ad->destActiveID = cfg->GetInt(a->cfg, "JumpPoint", "DestActiveID", 5);
	/* cfghelp: JumpPoint:DestPowerDownID, arena, int, def: 6, mod: hs_id
	 * ID of the image displayed at the destination while powering down */
	ad->destPowerDownID = cfg->GetInt(a->cfg, "JumpPoint", "DestPowerDownID", 6);

	ad->cfg_AllowPriv = cfg->GetInt(a->cfg, "Hyperspace", "PrivFTL", 1);
}

EXPORT const char info_hs_jumpengines[] = "v1.2 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_jumpengines(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		cmdman = mm->GetInterface(I_CMDMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		obj = mm->GetInterface(I_OBJECTS, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		kill = mm->GetInterface(I_KILL, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);

		if (!lm || !net || !mapdata || !chat || !aman || !game || !cmdman || !cfg || !ml || !pd || !obj || !items || !kill || !prng || !selfpos) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(cmdman);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(obj);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(kill);
		mm->ReleaseInterface(prng);
		mm->ReleaseInterface(selfpos);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ml->SetTimer(load_regions, REGION_DELAY, REGION_DELAY, arena, arena);
		mm->RegCallback(CB_KILL, killCallback, arena);

		//haven't loaded regions
		ad->on = 0;

		ad->target.type = T_ARENA;
		ad->target.u.arena = arena;

		LLInit(&ad->jump_point_list);

		loadArenaConfig(arena);
		ad->last_id = ad->imageBase;

		ml->SetTimer(timer_callback, 5, 5, arena, arena);

		cmdman->AddCommand("jump", Cjump, arena, jump_help);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);
		ml->ClearTimer(load_regions, arena);


		cmdman->RemoveCommand("jump", Cjump, arena);

		ml->ClearTimer(timer_callback, arena);
		mm->UnregCallback(CB_KILL, killCallback, arena);

		LLEnum(&ad->jump_point_list, afree);
		LLEmpty(&ad->jump_point_list);

		if (ad->on)
		{
			ad->on = 0;
			kill->UnloadKiller(ad->killer);
		}

		unload_regions(arena);

		return MM_OK;
	}
	return MM_FAIL;
}

