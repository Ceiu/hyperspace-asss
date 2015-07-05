#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "asss.h"
#include "objects.h"
#include "hscore.h"
#include "kill.h"
#include "selfpos.h"
#include "hscore_spawner.h"
#include "balls.h"

#define KILLER_NAME "<engine failure>"
#define REGION_DELAY 100
#define ID_EXIT_ANGLE

typedef struct adata
{
	int on;

	Killer *killer;

	int last_id;

	Region *rgn_id_tunnel;            // ID tunnel thing
	Region *rgn_id_tunnel_out;        // ID tunnel exit thing
	//Region *rgn_warp_out;       	  // Region where ID allowed
	LinkedList allowIDRegion;
	LinkedList blockIDRegion;

	int dest_x;
	int dest_y;
	int v_x;
	int v_y;
	int rot;

	int imageBase;
	int imageCount;

	int enterImageID;
	int enterImageSizeX;
	int enterImageSizeY;
	int exitImageID;
	int exitImageSizeX;
	int exitImageSizeY;

	int tunnelTime;

	int tunnelAngle;                // Angle(ss) the user should be facing when entering/exiting id

	Target target;

	int cfg_AllowPriv;
} adata;

typedef struct pdata
{
	int energy;

	int in_id;

	int image_id;
	int dest_x;
	int dest_y;
	int v_x;
	int v_y;
	int rot;

	ticks_t last_warp;
	ticks_t next_warp;
	ticks_t imageReachTime;

	short destX;
	short destY;
	short destRadius;
	Region *destSector;
	int attemptingWarp : 1;
	int padding : 7;
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
local Iflagcore *fc;
local Iselfpos *selfpos;
local Iballs *balls;

local int adkey;
local int pdkey;

local pthread_mutex_t id_mutex = PTHREAD_MUTEX_INITIALIZER;

#define LOCK pthread_mutex_lock(&id_mutex);
#define UNLOCK pthread_mutex_unlock(&id_mutex);
local int graphicsTimer(void *_p);
local void tryWarp(Player *p);

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

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	//make sure the killer can't reactivate id for a bit
	pdata *data = PPDATA(killed, pdkey);
	int delay = cfg->GetInt(arena->cfg, "Kill", "EnterDelay", 0);
	data->next_warp = current_ticks() + delay + 100;
	data->in_id = 0;
	data->attemptingWarp = 0;
	data->imageReachTime = 0;
	data->destSector = 0;
	data->destX = 0;
	data->destY = 0;
	data->destRadius = 0;
	ml->ClearTimer(graphicsTimer, killed);
}

local void Destroy(Player *p, int blast, int onlyTeam)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);

	kill->Kill(p, ad->killer, blast, onlyTeam);

	data->in_id = 0;
}

local int timer_callback(void *clos)
{
	Player *p = clos;
	pdata *data = PPDATA(p, pdkey);

	if (data->in_id)
	{
		Destroy(p, 0, 0);
	}

	return FALSE;
}

local void WarpPlayerIntoID(Player *p, int src_x, int src_y, int dest_x, int dest_y, int v_x, int v_y, int rot)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);

	data->dest_x = dest_x;
	data->dest_y = dest_y;
	data->v_x = v_x;
	data->v_y = v_y;
	data->rot = rot;
	data->in_id = 1;
	data->last_warp = current_ticks();



	selfpos->WarpPlayer(p, ad->dest_x, ad->dest_y, ad->v_x, ad->v_y, ad->tunnelAngle, 0);

	ml->ClearTimer(timer_callback, p);
	ml->SetTimer(timer_callback, ad->tunnelTime, ad->tunnelTime, p, p);
}

local void WarpPlayerOutOfID(Player *p)
{
	adata *ad = P_ARENA_DATA(p->arena, adkey);
	pdata *data = PPDATA(p, pdkey);

	data->in_id = 0;
	data->last_warp = current_ticks();

	// Pick a random angle to be moving upon exit...
	int intAngle = prng->Number(0, 360);
	int intRotOut = intAngle / 9;

	double dTheta = (double) intAngle * M_PI / (double) 180;
	double dTrajX = cos(dTheta);
	double dTrajY = sin(dTheta);

	int intVelX = (int) (dTrajX * p->position.xspeed);
	int intVelY = (int) (dTrajY * p->position.xspeed) * -1;

	obj->Move(&(ad->target), data->image_id, (data->dest_x << 4) - (ad->exitImageSizeX/2), (data->dest_y << 4) - (ad->exitImageSizeY/2), 0, 0);
	obj->Image(&(ad->target), data->image_id, ad->exitImageID);
	obj->Toggle(&(ad->target), data->image_id, 1);

	selfpos->WarpPlayer(p, data->dest_x << 4, data->dest_y << 4, intVelX, intVelY, (70 - intRotOut) % 40, 0);
}

local int getFailure(Player *p)
{
	pdata *data = PPDATA(p, pdkey);
	local Ihscorespawner *spawner;
	int energyPercent;
	int maxtofail = items->getPropertySum(p, p->p_ship, "idmaxtofail", 0);

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

local int graphicsTimer(void *_p)
{
	Player *p = (Player *)_p;
	Arena *a = p->arena;
	struct adata *ad = P_ARENA_DATA(a, adkey);
	pdata *data = PPDATA(p, pdkey);
	int dx, dy, timeAway;

	if (!data->attemptingWarp)
	{
		return 0;
	}

	LOCK;
	ad->last_id++;
	if (ad->last_id >= ad->imageBase + ad->imageCount)
		ad->last_id = ad->imageBase;

	data->image_id = ad->last_id;
	UNLOCK;

	timeAway = items->getPropertySum(p, p->p_ship, "idwarmup", 150) - items->getPropertySum(p, p->p_ship, "idimage", 50);
	if (timeAway < 0)
		timeAway = 0;

	dx = p->position.x + ((int)p->position.xspeed * timeAway) / 1000;
	dy = p->position.y + ((int)p->position.yspeed * timeAway) / 1000;

	data->destX = (short)dx;
	data->destY = (short)dy;
	data->destRadius = items->getPropertySum(p, p->p_ship, "idarea", 48);

	obj->Move(&(ad->target), data->image_id, data->destX - (ad->enterImageSizeX/2), data->destY - (ad->enterImageSizeY/2), 0, 0);
	obj->Image(&(ad->target), data->image_id, ad->enterImageID);
	obj->Toggle(&(ad->target), data->image_id, 1);

	return 0;
}

local void tryWarp(Player *p)
{
	pdata *data = PPDATA(p, pdkey);

	if (!data->attemptingWarp)
	{
		return;
	}
	data->attemptingWarp = 0;

	if (game->IsAntiwarped(p, NULL))
	{
		chat->SendMessage(p, "You are antiwarped!");
		return;
	}
	else if (p->position.status & STATUS_SAFEZONE)
	{
		chat->SendMessage(p, "You cannot enter interdimensional space in a safe zone!");
		return;
	}
	else if(fc->CountPlayerFlags(p))
	{
		chat->SendMessage(p, "You cannot enter interdimensional space with flags!");
		return;
	}
	else if (isBallCarrier(p->arena, p))
	{
		chat->SendMessage(p, "You cannot carry a powerball through dimensions.");
		return;
	}
	else
	{
		int x = -1;
		int y = -1;
		int v_x = 0;
		int v_y = 0;
		int rot = 0;

		if (data->destSector)
			mapdata->FindRandomPoint(data->destSector, &x, &y);

		if (x == -1 || y == -1)
		{
			chat->SendMessage(p, "Unknown ID failure!");
			return;
		}

		//check for engine failure!
		if (!getFailure(p))
		{
			WarpPlayerIntoID(p, p->position.x >> 4, p->position.y >> 4, x, y, v_x, v_y, rot);
			items->triggerEvent(p, p->p_ship, "id");
			data->next_warp = current_ticks() + items->getPropertySum(p, p->p_ship, "idcool", 0);
		}
		else
		{
			//kill 'em
			int blast = items->getPropertySum(p, p->p_ship, "idblast", 0);
			int onlyTeam = items->getPropertySum(p, p->p_ship, "idonlytk", 0);
			chat->SendMessage(p, "ID engine failure! Your atoms are scattered across the known universe!");
			Destroy(p, blast, onlyTeam);
		}
	}
}


local helptext_t id_help =
"Targets: none\n"
"Args: <destination sector>\n"
"Activates the interdimensonal drives on your ship. "
"Interdimensional space is used to travel directly from one sector to another.\n"
"Only one player is warped.";

local void Cid(const char *command, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	pdata *data = PPDATA(p, pdkey);
	adata *ad = P_ARENA_DATA(arena, adkey);
	Link *l;
	Region *checkRegion;
	int allow = 0;

	int dest_sector;

	/* handle common errors */
	if (!arena || !ad->on)
		return;

	if (p->p_ship == SHIP_SPEC)
		return;

	if(!ad->cfg_AllowPriv && p->p_freq >= 100)
	{
		chat->SendMessage(p, "Private Frequencies are not allowed to leave center.");
		return;
	}

	if (data->attemptingWarp)
	{
		chat->SendMessage(p, "You are already using ID!");
		return;
	}

	if (current_ticks() < data->next_warp) //they recently were warped!
	{
		chat->SendMessage(p, "Your ID engines have not recharged!");
		return;
	}

	if (game->IsAntiwarped(p, NULL))
	{
		chat->SendMessage(p, "You are antiwarped!");
		return;
	}

	if (!items->getPropertySum(p, p->p_ship, "id", 0))
	{
		chat->SendMessage(p, "You do not have an item capable of entering interdimensional space.");
		return;
	}

	//parse their destination
	if (*params == 'c' || *params == 'C')
	{
		dest_sector = 0;
	}
	else
	{
		dest_sector = *params - '0';
		if (dest_sector < 0 || 8 < dest_sector)
		{
			chat->SendMessage(p, "Unknown destination. Use 1-8 or C");
			return;
		}
	}

	// Make sure the player isn't in a warp-disabled region or in hyperspace
    /*if (!mapdata->Contains(ad->rgn_warp_out, p->position.x >> 4, p->position.y >> 4))
	{
		//can't warp
		chat->SendMessage(p, "You cannot warp from your current location. Try moving into open space.");
		return;
	}*/

	//see if the user is allowed to use id here according to Allowed regions
	FOR_EACH(&ad->allowIDRegion, checkRegion, l)
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
		FOR_EACH(&ad->blockIDRegion, checkRegion, l)
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
		chat->SendMessage(p, "Unable to open an interdimensional portal from this location. Try moving into open space.");
		return;
	}
	else
	{
		Region *out;
		char regionName[100];
		sprintf(regionName, "sector%d_in", dest_sector);
		out = mapdata->FindRegionByName(arena, regionName);
		int imagetime = items->getPropertySum(p, p->p_ship, "idimage", 50);
		int warmup = items->getPropertySum(p, p->p_ship, "idwarmup", 150);

		if (out != NULL)
		{
			// Get random point in the region...
			data->destSector = out;
		}
		else
		{
			chat->SendMessage(p, "Unknown destination sector.");
			return;
		}

		ml->SetTimer(graphicsTimer, imagetime, imagetime, p, p);
		data->attemptingWarp = 1;
		data->destX = 0;
		data->destY = 0;
		data->destRadius = 0;
		data->imageReachTime = current_ticks() + warmup;
	}
}

local void Pppk(Player *p, byte *p2, int len)
{
	struct C2SPosition *pos = (struct C2SPosition *)p2;
	Arena *arena = p->arena;
	adata *ad = P_ARENA_DATA(arena, adkey);
	pdata *data = PPDATA(p, pdkey);

	if (len < 22)
		return;

	/* handle common errors */
	if (!arena || !ad->on) return;

	if (p->p_ship == SHIP_SPEC)
		return;

	data->energy = pos->energy;

	if (data->attemptingWarp)
	{
		if (pos->weapon.type != W_NULL)
		{
			chat->SendMessage(p, "Firing weapons and items prevents the interdimensional drive from working!");
			data->attemptingWarp = 0;
			return;
		}
		else if (data->destRadius)
		{
			if (current_ticks() > data->imageReachTime + 10)
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


	if (pos->status & STATUS_SAFEZONE)
		return;

	if (current_ticks() - data->last_warp < 100) //they recently were warped!
		return;

	//see if they're in the id tunnel
	if (mapdata->Contains(ad->rgn_id_tunnel, pos->x >> 4, pos->y >> 4))
	{
		//see if they're supposed to be
		if (data->in_id)
		{
			//see if they need to be warped out
			if (mapdata->Contains(ad->rgn_id_tunnel_out, pos->x >> 4, pos->y >> 4))
			{
				WarpPlayerOutOfID(p);
			}
		}
		else
		{
			//kill 'em
			Destroy(p, 0, 0);
		}
	}
	else
	{
		//see if they're supposed to be
		if (data->in_id)
		{
			//kill 'em
			Destroy(p, 0, 0);
		}
	}
}

local void playerActionCallback(Player *p, int action, Arena *arena)
{
	pdata *pdata = PPDATA(p, pdkey);
	if (action == PA_ENTERARENA)
	{
		pdata->in_id = 0;
		pdata->attemptingWarp = 0;
		pdata->imageReachTime = 0;
		pdata->destSector = 0;
		pdata->destX = 0;
		pdata->destY = 0;
		pdata->destRadius = 0;
	}
	if (action == PA_LEAVEARENA)
	{
		ml->ClearTimer(timer_callback, p);
		ml->ClearTimer(graphicsTimer, p);
		pdata->in_id = 0;
		pdata->attemptingWarp = 0;
		pdata->imageReachTime = 0;
		pdata->destSector = 0;
		pdata->destX = 0;
		pdata->destY = 0;
		pdata->destRadius = 0;
	}
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	pdata *data = PPDATA(p, pdkey);
	ml->ClearTimer(graphicsTimer, p);
	data->in_id = 0;
	data->attemptingWarp = 0;
	data->imageReachTime = 0;
	data->destSector = 0;
	data->destX = 0;
	data->destY = 0;
	data->destRadius = 0;
}

local int load_regions(void *clos)
{
	Arena *arena = clos;
	struct adata *ad = P_ARENA_DATA(arena, adkey);
	const char *attr;
	const char *tmp = 0;
	char buf[256];

	ad->rgn_id_tunnel = mapdata->FindRegionByName(arena, "interdimensional");
	ad->rgn_id_tunnel_out = mapdata->FindRegionByName(arena, "id_tunnel_out");
	//ad->rgn_warp_out = mapdata->FindRegionByName(arena, "warpout");

	if(!ad->rgn_id_tunnel || !ad->rgn_id_tunnel_out/* || !ad->rgn_warp_out*/)
		return TRUE; // Uh oh...

	LLInit(&ad->allowIDRegion);
	attr = mapdata->GetAttr(arena, "idallowlist");
	if (attr)
	{
		while (strsplit(attr, ",;", buf, sizeof(buf), &tmp))
		{
			Region *rgn = mapdata->FindRegionByName(arena, buf);
			if (!rgn)
				lm->LogA(L_WARN, "hs_id", arena, "could not find requested allow region %s", buf);
			else
				LLAdd(&ad->allowIDRegion, rgn);
		}
	}
	else
	{
		if (cfg->GetInt(arena->cfg, "Interdimensional", "UseWarpOutRegion", 1))
		{
			Region *rgn = mapdata->FindRegionByName(arena, "warpout");
			if (!rgn)
				lm->LogA(L_WARN, "hs_id", arena, "could not find requested allow region warpout");
			else
				LLAdd(&ad->allowIDRegion, rgn);
		}
	}

	tmp = 0;

	LLInit(&ad->blockIDRegion);
	attr = mapdata->GetAttr(arena, "idblocklist");
	if (attr)
	{
		while (strsplit(attr, ",;", buf, sizeof(buf), &tmp))
		{
			Region *rgn = mapdata->FindRegionByName(arena, buf);
			if (!rgn)
				lm->LogA(L_WARN, "hs_id", arena, "could not find requested block region %s", buf);
			else
				LLAdd(&ad->blockIDRegion, rgn);
		}
	}

	ad->killer = kill->LoadKiller(KILLER_NAME, arena, 0, 9999);
	ad->on = 1;

	return FALSE;
}

local void unload_regions(Arena *arena)
{
	struct adata *ad = P_ARENA_DATA(arena, adkey);

	LLEmpty(&ad->allowIDRegion);
	LLEmpty(&ad->blockIDRegion);
}

local void loadArenaConfig(Arena *a)
{
	struct adata *ad = P_ARENA_DATA(a, adkey);

	/* cfghelp: Interdimensional:EntranceX, arena, int, def: 931, mod: hs_id
	 * X coordinate for the entrance to the ID tunnel */
	ad->dest_x = cfg->GetInt(a->cfg, "Interdimensional", "EntranceX", 931) << 4;
	/* cfghelp: Interdimensional:EntranceY, arena, int, def: 123, mod: hs_id
	 * Y coordinate for the entrance to the ID tunnel */
	ad->dest_y = cfg->GetInt(a->cfg, "Interdimensional", "EntranceY", 123) << 4;
	/* cfghelp: Interdimensional:XVelocity, arena, int, def: -30000, mod: hs_id
	 * X velocity that the player will enter the tunnel with */
	ad->v_x = cfg->GetInt(a->cfg, "Interdimensional", "XVelocity", -30000);
	/* cfghelp: Interdimensional:YVelocity, arena, int, def: 0, mod: hs_id
	 * Y velocity that the player will enter the tunnel with*/
	ad->v_y = cfg->GetInt(a->cfg, "Interdimensional", "YVelocity", 0);
	/* cfghelp: Interdimensional:Rotation, arena, int, def: 30, mod: hs_id
	 * Rotation that the player will enter the tunnel with*/
	ad->v_y = cfg->GetInt(a->cfg, "Interdimensional", "Rotation", 30);

	/* cfghelp: Interdimensional:ImageIDBase, arena, int, def: 950, mod: hs_id
	 * The object id to start counting from */
	ad->imageBase = cfg->GetInt(a->cfg, "Interdimensional", "ImageIDBase", 950);
	/* cfghelp: Interdimensional:ImageIDCount, arena, int, def: 20, mod: hs_id
	 * Number of object ids including ImageIDBase */
	ad->imageCount = cfg->GetInt(a->cfg, "Interdimensional", "ImageIDCount", 20);


	/* cfghelp: Interdimensional:EnterImageID, arena, int, def: 1, mod: hs_id
	 * Enter image object id */
	ad->enterImageID = cfg->GetInt(a->cfg, "Interdimensional", "EnterImageID", 1);
	/* cfghelp: Interdimensional:EnterImageSizeX, arena, int, def: 360, mod: hs_id
	 * Height of enter image */
	ad->enterImageSizeX = cfg->GetInt(a->cfg, "Interdimensional", "EnterImageSizeX", 300);
	/* cfghelp: Interdimensional:EnterImageSizeY, arena, int, def: 360, mod: hs_id
	 * Width of enter image */
	ad->enterImageSizeY = cfg->GetInt(a->cfg, "Interdimensional", "EnterImageSizeY", 300);
	/* cfghelp: Interdimensional:ExitImageID, arena, int, def: 2, mod: hs_id
	 * Exit image object id */
	ad->exitImageID = cfg->GetInt(a->cfg, "Interdimensional", "ExitImageID", 2);
	/* cfghelp: Interdimensional:ExitImageSizeX, arena, int, def: 360, mod: hs_id
	 * Height of exit image */
	ad->exitImageSizeX = cfg->GetInt(a->cfg, "Interdimensional", "ExitImageSizeX", 300);
	/* cfghelp: Interdimensional:ExitImageSizeY, arena, int, def: 360, mod: hs_id
	 * Width of exit image */
	ad->exitImageSizeY = cfg->GetInt(a->cfg, "Interdimensional", "ExitImageSizeY", 300);


	/* cfghelp: Interdimensional:TunnelTime, arena, int, def: 1500, mod: hs_id
	 * Time to allow a player to remain in the tunnel before engine failure */
	ad->tunnelTime = cfg->GetInt(a->cfg, "Interdimensional", "TunnelTime", 1500);

	/* cfghelp: Interdimensional:TunnelAngle, arena, int, def: 30, mod: hs_id
	 * The rotation angle in which the player will enter (and exit) the tunnel */
    ad->tunnelAngle = cfg->GetInt(a->cfg, "Interdimensional", "TunnelAngle", 30);

	ad->cfg_AllowPriv = cfg->GetInt(a->cfg, "Hyperspace", "PrivFTL", 1);
}

EXPORT const char info_hs_id[] = "v1.3 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_id(int action, Imodman *_mm, Arena *arena)
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
		fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);

		if (!lm || !net || !mapdata || !chat || !aman || !game || !cmdman || !cfg || !ml || !pd || !obj || !items || !kill || !prng || !fc || !selfpos || !balls) return MM_FAIL;

		adkey = aman->AllocateArenaData(sizeof(adata));
		if (adkey == -1) return MM_FAIL;

		pdkey = pd->AllocatePlayerData(sizeof(pdata));
		if (pdkey == -1) return MM_FAIL;

		net->AddPacket(C2S_POSITION, Pppk);

		mm->RegCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_POSITION, Pppk);
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);
		mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);

		// clear all timers
		ml->ClearTimer(timer_callback, NULL);
		ml->ClearTimer(load_regions, NULL);
		ml->ClearTimer(graphicsTimer, 0);

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
		mm->ReleaseInterface(fc);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(balls);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ad->on = 0;

		mm->RegCallback(CB_KILL, killCallback, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		ml->SetTimer(load_regions, REGION_DELAY, REGION_DELAY, arena, arena);


		ad->target.type = T_ARENA;
		ad->target.u.arena = arena;

		loadArenaConfig(arena);
		ad->last_id = ad->imageBase;

		cmdman->AddCommand("id", Cid, arena, id_help);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		struct adata *ad = P_ARENA_DATA(arena, adkey);

		ml->ClearTimer(load_regions, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
		mm->UnregCallback(CB_KILL, killCallback, arena);


		cmdman->RemoveCommand("id", Cid, arena);

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

