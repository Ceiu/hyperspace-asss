#include <math.h>

#include "asss.h"
#include "hscore.h"
#include "fake.h"
#include "packets/ppk.h"

// Event IDs...
#define ECB_VENGEANCE 150

// Modules...
local Iarenaman *aman;
local Iconfig *cfg;
local Ichat *chat;
local Ifake *fake;
local Ilogman *lm;
local Imainloop *ml;
local Imodman *mm;
local Inet *net;
local Iobjects *obj;
local Iplayerdata *pd;
local Igame *game;

typedef struct Weapons WeaponInfo;
typedef struct S2CWeapons S2CWeaponPkt;


// Callbacks!
local void event_callback(Player *player, int intEventId);
local void arena_callback(Arena *arena, int intActionId);
local void player_action_callback(Player *player, int action, Arena *arena);

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Relic of Vengeance/Cobalt Warhead mk2 Stuff
//
////////////////////////////////////////////////////////////////////////////////////////////////////

#define ROV_FAKE_NAME "<Relic-Vengeance>"


local int rov_adkey;            // Arena data key

typedef struct
{
    int     attached;           // If the module is attached to the arena

    int     explode_delay;      // Delay between death and explosion (centiseconds)
    int     explode_radius;     // Explosion radius (pixels)

    int     lvz_obj_id;         // lvz object id to use
    int     lvz_img_id;         // lvz image id to use
    int     lvz_img_width;      // lvz size (pixels)
    int     lvz_img_height;     // ^

    Player  *fake;              // The fake player we use to explode people
    Target  target;             // The arena target we send lvz stuff to
} VengeanceCfg;

typedef struct
{
    Arena   *arena;     // The arena where the player blow'd up (incase they leave or something)
    Player  *player;    // The player who blow'd up.

    int     x;          // The location of their death, in pixels.
    int     y;          // ^
} VengeanceInfo;

local int timer_vengeance(void *objPlayer);
local void load_vengeance_config(Arena *arena, VengeanceCfg *objVengeanceCfg);
local void load_vengeance_fake(Arena *arena);
local void unload_vengeance_fake(Arena *arena);


////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Event callback!!
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/** Events triggered by item callbacks start here. The event id needs to be
    the same as what is defined in the item info.
 */
local void event_callback(Player *player, int intEventId)
{
    VengeanceCfg *objVengeanceCfg;
    VengeanceInfo *objVengeanceInfo;

    switch(intEventId)
    {
        case ECB_VENGEANCE:
            objVengeanceCfg = P_ARENA_DATA(player->arena, rov_adkey);
            objVengeanceInfo = amalloc( sizeof(VengeanceInfo) );

            // Send LVZ stuffs...
            if(objVengeanceCfg->lvz_obj_id)
            {
                //lm->Log(L_ERROR, "death coords: (%d, %d), lvz size: %d x %d, offset: %d, %d", player->position.x, player->position.y, objVengeanceCfg->lvz_img_width, objVengeanceCfg->lvz_img_height, (objVengeanceCfg->lvz_img_width >> 1), (objVengeanceCfg->lvz_img_height >> 1));

                obj->Move(&(objVengeanceCfg->target), objVengeanceCfg->lvz_obj_id, (player->position.x - (objVengeanceCfg->lvz_img_width >> 1)), (player->position.y - (objVengeanceCfg->lvz_img_height >> 1)), 0, 0);
                obj->Image(&(objVengeanceCfg->target), objVengeanceCfg->lvz_obj_id, objVengeanceCfg->lvz_img_id);
                obj->Toggle(&(objVengeanceCfg->target), objVengeanceCfg->lvz_obj_id, 1);
            }

            // Store player/position info...
            if(objVengeanceInfo && objVengeanceCfg && objVengeanceCfg->fake)
            {
                objVengeanceInfo->arena = player->arena;
                objVengeanceInfo->player = player;
                objVengeanceInfo->x = player->position.x;
                objVengeanceInfo->y = player->position.y;

                // Set timer for DOOOOOOOOOOOOM
                ml->SetTimer(timer_vengeance, objVengeanceCfg->explode_delay, 0, objVengeanceInfo, 0);
            }

            break;


        //default:
            // Do nothing.
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Arena callbacks!!
//
////////////////////////////////////////////////////////////////////////////////////////////////////

/** Arena-specific configuration is loaded here. Wheeee...
 */
local void arena_callback(Arena *arena, int action)
{
    VengeanceCfg *objVengeanceCfg;

    switch(action)
    {
        case AA_CREATE:
        case AA_CONFCHANGED:

            objVengeanceCfg = P_ARENA_DATA(arena, rov_adkey);

            if(objVengeanceCfg && objVengeanceCfg->attached)
                load_vengeance_config(arena, objVengeanceCfg);

            break;

    //////////////////////////////////////////////////

        case AA_DESTROY:

            // Unload Relic of Vengeance stuffs...
            objVengeanceCfg = P_ARENA_DATA(arena, rov_adkey);

            if(objVengeanceCfg && objVengeanceCfg->fake)
                if(fake->EndFaked(objVengeanceCfg->fake))
                    objVengeanceCfg->fake = 0;

            break;


        //default:
            // do nothing.
    }
}

local void player_action_callback(Player *player, int action, Arena *arena)
{
    Link *link;
    int player_count = 0;

    switch(action)
    {
        case PA_LEAVEARENA:
            player_count = -1; // Player hasn't been removed from the arena yet?

        case PA_ENTERARENA:
            pd->WriteLock();
            FOR_EACH_PLAYER(player)
                if(player->arena == arena && IS_HUMAN(player))
                    ++player_count;
            pd->WriteUnlock();

            if(player_count > 0)
                load_vengeance_fake(arena);
            else
                unload_vengeance_fake(arena);

            break;


        //default:
            // do nothing!
    }

}


/** Used with relic of veneance/cobalt warhead mk2 to make players go boom after some time.
 */
local int timer_vengeance(void *objParam)
{
    Link *link;
    Player *player;
    VengeanceCfg *objVengeanceCfg;
    VengeanceInfo *objVengeanceInfo = objParam;

    S2CWeaponPkt packet;
    WeaponInfo weapon;

    // Validation!
    if(!objVengeanceInfo)
        return FALSE; // Something went horribly wrong here...

    objVengeanceCfg = P_ARENA_DATA(objVengeanceInfo->arena, rov_adkey);

    if(objVengeanceCfg && objVengeanceCfg->fake)
    {
        // Constant stuff...
        weapon.type = W_THOR;
        weapon.level = 3;

        packet.type = S2C_WEAPON;
        packet.status = STATUS_UFO | STATUS_CLOAK | STATUS_STEALTH;
        packet.weapon = weapon;
        packet.playerid = objVengeanceCfg->fake->pid;

        // Everyone within the blast radius gets ufo'd uberthor to the face
        pd->WriteLock();
        FOR_EACH_PLAYER(player)
        {
            if(player == objVengeanceInfo->player || player->arena != objVengeanceInfo->arena)
                continue; // Not our arena. Next plz.

            if(!IS_HUMAN(player) || player->pkt.ship == SHIP_SPEC)
                continue; // In spec or not human...

            int dist = sqrt(pow(player->position.x - objVengeanceInfo->x, 2) + pow(player->position.y - objVengeanceInfo->y, 2));

            if(dist <= objVengeanceCfg->explode_radius)
            {
                packet.time = current_ticks() & 0xFFFF;
                packet.rotation = player->position.rotation;
                packet.x = player->position.x;
                packet.y = player->position.y;
                packet.xspeed = player->position.xspeed;
                packet.yspeed = player->position.yspeed;

                game->DoWeaponChecksum(&packet);

                net->SendToOne(player, (byte*)&packet, sizeof(S2CWeaponPkt) - sizeof(struct ExtraPosData), NET_RELIABLE);
            }
        }
        pd->WriteUnlock();
    }

    // Free up our storage stuffs...
    afree(objVengeanceInfo);

    return FALSE;
}




// Support Functions
////////////////////////////////////////////////////////////////////////////////////////////////////

local void load_vengeance_config(Arena *arena, VengeanceCfg *objVengeanceCfg)
{
    if(objVengeanceCfg)
    {
        /* cfghelp: Relics:VengeanceExplodeDelay, arena, int, def: 300, mod: hs_relics
         * Amount of time between the player's death and the earth-shattering kaboom (centiseconds). */
        objVengeanceCfg->explode_delay = cfg->GetInt(arena->cfg, "Relics", "VengeanceExplodeDelay", 300);
        /* cfghelp: Relics:VengeanceExplodeRadius, arena, int, def: 400, mod: hs_relics
         * Radius of the vengeance explosion (pixels). */
        objVengeanceCfg->explode_radius = cfg->GetInt(arena->cfg, "Relics", "VengeanceExplodeRadius", 400);

        /* cfghelp: Relics:VengeanceObjectId, arena, int, def: 0, mod: hs_relics
         * LVZ object id to use for a warning graphic when the player dies. */
        objVengeanceCfg->lvz_obj_id = cfg->GetInt(arena->cfg, "Relics", "VengeanceObjectId", 0);
        /* cfghelp: Relics:VengeanceImageId, arena, int, def: 0, mod: hs_relics
         * LVZ Image id to use for the warning graphic. */
        objVengeanceCfg->lvz_img_id = cfg->GetInt(arena->cfg, "Relics", "VengeanceImageId", 0);
        /* cfghelp: Relics:VengeanceImageWidth, arena, int, def: 0, mod: hs_relics
         * Width of the warning graphic (pixels). */
        objVengeanceCfg->lvz_img_width = cfg->GetInt(arena->cfg, "Relics", "VengeanceImageWidth", 0);
        /* cfghelp: Relics:VengeanceImageHeight, arena, int, def: 0, mod: hs_relics
         * Height of the warning graphc (pixels). */
        objVengeanceCfg->lvz_img_height = cfg->GetInt(arena->cfg, "Relics", "VengeanceImageHeight", 0);


        objVengeanceCfg->target.type = T_ARENA;
        objVengeanceCfg->target.u.arena = arena;
    }
}

local void load_vengeance_fake(Arena *arena)
{
    const char *fake_name;

    // Validation
    if(!arena)
        return; // Uhh?

    // Is this an arena we care about?
    VengeanceCfg *objVengeanceCfg = P_ARENA_DATA(arena, rov_adkey);
    if(!objVengeanceCfg || !objVengeanceCfg->attached)
        return; // Nope.

    // Load!
    if(!objVengeanceCfg->fake)
    {
        /* cfghelp: Relics:VengeanceFakeName, arena, str, def: <Relic: Vengeance>, mod: hs_relics
         * The name of the fake player the Relic of Vengeance uses. If unset, the module will use the default. */
        fake_name = cfg->GetStr(arena->cfg, "Relics", "VengeanceFakeName");

        objVengeanceCfg->fake = fake->CreateFakePlayer((fake_name ? fake_name : ROV_FAKE_NAME), arena, 0, 9999);
    }
}

local void unload_vengeance_fake(Arena *arena)
{
    // Validation
    if(!arena)
        return; // Uhh?

    // Is this an arena we care about?
    VengeanceCfg *objVengeanceCfg = P_ARENA_DATA(arena, rov_adkey);
    if(!objVengeanceCfg || !objVengeanceCfg->attached)
        return; // Nope.

    // Unload!
    if(objVengeanceCfg->fake && fake->EndFaked(objVengeanceCfg->fake))
        objVengeanceCfg->fake = 0;
}


// Module nonsense.
////////////////////////////////////////////////////////////////////////////////////////////////////
EXPORT const char info_hs_relics[] = "v1.00 - Chris \"Cerium\" Rog";

EXPORT int MM_hs_relics(int action, Imodman *_mm, Arena *arena)
{
    Link *link;
    VengeanceCfg *objVengeanceCfg;

    switch(action)
    {
        case MM_LOAD:
            mm = _mm;

            // Register interfaces...
            aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
            cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
            chat = mm->GetInterface(I_CHAT, ALLARENAS);
            fake = mm->GetInterface(I_FAKE, ALLARENAS);
            lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
            ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
            net = mm->GetInterface(I_NET, ALLARENAS);
            obj = mm->GetInterface(I_OBJECTS, ALLARENAS);
            pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
            game = mm->GetInterface(I_GAME, ALLARENAS);

            if (!aman || !cfg || !chat || !fake || !lm || !ml || !net || !obj || !pd || !game)
            {
                mm->ReleaseInterface(aman);
                mm->ReleaseInterface(cfg);
                mm->ReleaseInterface(chat);
                mm->ReleaseInterface(fake);
                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(ml);
                mm->ReleaseInterface(net);
                mm->ReleaseInterface(obj);
                mm->ReleaseInterface(pd);
                mm->ReleaseInterface(game);

                return MM_FAIL;
            }

            // Arena/Player data allocation...
            rov_adkey = aman->AllocateArenaData( sizeof(VengeanceCfg) );
            if(rov_adkey == -1) return MM_FAIL;

            // Callbacks...
            mm->RegCallback(CB_EVENT_ACTION, event_callback, ALLARENAS);
            mm->RegCallback(CB_ARENAACTION, arena_callback, ALLARENAS);
            mm->RegCallback(CB_PLAYERACTION, player_action_callback, ALLARENAS);

            // YAY!
            return MM_OK;

    //////////////////////////////////////////////////

        case MM_UNLOAD:
            // Clear timers...
            ml->ClearTimer(timer_vengeance, 0);

            // Release callbacks...
            mm->UnregCallback(CB_EVENT_ACTION, event_callback, ALLARENAS);
            mm->UnregCallback(CB_ARENAACTION, arena_callback, ALLARENAS);
            mm->UnregCallback(CB_PLAYERACTION, player_action_callback, ALLARENAS);

            // Release fake players...
            FOR_EACH_ARENA_P(arena, objVengeanceCfg, rov_adkey)
                if(objVengeanceCfg->fake && !fake->EndFaked(objVengeanceCfg->fake))
                    lm->Log(L_ERROR, "Unable to destroy fake player in %s", arena->name);

            // Free allocated data...
            aman->FreeArenaData(rov_adkey);

            // Release interfaces...
            mm->ReleaseInterface(aman);
            mm->ReleaseInterface(cfg);
            mm->ReleaseInterface(chat);
            mm->ReleaseInterface(fake);
            mm->ReleaseInterface(lm);
            mm->ReleaseInterface(ml);
            mm->ReleaseInterface(net);
            mm->ReleaseInterface(obj);
            mm->ReleaseInterface(pd);
            mm->ReleaseInterface(game);


            return MM_OK;

    //////////////////////////////////////////////////

        case MM_ATTACH:

            // load config and mark as attached...
            objVengeanceCfg = P_ARENA_DATA(arena, rov_adkey);

            if(objVengeanceCfg)
            {
                load_vengeance_config(arena, objVengeanceCfg);
                objVengeanceCfg->attached = 1;

                load_vengeance_fake(arena);
            }

            return MM_OK;

    //////////////////////////////////////////////////

        case MM_DETACH:

            // Kill fake player if it exists and remove the 'attached' status...
            unload_vengeance_fake(arena);

            // Clear the "attached" flag...
            objVengeanceCfg = P_ARENA_DATA(arena, rov_adkey);
            if(objVengeanceCfg) objVengeanceCfg->attached = 0;

            return MM_OK;

    //////////////////////////////////////////////////

        //default:
            // watf?
    }

	return MM_FAIL;
}
