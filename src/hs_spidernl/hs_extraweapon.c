#include <stdio.h>

#include "asss.h"
#include "fake.h"
#include "packets/shipchange.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_items.h"

typedef struct ArenaData
{
    Player *fake;
    int attached;
} ArenaData;
local int adkey = -1;
local struct ArenaData *getArenaData(Arena *arena)
{
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    return adata;
}

typedef struct ExtraWeaponData
{
    int activated;
    ticks_t activationTick;

    struct Weapons weapon;
    int weaponsFired;

    struct
    {
        int extraWeaponDelay;
        int extraWeaponCount;
    } Properties;
} ExtraWeaponData;

#define PlayerData PlayerData_
typedef struct PlayerData
{
    int extraWeaponAttached;
    int hasExtraWeapon;

    struct ExtraWeaponData extraBombData;
    struct ExtraWeaponData extraMineData;
    struct ExtraWeaponData extraGunData;
    struct ExtraWeaponData extraRepelData;
    struct ExtraWeaponData extraDecoyData;
    struct ExtraWeaponData extraBurstData;
    struct ExtraWeaponData extraThorData;
} PlayerData;
local int pdkey = -1;
local struct PlayerData *getPlayerData(Player *p)
{
    PlayerData *pdata = PPDATA(p, pdkey);
    return pdata;
}

//Interfaces
local Imodman *mm;
local Iarenaman *aman;
local Ihscoredatabase *db;
local Ifake *fake;
local Igame *game;
local Ihscoreitems *items;
local Inet *net;
local Iplayerdata *pd;

//Prototypes
local void editPPK(Player *p, struct C2SPosition *pos);

local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void itemsChangedCB(Player *p, ShipHull *hull);
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq);

local void attachPKT(Player *p, byte *pkt, int len);

local void updatePlayerData(Player *p, ShipHull *hull, int freq, int shipfreqchanged, int lock, int deactivate);
local void triggerExtraWeapon(ExtraWeaponData *extraWeaponData, struct C2SPosition *pos);
local void addExtraWeapon(Player *p, struct C2SPosition *pos, Player *fake, struct ExtraWeaponData *extraWeaponData);
local void deactivateAllWeapons(PlayerData *pdata);
local void attachFake(Player *p, PlayerData *pdata, ArenaData *adata);
local void detachFake(Player *p, PlayerData *pdata, ArenaData *adata);

local void getInterfaces();
local int checkInterfaces();
local void releaseInterfaces();

//Adviser
local void editPPK(Player *p, struct C2SPosition *pos)
{
    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

    if (!IS_STANDARD(p)) return;

    if (!pdata->hasExtraWeapon) return;

    if (!pdata->extraWeaponAttached)
    {
        attachFake(p, pdata, adata);
    }

    if (pos->weapon.type != W_NULL)
    {
        if (pos->weapon.type == W_BOMB || pos->weapon.type == W_PROXBOMB)
        {
            if (!pos->weapon.alternate) //Bomb
               triggerExtraWeapon(&pdata->extraBombData, pos);
            else //Mine
               triggerExtraWeapon(&pdata->extraMineData, pos);
        }
        else if (pos->weapon.type == W_BULLET || pos->weapon.type == W_BOUNCEBULLET)
        {
            triggerExtraWeapon(&pdata->extraGunData, pos);
        }
        else if (pos->weapon.type == W_REPEL)
        {
            triggerExtraWeapon(&pdata->extraRepelData, pos);
        }
        else if (pos->weapon.type == W_DECOY)
        {
            triggerExtraWeapon(&pdata->extraDecoyData, pos);
        }
        else if (pos->weapon.type == W_BURST)
        {
            triggerExtraWeapon(&pdata->extraBurstData, pos);
        }
        else if (pos->weapon.type == W_THOR)
        {
            triggerExtraWeapon(&pdata->extraThorData, pos);
        }
    }
    else
    {
        if (pos->status & STATUS_SAFEZONE)
        {
            //Put the fake player inside safe as well, so any extra weapons are removed
            struct S2CWeapons fakePosition = {
            S2C_WEAPON, pos->rotation, pos->time, pos->x, pos->yspeed,
            adata->fake->pid, pos->xspeed, 0,
            STATUS_SAFEZONE | STATUS_UFO | STATUS_CLOAK | STATUS_STEALTH,
            0, pos->y, 0
            };

            net->SendToOne(p, (byte*)&fakePosition, sizeof(fakePosition), NET_RELIABLE);
            return;
        }
        if (pdata->extraBombData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraBombData);
            return;
        }
        if (pdata->extraMineData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraMineData);
            return;
        }
        if (pdata->extraGunData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraGunData);
            return;
        }
        if (pdata->extraRepelData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraRepelData);
            return;
        }
        if (pdata->extraDecoyData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraDecoyData);
            return;
        }
        if (pdata->extraBurstData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraBurstData);
            return;
        }
        if (pdata->extraThorData.activated)
        {
            addExtraWeapon(p, pos, adata->fake, &pdata->extraThorData);
            return;
        }
    }

}
local Appk PPKAdviser =
{
	ADVISER_HEAD_INIT(A_PPK)

	editPPK,
	NULL
};

//Callbacks
local void killCB(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    PlayerData *pdata = getPlayerData(killed);

    if (pdata->hasExtraWeapon)
    {
        deactivateAllWeapons(pdata);
        pdata->extraWeaponAttached = 0;
    }
}
local void itemsChangedCB(Player *p, ShipHull *hull)
{
    updatePlayerData(p, hull, p->p_freq, 0, 0, 0);
}
local void shipFreqChangeCB(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
    updatePlayerData(p, db->getPlayerShipHull(p, newship), newfreq, 1, 1, 1);
}

local void playerActionCB(Player *p, int action, Arena *arena)
{
	if (action == PA_LEAVEARENA)
	{
        int players = 0;
        Player *i;
		Link *link;

		pd->Lock();
		FOR_EACH_PLAYER(i)
		{
            if (i != p && i->arena == arena && IS_HUMAN(i))
			{
				players++;
			}
        }
		pd->Unlock();

		if (players == 0)
		{
			//Since this arena has no real players in it, get rid of our fake
			//player and hope the arena gets destroyed, detaching the module
			ArenaData *adata = getArenaData(arena);

            if (adata->fake)
            {
                fake->EndFaked(adata->fake);
                adata->fake = NULL;
            }
		}
	}
	if (action == PA_ENTERARENA)
    {
        if (!IS_HUMAN(p))
           return;

        ArenaData *adata = getArenaData(arena);
        if (!adata->fake) //Just in case something weird happened
        {
            adata->fake = fake->CreateFakePlayer("<extraweapon>", arena, 0, 9999);
        }
    }
}

//Packet handlers
local void attachPKT(Player *p, byte *pkt, int len)
{
    ArenaData *adata = getArenaData(p->arena);
    if (!adata->attached) return;

    PlayerData *pdata = getPlayerData(p);
    if (!pdata->hasExtraWeapon) return;

    if (pdata->extraWeaponAttached)
       detachFake(p, pdata, adata); //Detach, it'll auto attach on the next position packet anyway.
}


//Misc/Utilities
local void updatePlayerData(Player *p, ShipHull *hull, int freq, int shipfreqchanged, int lock, int deactivate)
{
    PlayerData *pdata = getPlayerData(p);
    ArenaData *adata = getArenaData(p->arena);

	if (!hull || hull != db->getPlayerCurrentHull(p)) {
        pdata->hasExtraWeapon = 0;

        struct ShipChangePacket shipChange = { S2C_SHIPCHANGE, 0, adata->fake->pid, 9999 };
        net->SendToOne(p, (byte*)&shipChange, 6, NET_RELIABLE);

        return;
    }

    if (deactivate)
    {
        deactivateAllWeapons(pdata);
    }

    if (adata->fake && pdata->extraWeaponAttached && shipfreqchanged)
    {
        detachFake(p, pdata, adata);
    }

    int hadExtraWeapon = pdata->hasExtraWeapon;

    if (lock)
       db->lock();
    //Bomb
    pdata->extraBombData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extrabombcount", 0);
    pdata->extraBombData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extrabombdelay", 10);

    //Mine
    pdata->extraMineData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extraminecount", 0);
    pdata->extraMineData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extraminedelay", 10);

    //Gun
    pdata->extraGunData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extraguncount", 0);
    pdata->extraGunData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extragundelay", 10);

    //Repel
    pdata->extraRepelData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extrarepelcount", 0);
    pdata->extraRepelData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extrarepeldelay", 10);

    //Decoy
    pdata->extraDecoyData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extradecoycount", 0);
    pdata->extraDecoyData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extradecoydelay", 10);

    //Burst
    pdata->extraBurstData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extraburstcount", 0);
    pdata->extraBurstData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extraburstdelay", 10);

    //Thor
    pdata->extraThorData.Properties.extraWeaponCount = items->getPropertySumOnHull(p, hull, "extrathorcount", 0);
    pdata->extraThorData.Properties.extraWeaponDelay = items->getPropertySumOnHull(p, hull, "extrathordelay", 10);
    if (lock)
       db->unlock();

    pdata->hasExtraWeapon = 0;
    pdata->hasExtraWeapon += (pdata->extraBombData.Properties.extraWeaponCount > 0);
    pdata->hasExtraWeapon += (pdata->extraMineData.Properties.extraWeaponCount > 0);
    pdata->hasExtraWeapon += (pdata->extraGunData.Properties.extraWeaponCount > 0);
    pdata->hasExtraWeapon += (pdata->extraRepelData.Properties.extraWeaponCount > 0);
    pdata->hasExtraWeapon += (pdata->extraDecoyData.Properties.extraWeaponCount > 0);
    pdata->hasExtraWeapon += (pdata->extraBurstData.Properties.extraWeaponCount > 0);
    pdata->hasExtraWeapon += (pdata->extraThorData.Properties.extraWeaponCount > 0);

    if (pdata->hasExtraWeapon > 0 && adata->fake)
    {
        if (!hadExtraWeapon || shipfreqchanged)
        {
            struct ShipChangePacket shipChange = { S2C_SHIPCHANGE, hull->ship, adata->fake->pid, freq };
            net->SendToOne(p, (byte*)&shipChange, 6, NET_RELIABLE);

            attachFake(p, pdata, adata);
        }
    }
    else if (adata->fake)
    {
        detachFake(p, pdata, adata);
        struct ShipChangePacket shipChange = { S2C_SHIPCHANGE, 0, adata->fake->pid, 9999 };
        net->SendToOne(p, (byte*)&shipChange, 6, NET_RELIABLE);
    }
}

local void triggerExtraWeapon(ExtraWeaponData *extraWeaponData, struct C2SPosition *pos)
{
     if (extraWeaponData->Properties.extraWeaponCount > 0 && !extraWeaponData->activated)
     {
         extraWeaponData->weapon = pos->weapon;
         extraWeaponData->weaponsFired = 0;
         extraWeaponData->activated = 1;
         extraWeaponData->activationTick = current_ticks();
     }
}

local void addExtraWeapon(Player *p, struct C2SPosition *pos, Player *fake, ExtraWeaponData *extraWeaponData)
{
    if (p->flags.is_dead) {
       extraWeaponData->activated = 0;
       return;
    }
    if (extraWeaponData->weaponsFired == extraWeaponData->Properties.extraWeaponCount) {
       extraWeaponData->activated = 0;
       return;
    }
    if (TICK_DIFF(current_ticks(), extraWeaponData->activationTick) < extraWeaponData->Properties.extraWeaponDelay)
       return;

    //Fire an extra weapon!
    pos->weapon = extraWeaponData->weapon;

    //Use a fake player to make the weapon appear for the 'user' client as well.
    struct S2CWeapons fakeWeapon = {
        S2C_WEAPON, pos->rotation, pos->time, pos->x, pos->yspeed,
        fake->pid, pos->xspeed, 0, STATUS_UFO | STATUS_CLOAK | STATUS_STEALTH,
        0, pos->y, 0
    };
    fakeWeapon.weapon = extraWeaponData->weapon;
    net->SendToOne(p, (byte*)&fakeWeapon, sizeof(fakeWeapon), NET_RELIABLE);

    //Update data
    ++extraWeaponData->weaponsFired;
    extraWeaponData->activationTick = current_ticks();
}
local void deactivateAllWeapons(PlayerData *pdata)
{
    pdata->extraBombData.activated = 0;
    pdata->extraMineData.activated = 0;
    pdata->extraGunData.activated = 0;
    pdata->extraRepelData.activated = 0;
    pdata->extraDecoyData.activated = 0;
    pdata->extraBurstData.activated = 0;
    pdata->extraThorData.activated = 0;
}

local void attachFake(Player *p, PlayerData *pdata, ArenaData *adata)
{
    //Attach to the player or whoever they're attached to
    i16 targetpid = (p->p_attached == -1 ? p->pid : p->p_attached);

    struct SimplePacket attach = { S2C_TURRET, adata->fake->pid, targetpid };
    net->SendToOne(p, (byte*)&attach, 5, NET_RELIABLE);

    pdata->extraWeaponAttached = 1;
}
local void detachFake(Player *p, PlayerData *pdata, ArenaData *adata)
{
    struct SimplePacket detach = { S2C_TURRET, adata->fake->pid, -1};
    net->SendToOne(p, (byte*)&detach, 5, NET_RELIABLE);

    pdata->extraWeaponAttached = 0;
}

//Module/asss stuff
local void getInterfaces()
{
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
    fake = mm->GetInterface(I_FAKE, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
    net = mm->GetInterface(I_NET, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}
local int checkInterfaces()
{
    if (aman && db && fake && game && items && net && pd) return 1;
    return 0;
}
local void releaseInterfaces()
{
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(db);
    mm->ReleaseInterface(fake);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(items);
    mm->ReleaseInterface(net);
    mm->ReleaseInterface(pd);
}

EXPORT const char info_hs_extraweapon[] = "v1.3 by Spidernl";

EXPORT int MM_hs_extraweapon(int action, Imodman *mm_, Arena *arena)
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

        //Arena/Player data
        adkey = aman->AllocateArenaData(sizeof(struct ArenaData));
        pdkey = pd->AllocatePlayerData(sizeof(struct PlayerData));

        if (adkey == -1 || pdkey == -1) //Memory check
        {
            if (adkey  != -1) //free data if it was allocated
                aman->FreeArenaData(adkey);

            if (pdkey != -1) //free data if it was allocated
                pd->FreePlayerData(pdkey);

            releaseInterfaces();
            return MM_FAIL;
        }

        //Add packet handlers
        net->AddPacket(C2S_ATTACHTO, attachPKT);

        return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
        //Remove packet handlers
        net->RemovePacket(C2S_ATTACHTO, attachPKT);

        aman->FreeArenaData(adkey);
        pd->FreePlayerData(pdkey);

        releaseInterfaces();
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
        ArenaData *adata = getArenaData(arena);
        if (adata->attached)
           return MM_FAIL;

        mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);
        mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->RegCallback(CB_KILL, killCB, arena);

        mm->RegAdviser(&PPKAdviser, arena);

        adata->fake = fake->CreateFakePlayer("<extraweapon>", arena, 0, 9999);

        adata->attached = 1;
        return MM_OK;
    }
    else if (action == MM_DETACH)
    {
        ArenaData *adata = getArenaData(arena);
        if (!adata->attached)
           return MM_FAIL;

        mm->UnregAdviser(&PPKAdviser, arena);

        mm->UnregCallback(CB_KILL, killCB, arena);
        mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
        mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
        mm->UnregCallback(CB_PLAYERACTION, playerActionCB, arena);

        if (adata->fake)
        {
            fake->EndFaked(adata->fake);
            adata->fake = NULL;
        }

        adata->attached = 0;
        return MM_OK;
    }

    return MM_FAIL;
}
