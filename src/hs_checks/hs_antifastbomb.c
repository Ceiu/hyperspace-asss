#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "hscore_database.h"
#include "packets/ppk.h"


// Modules...
local Iarenaman *aman;
local Iconfig *cfg;
local Ilogman *lm;
local Imodman *mm;
local Iplayerdata *pd;
local Igame *game;
local Ihscoreitems *items;
local Ihscoredatabase *db;


local int adkey;            // Arena data key
local int pdkey;            // Player data key

typedef struct {
    int attached;           // Whether or not we're attached to this arena

    int earlybombticklimit; // How early a bomb can be and still be allowed.
    int defbombdelay[8];    // Default bomb delay for each ship.
} AConfig;

typedef struct {
    int bombdelay;          // Player specific bomb delay. Updated on freq/ship change.

    u32 lastbombtime;       // Last time the player fired a valid bomb.
} PInfo;

// Prototypes
local void load_config(Arena *arena, AConfig *aconfig);



////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Arena callbacks!!
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Arena-specific configuration is loaded here. Wheeee...
 */
local void a_action(Arena *arena, int action) {
    switch(action) {
        case AA_CREATE:
        case AA_CONFCHANGED:
            load_config(arena, P_ARENA_DATA(arena, adkey));
    }
}

local void p_shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq) {

    if(!p->arena)
        return; // Player somehow isn't in an arena yet.

    AConfig *aconfig = P_ARENA_DATA(p->arena, adkey);
    PInfo *pinfo = PPDATA(p, pdkey);

    if(!aconfig->attached)
        return; // Invalid config or an arena we're not attached to.

    if(newship >= 8)
        return; // We don't care about spectators.

    pinfo->bombdelay = items->getPropertySum(p, newship, "bombdelay", aconfig->defbombdelay[newship]);
    pinfo->bombdelay -= aconfig->earlybombticklimit;

    pinfo->lastbombtime = 0;

}

local void p_shipitemschanged(Player *p, ShipHull *hull) {

    if (!p->arena || !hull)
        return; // Player somehow isn't in an arena yet, or doesn't have a hull.

    if (db->getPlayerCurrentHull(p) != hull)
        return; // The hull that changed wasn't their current hull.


    AConfig *aconfig = P_ARENA_DATA(p->arena, adkey);
    PInfo *pinfo = PPDATA(p, pdkey);

    if(!aconfig->attached)
        return; // Invalid config or an arena we're not attached to.

    pinfo->bombdelay = items->getPropertySumOnHull(p, hull, "bombdelay", aconfig->defbombdelay[hull->ship]);
    pinfo->bombdelay -= aconfig->earlybombticklimit;

    pinfo->lastbombtime = 0;

}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Adviser Stuff
//
////////////////////////////////////////////////////////////////////////////////////////////////////

local void edit_ppk(Player *p, struct C2SPosition *pos) {

    if(!p->arena)
        return; // Player somehow isn't in an arena yet.

    AConfig *aconfig = P_ARENA_DATA(p->arena, adkey);
    PInfo *pinfo = PPDATA(p, pdkey);

    u32 delay;

    if(!aconfig->attached)
        return; // Invalid config or an arena we're not attached to.

    if(pos->weapon.alternate)
        return; // Weapon is a mine or multifire.

    switch(pos->weapon.type) {
        case W_BOMB:
        case W_PROXBOMB:
            delay = pos->time - pinfo->lastbombtime;

            if(delay < pinfo->bombdelay) {
                lm->LogP(L_WARN, "antifastbomb", p, "Fast bomb detected. Min delay: %d, Last valid: %d, Diff: %d", pinfo->bombdelay, pinfo->lastbombtime, delay);
                pos->weapon.type = 0;
            } else {
                pinfo->lastbombtime = pos->time;
            }
    }
}

local Appk ppk_adviser = {
    ADVISER_HEAD_INIT(A_PPK)
    edit_ppk, NULL
};




// Support Functions
////////////////////////////////////////////////////////////////////////////////////////////////////

local void load_config(Arena *arena, AConfig *aconfig) {
    if(!arena || !aconfig || !aconfig->attached)
        return; // Invalid config or an arena we're not attached to.

    // Get default delay...
    for(int i = 0; i < 8; ++i)
        aconfig->defbombdelay[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "BombFireDelay", 0);

    /* cfghelp: Hyperspace:EarlyBombTickLimit, arena, int, def: 15, mod: hs_antifastbomb
     * Maximum amount of ticks a bomb is allowed to be early before it is thrown out. */
    aconfig->earlybombticklimit = cfg->GetInt(arena->cfg, "Hyperspace", "EarlyBombTickLimit", 15);
}


// Module nonsense.
////////////////////////////////////////////////////////////////////////////////////////////////////
EXPORT const char info_hs_antifastbomb[] = "v1.00 - Chris \"Ceiu\" Rog";

EXPORT int MM_hs_antifastbomb(int action, Imodman *_mm, Arena *arena) {

    AConfig *aconfig;

    switch(action) {
        case MM_LOAD:
            mm = _mm;

            // Register interfaces...
            aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
            cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
            lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
            pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
            game = mm->GetInterface(I_GAME, ALLARENAS);
            items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
            db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

            if (!aman || !cfg || !lm || !pd || !game || !items || !db) {
                mm->ReleaseInterface(aman);
                mm->ReleaseInterface(cfg);
                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(pd);
                mm->ReleaseInterface(game);
                mm->ReleaseInterface(items);
                mm->ReleaseInterface(db);

                return MM_FAIL;
            }

            // Arena/Player data allocation...
            adkey = aman->AllocateArenaData( sizeof(AConfig) );
            pdkey = pd->AllocatePlayerData( sizeof(PInfo) );
            if(adkey == -1 || pdkey == -1) return MM_FAIL;

            // YAY!
            return MM_OK;

    //////////////////////////////////////////////////

        case MM_UNLOAD:
            // Free allocated data...
            aman->FreeArenaData(adkey);
            pd->FreePlayerData(pdkey);

            // Release interfaces...
            mm->ReleaseInterface(aman);
            mm->ReleaseInterface(cfg);
            mm->ReleaseInterface(lm);
            mm->ReleaseInterface(pd);
            mm->ReleaseInterface(game);
            mm->ReleaseInterface(items);
            mm->ReleaseInterface(db);


            return MM_OK;

    //////////////////////////////////////////////////

        case MM_ATTACH:
            // load config and mark as attached...
            aconfig = P_ARENA_DATA(arena, adkey);

            if(aconfig) {
                aconfig->attached = 1;
                load_config(arena, aconfig);
            }

            // Register stuffs...
            mm->RegAdviser(&ppk_adviser, arena);

            mm->RegCallback(CB_ITEMS_CHANGED, p_shipitemschanged, arena);
            mm->RegCallback(CB_SHIPFREQCHANGE, p_shipfreqchange, arena);
            mm->RegCallback(CB_ARENAACTION, a_action, arena);

            return MM_OK;

    //////////////////////////////////////////////////

        case MM_DETACH:
            // Release stuffs...
            mm->UnregCallback(CB_ARENAACTION, a_action, arena);
            mm->UnregCallback(CB_SHIPFREQCHANGE, p_shipfreqchange, arena);
            mm->UnregCallback(CB_ITEMS_CHANGED, p_shipitemschanged, arena);

            mm->UnregAdviser(&ppk_adviser, arena);

            // Clear the "attached" flag...
            aconfig = P_ARENA_DATA(arena, adkey);
            if(aconfig) aconfig->attached = 0;

            return MM_OK;

    //////////////////////////////////////////////////

        //default:
            // watf?
    }

	return MM_FAIL;
}
