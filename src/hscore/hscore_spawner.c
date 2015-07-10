
#include <stdlib.h>
#include <string.h>

#include "asss.h"

#include "clientset.h"
#include "hscore.h"
#include "hscore_storeman.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "hscore_shipnames.h"
#include "fg_wz.h"
#include "selfpos.h"

typedef struct PrizeData
{
	Player *player;
	int prizeNumber;
	int count;
} PrizeData;

typedef struct CallbackData
{
	Player *player;
	ShipHull *hull;
	Item *item;
	int mult;
	int force;
} CallbackData;

typedef struct PlayerDataStruct
{
	short underOurControl;
	short dirty;
	short spawned;
	short usingPerShip[8];
	short reship_after_settings;
	int currentShip;
	ticks_t lastDeath;

	ticks_t lastSet;
	int oldBounty;

	LinkedList ignoredPrizes;
} PlayerDataStruct;

typedef struct IgnorePrizeStruct
{
	int prize;
	ticks_t timeout;
} IgnorePrizeStruct;

typedef struct ShipOverrideKeys
{
	override_key_t ShrapnelMax;
	override_key_t ShrapnelRate;
	override_key_t CloakStatus;
	override_key_t StealthStatus;
	override_key_t XRadarStatus;
	override_key_t AntiWarpStatus;
	override_key_t InitialGuns;
	override_key_t MaxGuns;
	override_key_t InitialBombs;
	override_key_t MaxBombs;
	override_key_t SeeMines;
	override_key_t SeeBombLevel;
	override_key_t Gravity;
	override_key_t GravityTopSpeed;
	override_key_t BulletFireEnergy;
	override_key_t MultiFireEnergy;
	override_key_t BombFireEnergy;
	override_key_t BombFireEnergyUpgrade;
	override_key_t LandmineFireEnergy;
	override_key_t LandmineFireEnergyUpgrade;
	override_key_t BulletSpeed;
	override_key_t BombSpeed;
	override_key_t CloakEnergy;
	override_key_t StealthEnergy;
	override_key_t AntiWarpEnergy;
	override_key_t XRadarEnergy;
	override_key_t MaximumRotation;
	override_key_t MaximumThrust;
	override_key_t MaximumSpeed;
	override_key_t MaximumRecharge;
	override_key_t MaximumEnergy;
	override_key_t InitialRotation;
	override_key_t InitialThrust;
	override_key_t InitialSpeed;
	override_key_t InitialRecharge;
	override_key_t InitialEnergy;
	override_key_t UpgradeRotation;
	override_key_t UpgradeThrust;
	override_key_t UpgradeSpeed;
	override_key_t UpgradeRecharge;
	override_key_t UpgradeEnergy;
	override_key_t AfterburnerEnergy;
	override_key_t BombThrust;
	override_key_t TurretThrustPenalty;
	override_key_t TurretSpeedPenalty;
	override_key_t BulletFireDelay;
	override_key_t MultiFireDelay;
	override_key_t BombFireDelay;
	override_key_t LandmineFireDelay;
	override_key_t RocketTime;
	override_key_t InitialBounty;
	override_key_t DamageFactor;
	override_key_t AttachBounty;
	override_key_t SoccerThrowTime;
	override_key_t SoccerBallProximity;
	override_key_t MaxMines;
	override_key_t RepelMax;
	override_key_t BurstMax;
	override_key_t DecoyMax;
	override_key_t ThorMax;
	override_key_t BrickMax;
	override_key_t RocketMax;
	override_key_t PortalMax;
	override_key_t InitialRepel;
	override_key_t InitialBurst;
	override_key_t InitialBrick;
	override_key_t InitialRocket;
	override_key_t InitialThor;
	override_key_t InitialDecoy;
	override_key_t InitialPortal;
	override_key_t DisableFastShooting;
} ShipOverrideKeys;

typedef struct GlobalOverrideKeys
{
	override_key_t BulletDamageLevel;
	override_key_t BulletDamageUpgrade;
	override_key_t BombDamageLevel;
	override_key_t EBombShutdownTime;
	override_key_t EBombDamagePercent;
	override_key_t BBombDamagePercent;
	override_key_t BurstDamageLevel;
	override_key_t DecoyAliveTime;
	override_key_t WarpPointDelay;
	override_key_t RocketThrust;
	override_key_t RocketSpeed;
	override_key_t InactiveShrapDamage;
	override_key_t ShrapnelDamagePercent;
	override_key_t MapZoomFactor;
	override_key_t FlaggerGunUpgrade;
	override_key_t FlaggerBombUpgrade;
	override_key_t AllowBombs;
	override_key_t AllowGuns;
	override_key_t UseFlagger;
	override_key_t BallLocation;
	override_key_t DoorMode;
	override_key_t JitterTime;
	override_key_t BombExplodePixels;
} GlobalOverrideKeys;

//modules
local Iplayerdata *pd;
local Inet *net;
local Igame *game;
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Iconfig *cfg;
local Ihscoreitems *items;
local Iclientset *clientset;
local Ihscoredatabase *database;
local Imainloop *ml;
local Iselfpos *selfpos;

local int playerDataKey;

local ShipOverrideKeys shipOverrideKeys[8];
local GlobalOverrideKeys globalOverrideKeys;

//interface function
//local void respawn(Player *p);

//prototypel
local void resendOverrides(Player *p);
local int doOverrideAdvisers(Player *p, int ship, int shipset, const char *prop, int init_value);
local int resetBountyTimerCallback(void *clos);

local inline int min(int a, int b)
{
	if (a < b)
	{
		return a;
	}
	else
	{
		return b;
	}
}

local inline int max(int a, int b)
{
	if (a > b)
	{
		return a;
	}
	else
	{
		return b;
	}
}

local void spawnPlayer(Player *p, int shipset)
{
	PlayerDataStruct *data = PPDATA(p, playerDataKey);
	Target t;
	int bounce, prox, multifire, shrapnel, energyViewing;
	data->spawned = 1;


	//do needed prizing
	t.type = T_PLAYER;
	t.u.p = p;

	bounce = items->getPropertySumOnShipSet(p, p->pkt.ship, shipset, "bounce", 0);
	bounce = doOverrideAdvisers(p, p->pkt.ship, shipset, "bounce", bounce);
	if (bounce > 0) game->GivePrize(&t, 10, bounce);

	prox = items->getPropertySumOnShipSet(p, p->pkt.ship, shipset, "prox", 0);
	prox = doOverrideAdvisers(p, p->pkt.ship, shipset, "prox", prox);
	if (prox > 0) game->GivePrize(&t, 16, prox);

	multifire = items->getPropertySumOnShipSet(p, p->pkt.ship, shipset, "multifire", 0);
	multifire = doOverrideAdvisers(p, p->pkt.ship, shipset, "multifire", multifire);
	if (multifire > 0) game->GivePrize(&t, 15, multifire);

	shrapnel = items->getPropertySumOnShipSet(p, p->pkt.ship, shipset, "shrapnel", 0);
	shrapnel = doOverrideAdvisers(p, p->pkt.ship, shipset, "shrapnel", shrapnel);
	if (shrapnel > 0) game->GivePrize(&t, 19, shrapnel);

	//set energy viewing
	energyViewing = items->getPropertySumOnShipSet(p, p->pkt.ship, shipset, "energyviewing", 0);
	energyViewing = doOverrideAdvisers(p, p->pkt.ship, shipset, "energyviewing", energyViewing);
	if (energyViewing > 0) game->SetPlayerEnergyViewing(p, ENERGY_SEE_ALL);
	else game->ResetPlayerEnergyViewing(p);
}

local void loadOverrides()
{
	int i;
	for (i = 0; i < 8; i++)
	{
		shipOverrideKeys[i].ShrapnelMax = clientset->GetOverrideKey(shipNames[i], "ShrapnelMax");
		shipOverrideKeys[i].ShrapnelRate = clientset->GetOverrideKey(shipNames[i], "ShrapnelRate");
		shipOverrideKeys[i].CloakStatus = clientset->GetOverrideKey(shipNames[i], "CloakStatus");
		shipOverrideKeys[i].StealthStatus = clientset->GetOverrideKey(shipNames[i], "StealthStatus");
		shipOverrideKeys[i].XRadarStatus = clientset->GetOverrideKey(shipNames[i], "XRadarStatus");
		shipOverrideKeys[i].AntiWarpStatus = clientset->GetOverrideKey(shipNames[i], "AntiWarpStatus");
		shipOverrideKeys[i].InitialGuns = clientset->GetOverrideKey(shipNames[i], "InitialGuns");
		shipOverrideKeys[i].MaxGuns = clientset->GetOverrideKey(shipNames[i], "MaxGuns");
		shipOverrideKeys[i].InitialBombs = clientset->GetOverrideKey(shipNames[i], "InitialBombs");
		shipOverrideKeys[i].MaxBombs = clientset->GetOverrideKey(shipNames[i], "MaxBombs");
		shipOverrideKeys[i].SeeMines = clientset->GetOverrideKey(shipNames[i], "SeeMines");
		shipOverrideKeys[i].SeeBombLevel = clientset->GetOverrideKey(shipNames[i], "SeeBombLevel");
		shipOverrideKeys[i].Gravity = clientset->GetOverrideKey(shipNames[i], "Gravity");
		shipOverrideKeys[i].GravityTopSpeed = clientset->GetOverrideKey(shipNames[i], "GravityTopSpeed");
		shipOverrideKeys[i].BulletFireEnergy = clientset->GetOverrideKey(shipNames[i], "BulletFireEnergy");
		shipOverrideKeys[i].MultiFireEnergy = clientset->GetOverrideKey(shipNames[i], "MultiFireEnergy");
		shipOverrideKeys[i].BombFireEnergy = clientset->GetOverrideKey(shipNames[i], "BombFireEnergy");
		shipOverrideKeys[i].BombFireEnergyUpgrade = clientset->GetOverrideKey(shipNames[i], "BombFireEnergyUpgrade");
		shipOverrideKeys[i].LandmineFireEnergy = clientset->GetOverrideKey(shipNames[i], "LandmineFireEnergy");
		shipOverrideKeys[i].LandmineFireEnergyUpgrade = clientset->GetOverrideKey(shipNames[i], "LandmineFireEnergyUpgrade");
		shipOverrideKeys[i].CloakEnergy = clientset->GetOverrideKey(shipNames[i], "CloakEnergy");
		shipOverrideKeys[i].StealthEnergy = clientset->GetOverrideKey(shipNames[i], "StealthEnergy");
		shipOverrideKeys[i].AntiWarpEnergy = clientset->GetOverrideKey(shipNames[i], "AntiWarpEnergy");
		shipOverrideKeys[i].XRadarEnergy = clientset->GetOverrideKey(shipNames[i], "XRadarEnergy");
		shipOverrideKeys[i].MaximumRotation = clientset->GetOverrideKey(shipNames[i], "MaximumRotation");
		shipOverrideKeys[i].MaximumThrust = clientset->GetOverrideKey(shipNames[i], "MaximumThrust");
		shipOverrideKeys[i].MaximumSpeed = clientset->GetOverrideKey(shipNames[i], "MaximumSpeed");
		shipOverrideKeys[i].MaximumRecharge = clientset->GetOverrideKey(shipNames[i], "MaximumRecharge");
		shipOverrideKeys[i].MaximumEnergy = clientset->GetOverrideKey(shipNames[i], "MaximumEnergy");
		shipOverrideKeys[i].InitialRotation = clientset->GetOverrideKey(shipNames[i], "InitialRotation");
		shipOverrideKeys[i].InitialThrust = clientset->GetOverrideKey(shipNames[i], "InitialThrust");
		shipOverrideKeys[i].InitialSpeed = clientset->GetOverrideKey(shipNames[i], "InitialSpeed");
		shipOverrideKeys[i].InitialRecharge = clientset->GetOverrideKey(shipNames[i], "InitialRecharge");
		shipOverrideKeys[i].InitialEnergy = clientset->GetOverrideKey(shipNames[i], "InitialEnergy");
		shipOverrideKeys[i].UpgradeRotation = clientset->GetOverrideKey(shipNames[i], "UpgradeRotation");
		shipOverrideKeys[i].UpgradeThrust = clientset->GetOverrideKey(shipNames[i], "UpgradeThrust");
		shipOverrideKeys[i].UpgradeSpeed = clientset->GetOverrideKey(shipNames[i], "UpgradeSpeed");
		shipOverrideKeys[i].UpgradeRecharge = clientset->GetOverrideKey(shipNames[i], "UpgradeRecharge");
		shipOverrideKeys[i].UpgradeEnergy = clientset->GetOverrideKey(shipNames[i], "UpgradeEnergy");
		shipOverrideKeys[i].AfterburnerEnergy = clientset->GetOverrideKey(shipNames[i], "AfterburnerEnergy");
		shipOverrideKeys[i].BombThrust = clientset->GetOverrideKey(shipNames[i], "BombThrust");
		shipOverrideKeys[i].TurretThrustPenalty = clientset->GetOverrideKey(shipNames[i], "TurretThrustPenalty");
		shipOverrideKeys[i].TurretSpeedPenalty = clientset->GetOverrideKey(shipNames[i], "TurretSpeedPenalty");
		shipOverrideKeys[i].BulletFireDelay = clientset->GetOverrideKey(shipNames[i], "BulletFireDelay");
		shipOverrideKeys[i].MultiFireDelay = clientset->GetOverrideKey(shipNames[i], "MultiFireDelay");
		shipOverrideKeys[i].BombFireDelay = clientset->GetOverrideKey(shipNames[i], "BombFireDelay");
		shipOverrideKeys[i].LandmineFireDelay = clientset->GetOverrideKey(shipNames[i], "LandmineFireDelay");
		shipOverrideKeys[i].RocketTime = clientset->GetOverrideKey(shipNames[i], "RocketTime");
		shipOverrideKeys[i].InitialBounty = clientset->GetOverrideKey(shipNames[i], "InitialBounty");
		shipOverrideKeys[i].DamageFactor = clientset->GetOverrideKey(shipNames[i], "DamageFactor");
		shipOverrideKeys[i].AttachBounty = clientset->GetOverrideKey(shipNames[i], "AttachBounty");
		shipOverrideKeys[i].SoccerThrowTime = clientset->GetOverrideKey(shipNames[i], "SoccerThrowTime");
		shipOverrideKeys[i].SoccerBallProximity = clientset->GetOverrideKey(shipNames[i], "SoccerBallProximity");
		shipOverrideKeys[i].MaxMines = clientset->GetOverrideKey(shipNames[i], "MaxMines");
		shipOverrideKeys[i].RepelMax = clientset->GetOverrideKey(shipNames[i], "RepelMax");
		shipOverrideKeys[i].BurstMax = clientset->GetOverrideKey(shipNames[i], "BurstMax");
		shipOverrideKeys[i].DecoyMax = clientset->GetOverrideKey(shipNames[i], "DecoyMax");
		shipOverrideKeys[i].ThorMax = clientset->GetOverrideKey(shipNames[i], "ThorMax");
		shipOverrideKeys[i].BrickMax = clientset->GetOverrideKey(shipNames[i], "BrickMax");
		shipOverrideKeys[i].RocketMax = clientset->GetOverrideKey(shipNames[i], "RocketMax");
		shipOverrideKeys[i].PortalMax = clientset->GetOverrideKey(shipNames[i], "PortalMax");
		shipOverrideKeys[i].InitialRepel = clientset->GetOverrideKey(shipNames[i], "InitialRepel");
		shipOverrideKeys[i].InitialBurst = clientset->GetOverrideKey(shipNames[i], "InitialBurst");
		shipOverrideKeys[i].InitialBrick = clientset->GetOverrideKey(shipNames[i], "InitialBrick");
		shipOverrideKeys[i].InitialRocket = clientset->GetOverrideKey(shipNames[i], "InitialRocket");
		shipOverrideKeys[i].InitialThor = clientset->GetOverrideKey(shipNames[i], "InitialThor");
		shipOverrideKeys[i].InitialDecoy = clientset->GetOverrideKey(shipNames[i], "InitialDecoy");
		shipOverrideKeys[i].InitialPortal = clientset->GetOverrideKey(shipNames[i], "InitialPortal");
		shipOverrideKeys[i].DisableFastShooting = clientset->GetOverrideKey(shipNames[i], "DisableFastShooting");
	}

	globalOverrideKeys.BulletDamageLevel = clientset->GetOverrideKey("Bullet", "BulletDamageLevel");
	globalOverrideKeys.BulletDamageUpgrade = clientset->GetOverrideKey("Bullet", "BulletDamageUpgrade");

	globalOverrideKeys.BombDamageLevel = clientset->GetOverrideKey("Bomb", "BombDamageLevel");
	globalOverrideKeys.EBombShutdownTime = clientset->GetOverrideKey("Bomb", "EBombShutdownTime");
	globalOverrideKeys.EBombDamagePercent = clientset->GetOverrideKey("Bomb", "EBombDamagePercent");
	globalOverrideKeys.BBombDamagePercent = clientset->GetOverrideKey("Bomb", "BBombDamagePercent");
	globalOverrideKeys.JitterTime = clientset->GetOverrideKey("Bomb", "JitterTime");

	globalOverrideKeys.BurstDamageLevel = clientset->GetOverrideKey("Burst", "BurstDamageLevel");

	globalOverrideKeys.DecoyAliveTime = clientset->GetOverrideKey("Misc", "DecoyAliveTime");
	globalOverrideKeys.WarpPointDelay = clientset->GetOverrideKey("Misc", "WarpPointDelay");

	globalOverrideKeys.RocketThrust = clientset->GetOverrideKey("Rocket", "RocketThrust");
	globalOverrideKeys.RocketSpeed = clientset->GetOverrideKey("Rocket", "RocketSpeed");

	globalOverrideKeys.InactiveShrapDamage = clientset->GetOverrideKey("Shrapnel", "InactiveShrapDamage");
	globalOverrideKeys.ShrapnelDamagePercent = clientset->GetOverrideKey("Shrapnel", "ShrapnelDamagePercent");

	globalOverrideKeys.MapZoomFactor = clientset->GetOverrideKey("Radar", "MapZoomFactor");

	globalOverrideKeys.FlaggerGunUpgrade = clientset->GetOverrideKey("Flag", "FlaggerGunUpgrade");
	globalOverrideKeys.FlaggerBombUpgrade = clientset->GetOverrideKey("Flag", "FlaggerBombUpgrade");

	globalOverrideKeys.AllowBombs = clientset->GetOverrideKey("Soccer", "AllowBombs");
	globalOverrideKeys.AllowGuns = clientset->GetOverrideKey("Soccer", "AllowGuns");
	globalOverrideKeys.UseFlagger = clientset->GetOverrideKey("Soccer", "UseFlagger");
	globalOverrideKeys.BallLocation = clientset->GetOverrideKey("Soccer", "BallLocation");

	globalOverrideKeys.DoorMode = clientset->GetOverrideKey("Door", "DoorMode");

	globalOverrideKeys.BombExplodePixels = clientset->GetOverrideKey("Bomb", "BombExplodePixels");
}

local int checkUsingPerShip(Player *p, ShipHull *hull)
{
	if (items->getPropertySumOnHull(p, hull, "bulletdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "bulletdamageup", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "bombdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "ebombtime", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "ebombdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "bbombdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "burstdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "decoyalive", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "warppointdelay", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "rocketthrust", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "rocketspeed", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "inactshrapdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "shrapdamage", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "mapzoom", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "flaggunup", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "flagbombup", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "soccerallowbombs", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "soccerallowguns", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "socceruseflag", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "soccerseeball", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "jittertime", 0)) return 1;
	if (items->getPropertySumOnHull(p, hull, "explodepixels", 0)) return 1;

	return 0;
}

local int doOverrideAdvisers(Player *p, int ship, int shipset, const char *prop, int init_value)
{
    LinkedList advisers = LL_INITIALIZER;
	Ahscorespawner *spawnAdviser;
	Link *link;

	mm->GetAdviserList(A_HSCORE_SPAWNER, p->arena, &advisers);
	int value = init_value;

	FOR_EACH(&advisers, spawnAdviser, link)
  {
		value = spawnAdviser->getOverrideValue(p, ship, shipset, prop, value);
	}

	return value;
}


#define DO_SPECIAL(key_name, override_key) \
int key_name = items->getPropertySumOnShipSet(p, i, shipset, #key_name, 0); \
key_name = doOverrideAdvisers(p, i, shipset, #key_name, key_name); \
if (key_name) clientset->PlayerOverride(p, shipOverrideKeys[i].override_key, 2); \
else clientset->PlayerUnoverride(p, shipOverrideKeys[i].override_key);

#define DO_GUNBOMB(key_name, init_override_key, max_override_key) \
int key_name = min(3, items->getPropertySumOnShipSet(p, i, shipset, #key_name, 0)); \
key_name = doOverrideAdvisers(p, i, shipset, #key_name, key_name); \
if (key_name > 0) { \
	clientset->PlayerOverride(p, shipOverrideKeys[i].init_override_key, key_name); \
	clientset->PlayerOverride(p, shipOverrideKeys[i].max_override_key, key_name); } \
else { \
	clientset->PlayerUnoverride(p, shipOverrideKeys[i].init_override_key); \
	clientset->PlayerUnoverride(p, shipOverrideKeys[i].max_override_key); }

#define DO_STATUS(key_name, init_override_key, upgrade) \
int key_name = items->getPropertySumOnShipSet(p, i, shipset, #key_name, 0); \
key_name = doOverrideAdvisers(p, i, shipset, #key_name, key_name); \
int key_name##_init = cfg->GetInt(conf, shipname, #init_override_key, 0); \
int key_name##_actual = max(0, key_name##_init + (upgrade * key_name)); \
key_name##_actual = doOverrideAdvisers(p, i, shipset, #key_name"_actual", key_name##_actual); \
clientset->PlayerOverride(p, shipOverrideKeys[i].init_override_key, key_name##_actual);

#define DO_POSITIVE(key_name, override_key) \
int key_name##_init = cfg->GetInt(conf, shipname, #override_key, 0); \
int key_name = max(0, items->getPropertySumOnShipSet(p, i, shipset, #key_name, key_name##_init)); \
key_name = doOverrideAdvisers(p, i, shipset, #key_name, key_name); \
clientset->PlayerOverride(p, shipOverrideKeys[i].override_key, key_name);

#define DO_BOOLEAN(key_name, override_key) \
int key_name##_init = cfg->GetInt(conf, shipname, #override_key, 0); \
int key_name = items->getPropertySumOnShipSet(p, i, shipset, #key_name, key_name##_init); \
key_name = doOverrideAdvisers(p, i, shipset, #key_name, key_name); \
clientset->PlayerOverride(p, shipOverrideKeys[i].override_key, key_name != 0);

#define DO_GENERAL(key_name, override_key) \
int key_name##_init = cfg->GetInt(conf, shipname, #override_key, 0); \
int key_name = items->getPropertySumOnShipSet(p, i, shipset, #key_name, key_name##_init); \
key_name = doOverrideAdvisers(p, i, shipset, #key_name, key_name); \
clientset->PlayerOverride(p, shipOverrideKeys[i].override_key, key_name);

#define DO_GLOBAL(key_name, setting_group, override_key) \
int key_name##_init = cfg->GetInt(conf, #setting_group, #override_key, 0); \
int key_name = items->getPropertySumOnShipSet(p, p->p_ship, shipset, #key_name, key_name##_init); \
key_name = doOverrideAdvisers(p, p->p_ship, shipset, #key_name, key_name); \
clientset->PlayerOverride(p, globalOverrideKeys.override_key, key_name);

#define DO_GLOBAL_X1000(key_name, setting_group, override_key) \
int key_name##_init = cfg->GetInt(conf, #setting_group, #override_key, 0); \
int key_name = items->getPropertySumOnShipSet(p, p->p_ship, shipset, #key_name, key_name##_init); \
key_name = doOverrideAdvisers(p, p->p_ship, shipset, #key_name, key_name); \
clientset->PlayerOverride(p, globalOverrideKeys.override_key, key_name * 1000);

local void addOverrides(Player *p, int shipset)
{
	lm->LogP(L_DRIVEL, "hscore_spawner", p, "Updating player overrides for shipset %i", shipset);

	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	if (p->arena == NULL) return;

	ConfigHandle conf = p->arena->cfg;
	int i;

	for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
	{
		ShipHull *hull = database->getPlayerHull(p, i, shipset);
		if (hull != NULL)
		{
			char *shipname = shipNames[i];

			DO_SPECIAL(cloak, CloakStatus)
			DO_SPECIAL(stealth, StealthStatus)
			DO_SPECIAL(xradar, XRadarStatus)
			DO_SPECIAL(antiwarp, AntiWarpStatus)

			DO_GUNBOMB(gunlevel, InitialGuns, MaxGuns)
			DO_GUNBOMB(bomblevel, InitialBombs, MaxBombs)

			int thrust_up = cfg->GetInt(conf, shipname, "UpgradeThrust", 0);
			int speed_up = cfg->GetInt(conf, shipname, "UpgradeSpeed", 0);
			int energy_up = cfg->GetInt(conf, shipname, "UpgradeEnergy", 0);
			int recharge_up = cfg->GetInt(conf, shipname, "UpgradeRecharge", 0);
			int rotation_up = cfg->GetInt(conf, shipname, "UpgradeRotation", 0);

			DO_STATUS(thrust, InitialThrust, thrust_up)
			DO_STATUS(speed, InitialSpeed, speed_up)
			DO_STATUS(energy, InitialEnergy, energy_up)
			DO_STATUS(recharge, InitialRecharge, recharge_up)
			DO_STATUS(rotation, InitialRotation, rotation_up)
			DO_STATUS(maxthrust, MaximumThrust, thrust_up)
			DO_STATUS(maxspeed, MaximumSpeed, speed_up)
			DO_STATUS(afterburner, AfterburnerEnergy, recharge_up)

			DO_POSITIVE(burstmax, BurstMax)
			DO_POSITIVE(repelmax, RepelMax)
			DO_POSITIVE(decoymax, DecoyMax)
			DO_POSITIVE(thormax, ThorMax)
			DO_POSITIVE(brickmax, BrickMax)
			DO_POSITIVE(rocketmax, RocketMax)
			DO_POSITIVE(portalmax, PortalMax)
			DO_POSITIVE(burst, InitialBurst)
			DO_POSITIVE(repel, InitialRepel)
			DO_POSITIVE(decoy, InitialDecoy)
			DO_POSITIVE(thor, InitialThor)
			DO_POSITIVE(brick, InitialBrick)
			DO_POSITIVE(rocket, InitialRocket)
			DO_POSITIVE(portal, InitialPortal)

			DO_GENERAL(shraprate, ShrapnelRate)
			DO_GENERAL(maxmines, MaxMines)
			DO_GENERAL(attachbounty, AttachBounty)
			DO_GENERAL(initialbounty, InitialBounty)

			DO_BOOLEAN(seemines, SeeMines)

			DO_GENERAL(seebomblevel, SeeBombLevel)
			DO_GENERAL(bulletenergy, BulletFireEnergy)
			DO_GENERAL(multienergy, MultiFireEnergy)
			DO_GENERAL(bombenergy, BombFireEnergy)
			DO_GENERAL(bombenergyup, BombFireEnergyUpgrade)
			DO_GENERAL(mineenergy, LandmineFireEnergy)
			DO_GENERAL(mineenergyup, LandmineFireEnergyUpgrade)
			DO_GENERAL(bulletdelay, BulletFireDelay)
			DO_GENERAL(multidelay, MultiFireDelay)
			DO_GENERAL(bombdelay, BombFireDelay)
			DO_GENERAL(minedelay, LandmineFireDelay)

			DO_POSITIVE(cloakenergy, CloakEnergy)
			DO_POSITIVE(stealthenergy, StealthEnergy)
			DO_POSITIVE(antienergy, AntiWarpEnergy)
			DO_POSITIVE(xradarenergy, XRadarEnergy)

			DO_GENERAL(bombthrust, BombThrust)
			DO_GENERAL(rockettime, RocketTime)
			DO_GENERAL(damagefactor, DamageFactor)
			DO_GENERAL(soccertime, SoccerThrowTime)
			DO_GENERAL(soccerprox, SoccerBallProximity)
			DO_GENERAL(gravspeed, GravityTopSpeed)
			DO_GENERAL(grav, Gravity)
			DO_GENERAL(turretthrust, TurretThrustPenalty)
			DO_GENERAL(turretspeed, TurretSpeedPenalty)
			DO_GENERAL(nofastshoot, DisableFastShooting)

			data->usingPerShip[i] = checkUsingPerShip(p, hull);
		}
	}

	//add globals if the ship uses them
	if (p->p_ship != SHIP_SPEC)
	{
		DO_GLOBAL_X1000(bulletdamage, Bullet, BulletDamageLevel)
		DO_GLOBAL_X1000(bulletdamageup, Bullet, BulletDamageUpgrade)
		DO_GLOBAL_X1000(bombdamage, Bomb, BombDamageLevel)
		DO_GLOBAL(ebombtime, Bomb, EBombShutdownTime)
		DO_GLOBAL(ebombdamage, Bomb, EBombDamagePercent)
		DO_GLOBAL(bbombdamage, Bomb, BBombDamagePercent)
		DO_GLOBAL_X1000(burstdamage, Burst, BurstDamageLevel)
		DO_GLOBAL(jittertime, Bomb, JitterTime)
		DO_GLOBAL(decoyalive, Misc, DecoyAliveTime)
		DO_GLOBAL(warppointdelay, Misc, WarpPointDelay)
		DO_GLOBAL(rocketthrust, Rocket, RocketThrust)
		DO_GLOBAL(rocketspeed, Rocket, RocketSpeed)
		DO_GLOBAL_X1000(inactshrapdamage, Shrapnel, InactiveShrapDamage)
		DO_GLOBAL(shrapdamage, Shrapnel, ShrapnelDamagePercent)
		DO_GLOBAL(mapzoom, Radar, MapZoomFactor)
		DO_GLOBAL(flaggunup, Flag, FlaggerGunUpgrade)
		DO_GLOBAL(flagbombup, Flag, FlaggerBombUpgrade)
		DO_GLOBAL(soccerallowbombs, Soccer, AllowBombs)
		DO_GLOBAL(soccerallowguns, Soccer, AllowGuns)
		DO_GLOBAL(socceruseflag, Soccer, UseFlagger)
		DO_GLOBAL(soccerseeball, Soccer, BallLocation)
		DO_GLOBAL(doormode, Door, DoorMode)
		DO_GLOBAL(explodepixels, Bomb, BombExplodePixels)
	}
}


local void removeOverrides(Player *p)
{
	for (int i = 0; i < 8; i++)
	{
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].ShrapnelMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].ShrapnelRate);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].CloakStatus);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].StealthStatus);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].XRadarStatus);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].AntiWarpStatus);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialGuns);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaxGuns);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialBombs);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaxBombs);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].SeeMines);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].SeeBombLevel);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].Gravity);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].GravityTopSpeed);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BulletFireEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MultiFireEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BombFireEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BombFireEnergyUpgrade);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].LandmineFireEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].LandmineFireEnergyUpgrade);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].CloakEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].StealthEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].AntiWarpEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].XRadarEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaximumRotation);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaximumThrust);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaximumSpeed);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaximumRecharge);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaximumEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialRotation);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialThrust);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialSpeed);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialRecharge);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].UpgradeRotation);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].UpgradeThrust);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].UpgradeSpeed);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].UpgradeRecharge);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].UpgradeEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].AfterburnerEnergy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BombThrust);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].TurretThrustPenalty);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].TurretSpeedPenalty);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BulletFireDelay);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MultiFireDelay);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BombFireDelay);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].LandmineFireDelay);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].RocketTime);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialBounty);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].DamageFactor);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].AttachBounty);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].SoccerThrowTime);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].SoccerBallProximity);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].MaxMines);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].RepelMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BurstMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].DecoyMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].ThorMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].BrickMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].RocketMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].PortalMax);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialRepel);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialBurst);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialBrick);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialRocket);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialThor);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialDecoy);
		clientset->PlayerUnoverride(p, shipOverrideKeys[i].InitialPortal);
	}

	clientset->PlayerUnoverride(p, globalOverrideKeys.BulletDamageLevel);
	clientset->PlayerUnoverride(p, globalOverrideKeys.BulletDamageUpgrade);
	clientset->PlayerUnoverride(p, globalOverrideKeys.BombDamageLevel);
	clientset->PlayerUnoverride(p, globalOverrideKeys.EBombShutdownTime);
	clientset->PlayerUnoverride(p, globalOverrideKeys.EBombDamagePercent);
	clientset->PlayerUnoverride(p, globalOverrideKeys.BBombDamagePercent);
	clientset->PlayerUnoverride(p, globalOverrideKeys.JitterTime);
	clientset->PlayerUnoverride(p, globalOverrideKeys.BurstDamageLevel);
	clientset->PlayerUnoverride(p, globalOverrideKeys.DecoyAliveTime);
	clientset->PlayerUnoverride(p, globalOverrideKeys.WarpPointDelay);
	clientset->PlayerUnoverride(p, globalOverrideKeys.RocketThrust);
	clientset->PlayerUnoverride(p, globalOverrideKeys.RocketSpeed);
	clientset->PlayerUnoverride(p, globalOverrideKeys.InactiveShrapDamage);
	clientset->PlayerUnoverride(p, globalOverrideKeys.ShrapnelDamagePercent);
	clientset->PlayerUnoverride(p, globalOverrideKeys.MapZoomFactor);
	clientset->PlayerUnoverride(p, globalOverrideKeys.FlaggerGunUpgrade);
	clientset->PlayerUnoverride(p, globalOverrideKeys.FlaggerBombUpgrade);
	clientset->PlayerUnoverride(p, globalOverrideKeys.AllowBombs);
	clientset->PlayerUnoverride(p, globalOverrideKeys.AllowGuns);
	clientset->PlayerUnoverride(p, globalOverrideKeys.UseFlagger);
	clientset->PlayerUnoverride(p, globalOverrideKeys.BallLocation);
	clientset->PlayerUnoverride(p, globalOverrideKeys.DoorMode);
	clientset->PlayerUnoverride(p, globalOverrideKeys.BombExplodePixels);
}

/*
local void Pppk(Player *p, byte *p2, int len)
{
	Arena *arena = p->arena;
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	// handle common errors
	if (!arena) return;

	// speccers don't get their position sent to anyone
	if (p->p_ship == SHIP_SPEC)
		return;

	if (data->spawned == 0) //player hasn't been spawned
	{
		int enterDelay = cfg->GetInt(p->arena->cfg, "Kill", "EnterDelay", 100);
		if (current_ticks() > (data->lastDeath + enterDelay + 150)) //not still dead
		{
			if (data->underOurControl == 1) //attached to the arena
			{
				spawnPlayer(p, database->getPlayerShipSet(p));
			}
		}
	}
}
*/
local void PlayerSettingsReceived(Player *p, int success, void *clos)
{
	PlayerDataStruct *pdata = (PlayerDataStruct*)clos;

	if (success) {
		if (pdata->reship_after_settings) {
			lm->LogP(L_INFO, "hscore_spawner", p, "Player settings received, reshipping");

	        Target t;
	        //Ihscorespawner *spawner;
	        t.type = T_PLAYER;
	        t.u.p = p;

			game->ShipReset(&t);
		} else {
			lm->LogP(L_INFO, "hscore_spawner", p, "Player settings received.");
		}
	} else {
		lm->LogP(L_ERROR, "hscore_spawner", p, "Player settings rejected. Resending overrides and reshipping player");
		resendOverrides(p);
	}

}



local void playerActionCallback(Player *p, int action, Arena *arena)
{
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	if (action == PA_ENTERARENA)
	{
		data->underOurControl = 1;
		data->spawned = 0;
		LLInit(&data->ignoredPrizes);
		//the player is entering the arena.
	}
	else if (action == PA_LEAVEARENA)
	{
		data->underOurControl = 0;
		removeOverrides(p);

		LLEnum(&data->ignoredPrizes, afree);
		LLEmpty(&data->ignoredPrizes);
	}
}

local void playerSpawnCallback(Player *p, int reason)
{
  lm->LogP(L_INFO, "hscore_spawner", p, "Player respawning. Reason: %i.", reason);

  spawnPlayer(p, database->getPlayerShipSet(p));
}

local void shipsLoadedCallback(Player *p)
{
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	database->lock();
	addOverrides(p, database->getPlayerShipSet(p));
	database->unlock();
	//send the packet the first time
	data->reship_after_settings = 1;
	clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);

	data->dirty = 0;
	data->currentShip = p->p_ship;
}

local void shipsetChangedCallback(Player *p, int oldshipset, int newshipset)
{
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	database->lock();
	addOverrides(p, newshipset);
	database->unlock();

	//send the packet the first time
	data->reship_after_settings = 1;
	clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);

	data->dirty = 0;
	data->spawned = 0;
	data->lastDeath = 0;
	data->currentShip = p->p_ship;
}

local void OnShipAdded(Player *p, int ship, int shipset) {
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	database->lock();
	addOverrides(p, shipset);
	database->unlock();
	//send the packet the first time
	data->reship_after_settings = 1;
	clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);

	data->dirty = 0;
	data->spawned = 0;
	data->lastDeath = 0;
	data->currentShip = p->p_ship;
}

local void itemCountChangedCallback(Player *p, ShipHull *hull, Item *item, InventoryEntry *entry, int newCount, int oldCount)
{
  PlayerDataStruct *data = PPDATA(p, playerDataKey);

  // Make sure we're only sending settings if the player's ship has changed.
  if (p && database->getPlayerCurrentHull(p) == hull && item->resendSets) {
		data->dirty = 0;
		addOverrides(p, database->getPlayerShipSet(p));
		data->reship_after_settings = 0;
		clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
  } else {
		//check if it changed anything in clientset, and if it did, recompute and flag dirty
		if (item->affectsSets) {
		  data->dirty = 1;
		  return;
		}
  }
}

local void killCallback(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
	PlayerDataStruct *data = PPDATA(killed, playerDataKey);
	data->spawned = 0;
	data->lastDeath = current_ticks();

	//if the dirty bit is set, then send new settings while they're dead
	if (data->dirty)
	{
		data->dirty = 0;
		database->lock();
		addOverrides(killed, database->getPlayerShipSet(killed));
		database->unlock();
		data->reship_after_settings = 0;
		clientset->SendClientSettingsWithCallback(killed, PlayerSettingsReceived, data);
	}
}

local void shipFreqChangeCallback(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	//they need a respawn whenever they change ships
	PlayerDataStruct *data = PPDATA(p, playerDataKey);
	ml->ClearTimer(resetBountyTimerCallback, p);

	if (oldship != newship)
	{
		data->spawned = 0;

		if (oldship == SHIP_SPEC) oldship = 0;

		int perShipInUse = (data->usingPerShip[oldship] || (newship != SHIP_SPEC && data->usingPerShip[newship]));
		int changingIntoNewShip = (newship != SHIP_SPEC && newship != data->currentShip);

		//resend the data if it's dirty or if the player changed ships, isn't going into spec, and at least one of the ships uses per ship settings.
		if (data->dirty == 1 || (changingIntoNewShip && perShipInUse))
		{
			data->dirty = 0;
			database->lock();
			addOverrides(p, database->getPlayerShipSet(p));
			database->unlock();
			data->reship_after_settings = 1;
			clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
		}

		if (newship != SHIP_SPEC)
			data->currentShip = newship;
	}
	else
	{
		data->spawned = 0;

		if (data->dirty == 1)
		{
			data->dirty = 0;
			data->reship_after_settings = 1;
			clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
		}
	}
}

local void flagWinCallback(Arena *arena, int freq, int *points)
{
	//players on the winning freq need a respawn because of a continuum quirk
	Link *link;
	Player *p;
    pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		if(p->arena == arena && p->p_freq == freq && p->p_ship != SHIP_SPEC)
		{
			PlayerDataStruct *data = PPDATA(p, playerDataKey);
			data->spawned = 0;

			if (data->dirty == 1)
			{
				data->dirty = 0;
				data->reship_after_settings = 1;
				clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
			}
		}
	}
	pd->Unlock();
}

local int resetBountyTimerCallback(void *clos)
{
	Player *p = (Player*)clos;

	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	if (data->oldBounty < p->position.bounty)
	{
		selfpos->SetBounty(p, data->oldBounty);
	}

	return FALSE;
}

local int handleItemCallback(void *clos)
{
	CallbackData *data = clos;
	Player *p = data->player;
	PlayerDataStruct *pdata = PPDATA(p, playerDataKey);
	ShipHull *hull = data->hull;
	Item *item = data->item;
	int mult = data->mult;
	int force = data->force;
	Link *propLink;
	int oldBounty = p->position.bounty;
	int prized = 0;
	int fixBounty = 1;

	afree(clos);

	if (database->getPlayerCurrentHull(p) != hull)
	{
		//it's not on their current ship!
		return FALSE;
	}

	if (item == NULL)
	{
		lm->LogP(L_ERROR, "hscore_spawner", p, "NULL item in callback.");
		return FALSE;
	}

	if (item->ammo && item->needsAmmo)
	{
		// needs ammo
		int count = items->getItemCountOnHull(p, item->ammo, hull);
		if (!force && count < item->minAmmo)
		{
			// no ammo present
			return FALSE;
		}
	}

	for (propLink = LLGetHead(&item->propertyList); propLink; propLink = propLink->next)
	{
		Property *prop = propLink->data;
		const char *propName = prop->name;
		int prizeNumber = -1;

		if (prop->value == 0) continue;

		int count = abs(prop->value);
		int mult2 = count/prop->value;

		if (strcasecmp(propName, "bomblevel") == 0)
			prizeNumber = 9;
		else if (strcasecmp(propName, "gunlevel") == 0)
		{
			prizeNumber = 8;
			count--;
			if (count == 0)
				prizeNumber = -1;
		}
		else if (strcasecmp(propName, "repel") == 0)
			prizeNumber = 21;
		else if (strcasecmp(propName, "burst") == 0)
			prizeNumber = 22;
		else if (strcasecmp(propName, "thor") == 0)
			prizeNumber = 24;
		else if (strcasecmp(propName, "portal") == 0)
			prizeNumber = 28;
		else if (strcasecmp(propName, "decoy") == 0)
			prizeNumber = 23;
		else if (strcasecmp(propName, "brick") == 0)
			prizeNumber = 26;
		else if (strcasecmp(propName, "rocket") == 0)
			prizeNumber = 27;
		else if (strcasecmp(propName, "xradar") == 0)
			prizeNumber = 6;
		else if (strcasecmp(propName, "cloak") == 0)
			prizeNumber = 5;
		else if (strcasecmp(propName, "stealth") == 0)
			prizeNumber = 4;
		else if (strcasecmp(propName, "antiwarp") == 0)
			prizeNumber = 20;
		else if (strcasecmp(propName, "thrust") == 0)
		{
			if (mult*mult2 > 0)
				prizeNumber = -1;
			else
				prizeNumber = 11;
		}
		else if (strcasecmp(propName, "speed") == 0)
		{
			if (mult*mult2 > 0)
				prizeNumber = -1;
			else
				prizeNumber = 12;
		}
		else if (strcasecmp(propName, "energy") == 0)
		{
			if (mult*mult2 > 0)
				prizeNumber = -1;
			else
				prizeNumber = 2;
		}
		else if (strcasecmp(propName, "recharge") == 0)
		{
			if (mult*mult2 > 0)
				prizeNumber = -1;
			else
				prizeNumber = 1;
		}
		else if (strcasecmp(propName, "rotation") == 0)
		{
			if (mult*mult2 > 0)
				prizeNumber = -1;
			else
				prizeNumber = 3;
		}
		else if (strcasecmp(propName, "bounce") == 0)
			prizeNumber = 10;
		else if (strcasecmp(propName, "prox") == 0)
			prizeNumber = 16;
		else if (strcasecmp(propName, "shrapnel") == 0)
			prizeNumber = 19;
		else if (strcasecmp(propName, "multifire") == 0)
			prizeNumber = 15;
		else if (strcasecmp(propName, "ignorebounty") == 0)
			fixBounty = 0;

		if (prizeNumber != -1)
		{
			Link *link;
			int shouldPrize = 1;

			link = LLGetHead(&pdata->ignoredPrizes);
			while (link)
			{
				IgnorePrizeStruct *entry = link->data;
				link = link->next;

				if (entry->timeout < current_ticks())
				{
					// remove it
					afree(entry);
					LLRemove(&pdata->ignoredPrizes, entry);
				}
				else if (prizeNumber == entry->prize)
				{
					shouldPrize = 0;
				}
			}

			if (shouldPrize)
			{
				Target t;
				t.type = T_PLAYER;
				t.u.p = p;
				game->GivePrize(&t, mult*mult2*prizeNumber, count);
				prized = 1;
			}
		}
	}

	if (prized && fixBounty)
	{
		if (pdata->lastSet + 100 < current_ticks())
		{
			pdata->oldBounty = oldBounty;
		}

		pdata->lastSet = current_ticks();
		ml->ClearTimer(resetBountyTimerCallback, p);
		ml->SetTimer(resetBountyTimerCallback, 50, 0, p, p);
	}

	return FALSE;
}

local void ammoAddedCallback(Player *p, ShipHull *hull, Item *ammoUser) //warnings: cache is out of sync, and lock is held
{
	PlayerDataStruct *pdata = PPDATA(p, playerDataKey);

	CallbackData *data = amalloc(sizeof(*data));
	data->player = p;
	data->hull = hull;
	data->item = ammoUser;
	data->mult = 1;
	data->force = 1;

	if (database->getPlayerCurrentHull(p) == hull && ammoUser->resendSets)
	{
		pdata->dirty = 0;
		addOverrides(p, database->getPlayerShipSet(p));
		pdata->reship_after_settings = 0;
		clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
	}
	else //check if it changed anything in clientset, and if it did, recompute and flag dirty
	{
		if (ammoUser->affectsSets)
		{
			pdata->dirty = 1;
			return;
		}
	}

	handleItemCallback(data);
}

local void ammoRemovedCallback(Player *p, ShipHull *hull, Item *ammoUser) //warnings: cache is out of sync, and lock is held
{
	PlayerDataStruct *pdata = PPDATA(p, playerDataKey);

	CallbackData *data = amalloc(sizeof(*data));
	data->player = p;
	data->hull = hull;
	data->item = ammoUser;
	data->mult = -1;
	data->force = 1;


	if (database->getPlayerCurrentHull(p) == hull && ammoUser->resendSets)
	{
		pdata->dirty = 0;
		addOverrides(p, database->getPlayerShipSet(p));
		pdata->reship_after_settings = 0;
		clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
	}
	else //check if it changed anything in clientset, and if it did, recompute and flag dirty
	{
		if (ammoUser->affectsSets)
		{
			pdata->dirty = 1;
			return;
		}
	}

	handleItemCallback(data);
}

local void triggerEventCallback(Player *p, Item *item, ShipHull *hull, const char *eventName)
{

	if (strcasecmp(eventName, "add") == 0)
	{
		CallbackData *data = amalloc(sizeof(*data));
		data->player = p;
		data->hull = hull;
		data->item = item;
		data->mult = 1;
		data->force = 0;
		handleItemCallback(data);
	}
	else if (strcasecmp(eventName, "del") == 0)
	{
		CallbackData *data = amalloc(sizeof(*data));
		data->player = p;
		data->hull = hull;
		data->item = item;
		data->mult = -1;
		data->force = 0;
		handleItemCallback(data);
	}
	else
	{
		return; //nothing to do
	}
}

/*
local void respawn(Player *p)
{
	By handling the SPAWN callback, we can avoid needing
	to do janky stuff by just giving them items as necessary.

	//a simple redirect won't work because of locking issues.
	//instead rig them for respawn on next position packet
	PlayerDataStruct *data = PPDATA(p, playerDataKey);
	data->spawned = 0;
	data->lastDeath = 0;
}
*/

local void resendOverrides(Player *p)
{
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	database->lock();
	addOverrides(p, database->getPlayerShipSet(p));

	data->reship_after_settings = 1;
	clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
	database->unlock();
}

local int getFullEnergy(Player *p)
{
	if (p->p_ship >= SHIP_WARBIRD && p->p_ship <= SHIP_SHARK)
	{
		ConfigHandle conf = p->arena->cfg;
		const char *shipname = shipNames[p->p_ship];
		int energy = items->getPropertySum(p, p->p_ship, "energy", 0);
		energy = doOverrideAdvisers(p, p->p_ship, database->getPlayerShipSet(p), "energy", energy);
		int initEnergy = cfg->GetInt(conf, shipname, "InitialEnergy", 0);
		int upEnergy = cfg->GetInt(conf, shipname, "UpgradeEnergy", 0);

		int energy_actual = initEnergy + (upEnergy * energy);
		return doOverrideAdvisers(p, p->p_ship, database->getPlayerShipSet(p), "energy_actual", energy_actual);
	}
	else
	{
		return 0;
	}
}

local void ignorePrize(Player *p, int prize)
{
	IgnorePrizeStruct *prizeData = amalloc(sizeof(*prizeData));
	PlayerDataStruct *data = PPDATA(p, playerDataKey);

	prizeData->prize = prize;
	prizeData->timeout = current_ticks() + 50;

	LLAdd(&data->ignoredPrizes, prizeData);
}

local void HSItemReloadCallback(void)
{
	Player *p;
	PlayerDataStruct *data;
	Link *link;
	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		database->lock();
		addOverrides(p, database->getPlayerShipSet(p));
		database->unlock();

		data = PPDATA(p, playerDataKey);
		data->reship_after_settings = 0;
		clientset->SendClientSettingsWithCallback(p, PlayerSettingsReceived, data);
	}
	pd->Unlock();
}

local Ihscorespawner interface =
{
	INTERFACE_HEAD_INIT(I_HSCORE_SPAWNER, "hscore_spawner")
	/*respawn,*/ resendOverrides, getFullEnergy, ignorePrize
};

EXPORT const char info_hscore_spawner[] = "v1.3 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hscore_spawner(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		clientset = mm->GetInterface(I_CLIENTSET, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);

		if (!lm || !pd || !net || !game || !chat || !cfg || !items || !clientset || !database || !ml || !selfpos)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(game);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(clientset);
			mm->ReleaseInterface(database);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(selfpos);

			return MM_FAIL;
		}

		playerDataKey = pd->AllocatePlayerData(sizeof(PlayerDataStruct));
		if (playerDataKey == -1)
		{
			return MM_FAIL;
		}

		//net->AddPacket(C2S_POSITION, Pppk);

		loadOverrides();

		mm->RegCallback(CB_HS_ITEMRELOAD, HSItemReloadCallback, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_HS_ITEMRELOAD, HSItemReloadCallback, ALLARENAS);

		//net->RemovePacket(C2S_POSITION, Pppk);

		ml->ClearTimer(resetBountyTimerCallback, NULL);

		pd->FreePlayerData(playerDataKey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(clientset);
		mm->ReleaseInterface(database);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(selfpos);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegInterface(&interface, arena);

		mm->RegCallback(CB_SPAWN, playerSpawnCallback, arena);
		mm->RegCallback(CB_WARZONEWIN, flagWinCallback, arena);
		mm->RegCallback(CB_SHIPS_LOADED, shipsLoadedCallback, arena);
		mm->RegCallback(CB_SHIP_ADDED, OnShipAdded, arena);
		mm->RegCallback(CB_SHIPSET_CHANGED, shipsetChangedCallback, arena);
		mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->RegCallback(CB_PLAYERACTION, playerActionCallback, arena);
		mm->RegCallback(CB_KILL, killCallback, arena);
		mm->RegCallback(CB_ITEM_COUNT_CHANGED, itemCountChangedCallback, arena);

		mm->RegCallback(CB_TRIGGER_EVENT, triggerEventCallback, arena);
		mm->RegCallback(CB_AMMO_ADDED, ammoAddedCallback, arena);
		mm->RegCallback(CB_AMMO_REMOVED, ammoRemovedCallback, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		if (mm->UnregInterface(&interface, arena))
		{
			return MM_FAIL;
		}

		mm->UnregCallback(CB_SPAWN, playerSpawnCallback, arena);
		mm->UnregCallback(CB_ITEM_COUNT_CHANGED, itemCountChangedCallback, arena);
		mm->UnregCallback(CB_KILL, killCallback, arena);
		mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, arena);
		mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCallback, arena);
		mm->UnregCallback(CB_SHIPSET_CHANGED, shipsetChangedCallback, arena);
		mm->UnregCallback(CB_SHIP_ADDED, OnShipAdded, arena);
		mm->UnregCallback(CB_SHIPS_LOADED, shipsLoadedCallback, arena);
		mm->UnregCallback(CB_WARZONEWIN, flagWinCallback, arena);

		mm->UnregCallback(CB_TRIGGER_EVENT, triggerEventCallback, arena);
		mm->UnregCallback(CB_AMMO_ADDED, ammoAddedCallback, arena);
		mm->UnregCallback(CB_AMMO_REMOVED, ammoRemovedCallback, arena);

		return MM_OK;
	}
	return MM_FAIL;
}


