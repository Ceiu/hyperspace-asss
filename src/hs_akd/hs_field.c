#include "asss.h"

#define MODULENAME hs_field
#define SZMODULENAME "hs_field"
#define INTERFACENAME Ihs_field

#include "akd_asss.h"
#include "objects.h"
#include "fake.h"
#include "hscore.h"
#include "hscore_items.h"

#include <string.h>
#include <ctype.h>
#include <math.h>


//other interfaces we want to use besides the usual
local Iobjects *obj;
local Ifake *fake;
local Ihscoreitems *items;
//local Ihscoremoney *money;
//config values

//other globals
local pthread_mutex_t globalmutex;

#define ALLPLAYERS 0

#define UPPERLEFT 0
#define UPPERRIGHT 1
#define LOWERRIGHT 2
#define LOWERLEFT 3

#define FIELD_CALLBACK_ID 200
#define FIELD_CALLBACK_ID_MAX 208

#define HSFIELD_NAME_SIZE 32
typedef struct hs_field
{
        char name[32];          //name of this type of field
        struct Weapons wpn;     //type of weapon to throw at opponents
        int duration;           //how long the field will last
        short radius;           //how big the field will be
        short delay;            //how long between firing at people
        char event[16];         //event to call when this field is used
        int property;           //required property value to have to use this field
        int LVZIdBase[4];
        short nextLVZId[4];
        i8 maxLVZIds;
        i8 LVZSize;
        i8 track;
} hs_field;

typedef struct hs_field_instance
{
        Arena *a;
        Player *p;                      //creator
        Player *fake;           //the fake player firing the field
        hs_field *type;         //type of field
        ticks_t endTime;        //when to destroy this field
        short LVZIds[4];        //four corners
        short x, y;
} hs_field_instance;

const char *SHIP_NAMES[8] =
{
                "Warbird",
                "Javelin",
                "Spider",
                "Leviathan",
                "Terrier",
                "Weasel",
                "Lancaster",
                "Shark"
};


DEF_PARENA_TYPE
        LinkedList fields;                      //all of the loaded field types
        LinkedList fieldInstances;      //curent field instances in this arena
        int cfg_shipRadii[8];
        int attached : 2;
ENDDEF_PARENA_TYPE;

DEF_PPLAYER_TYPE
        u8 dead         : 1;    // If the player is dead... from dying.
        u8 buffer   : 7;    // Unused stuff.
        ticks_t lastField;  // Small addition by Spidernl, used for ?field cooldown
ENDDEF_PPLAYER_TYPE;

//prototype internal functions here.
typedef int (*hs_field_func)(LinkedList *, hs_field *, const void *extra);
local hs_field *hs_field_iterate(LinkedList *, hs_field_func, const void *extra);

local int getFieldByName(LinkedList *, hs_field *, const void *name);
local int getFieldByPropertyVal(LinkedList *, hs_field *, const void *val);
local int updateNextLVZId(LinkedList *, hs_field *, const void *array);

local int unloadFields(LinkedList *list, hs_field *data, const void *arena);

local void loadField(Arena *, const char *);


typedef int (*hs_field_instance_func)(LinkedList *, hs_field_instance *, const void *extra);
local hs_field_instance *hs_field_instance_iterate(LinkedList *, hs_field_instance_func, const void *extra);

local int getOneInstanceFromPlayer(LinkedList *, hs_field_instance *, const void *nothing);
local int removeAllInstancesFromPlayer(LinkedList *, hs_field_instance *, const void *p);

local void beginFieldInstance(Arena *a, Player *p, hs_field *type);
local void endFieldInstance(Arena *a, hs_field_instance *inst);

local int inSquare(Arena *, int ship, int sx, int sy, int r, int x, int y);

local helptext_t field_help =
"Targets: arena\n"
"Syntax:\n"
"  ?field [fieldtype]\n"
"Spawns an attack field around your ship of the specified name.\n"
"If you specify no name, ?field will pick a field you own.\n";
local void Cfield(const char *cmd, const char *params, Player *p, const Target *target);        //command prototype

// variable stuffs...
//local int player_info_key;

//callbacks
local void eventaction(Player *p, int eventID);
local void shipfreqchange(Player *, int newship, int oldship, int newfreq, int oldfreq);        //CB_SHIPFREQCHANGE callback prototype
local void playeraction(Player *, int action, Arena *);         //CB_PLAYERACTION callback prototype
local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);

//timers
local int fireExclamationPoint(void *a);
local int handle_respawn(void *param);

EXPORT const char info_hs_field[] = "v1.01 by Arnk Kilo Dylie <orbfighter@rshl.org>";
EXPORT int MM_hs_field(int action, Imodman *mm_, Arena *arena)
{
        BMM_FUNC_HEADER();

        if (action == MM_LOAD)
        {
                //store the provided Imodman interface.
                mm = mm_;

                //get all interfaces first. if a required interface is not available, jump to Lfailload and release the interfaces we did get, and return failure.
                GET_USUAL_INTERFACES(); //several interfaces used in many modules, there's no real harm in getting them even if they're not used
                GETINT(obj, I_OBJECTS);
                GETINT(fake, I_FAKE);
                GETINT(items, I_HSCORE_ITEMS);
                //GETINT(money, I_HSCORE_MONEY);

                //register per-arena and per-player data.
                BREG_PARENA_DATA();
                BREG_PPLAYER_DATA();

                //malloc and init anything else.

                //init a global mutex if you need one. you only need one if you have a global linkedlist, hashtable, or something cool like that.
                INIT_MUTEX(globalmutex);


                //finally, return success.
                return MM_OK;
        }
        else if (action == MM_UNLOAD)
        {
                //unregister all timers anyway because we're cool.
                ml->ClearTimer(fireExclamationPoint, 0);
                ml->ClearTimer(handle_respawn, 0);

                //clear the mutex if we were using it
                DESTROY_MUTEX(globalmutex);

                //free any other malloced data

                //unregister per-arena and per-player data
                UNREG_PARENA_DATA();
                UNREG_PPLAYER_DATA();

                //release interfaces last.
                //this is where GETINT jumps to if it fails.
Lfailload:
                RELEASE_USUAL_INTERFACES();
                RELEASEINT(obj);
                RELEASEINT(fake);
                RELEASEINT(items);
                //RELEASEINT(money);

                //returns MM_FAIL if we jumped from a failed GETINT or other MM_LOAD action, returns MM_OK if not.
                DO_RETURN();
        }


        else if (action == MM_ATTACH)
        {
                const char *str;
                const char *temp = 0;
                char buffer[512];
                int i;

                //malloc other things in arena data.
                LLInit(&ad->fieldInstances);
                LLInit(&ad->fields);

                for (i = 0; i < 8; ++i)
                {
                        ad->cfg_shipRadii[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "radius", 14);
                        if (!ad->cfg_shipRadii[i])
                                ad->cfg_shipRadii[i] = 14;
                }

                str = cfg->GetStr(arena->cfg, "hs_field", "fields");
                if (str)
                {
                        while (strsplit(str, " ,\n\t", buffer, 511, &temp))
                        {
                                loadField(arena, buffer);
                        }
                }

                //register commands, timers, and callbacks.
                mm->RegCallback(CB_EVENT_ACTION, eventaction, arena);
                mm->RegCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
                mm->RegCallback(CB_PLAYERACTION, playeraction, arena);
                mm->RegCallback(CB_KILL, playerkill, arena);

                cmd->AddCommand("field", Cfield, arena, field_help);

                ad->attached = 1;
                //finally, return success.
                return MM_OK;
        }
        else if (action == MM_DETACH)
        {
                //unregister global commands, timers, and callbacks.
                //remember to clear ALL timers this arena was using even if they were not set in the MM_ATTACH phase..
                ad->attached = 0;

                cmd->RemoveCommand("field", Cfield, arena);

                mm->UnregCallback(CB_KILL, playerkill, arena);
                mm->UnregCallback(CB_EVENT_ACTION, eventaction, arena);
                mm->UnregCallback(CB_SHIPFREQCHANGE, shipfreqchange, arena);
                mm->UnregCallback(CB_PLAYERACTION, playeraction, arena);

                //free other things in arena data.
                hs_field_instance_iterate(&ad->fieldInstances, removeAllInstancesFromPlayer, 0);
                hs_field_iterate(&ad->fields, unloadFields, 0);

                LLEmpty(&ad->fieldInstances);
                LLEmpty(&ad->fields);

//Lfailattach:

                //returns MM_FAIL if we jumped from a failed GETARENAINT or other MM_ATTACH action, returns MM_OK if not.
                DO_RETURN();
        }


        return MM_FAIL;
}

local hs_field *hs_field_iterate(LinkedList *list, hs_field_func func, const void *extra)
{
        Link *link;
        hs_field *result = 0;
        hs_field *data;

        MYGLOCK;
        FOR_EACH(list, data, link)
        {
                int found;
                found = func(list, data, extra);
                if (found)
                {
                        result = data;
                        break;
                }
        }
        MYGUNLOCK;
        return result;
}

local int getFieldByName(LinkedList *list, hs_field *data, const void *extra)
{
        char *name = (char *)extra;
        if (!strcasecmp(data->name, name))
                return 1;
        return 0;
}

local int getFieldByPropertyVal(LinkedList *list, hs_field *data, const void *extra)
{
        int *pVal = (int *)extra;

        if (data->property == *pVal)
                return 1;

        return 0;
}

local int updateNextLVZId(LinkedList *list, hs_field *type, const void *array)
{
        int *LVZId = (int *)array;
        int i;

        for (i = 0; i < 4; ++i)
        {
                if (type->LVZIdBase[i] != LVZId[i])
                        continue;

                ++type->nextLVZId[i];

                if (type->nextLVZId[i] >= type->LVZIdBase[i] + type->maxLVZIds)
                        type->nextLVZId[i] = type->LVZIdBase[i];
        }

        return 0;
}

local int unloadFields(LinkedList *list, hs_field *data, const void *arena)
{
        afree(data);
        return 0;
}

local void loadField(Arena *a, const char *cfgname)
{
        BDEF_AD(a);
        char buffer[256];
        const char *str;
        int i;
        hs_field *newField = amalloc(sizeof(hs_field));
        sprintf(buffer, "field-%s", cfgname);

        newField->delay = cfg->GetInt(a->cfg, buffer, "firedelay", 50);
        newField->duration = cfg->GetInt(a->cfg, buffer, "duration", 1000);
        newField->LVZIdBase[UPPERLEFT] = cfg->GetInt(a->cfg, buffer, "lvzidbase-ul", 0);
        newField->LVZIdBase[UPPERRIGHT] = cfg->GetInt(a->cfg, buffer, "lvzidbase-ur", 0);
        newField->LVZIdBase[LOWERRIGHT] = cfg->GetInt(a->cfg, buffer, "lvzidbase-lr", 0);
        newField->LVZIdBase[LOWERLEFT] = cfg->GetInt(a->cfg, buffer, "lvzidbase-ll", 0);
        for (i = 0; i < 4; ++i)
                newField->nextLVZId[i] = newField->LVZIdBase[i];

        newField->LVZSize = cfg->GetInt(a->cfg, buffer, "lvzsize", 32);
        newField->maxLVZIds = cfg->GetInt(a->cfg, buffer, "maxlvzids", 20);
        newField->track = cfg->GetInt(a->cfg, buffer, "track", 0);

        str = cfg->GetStr(a->cfg, buffer, "name");
        if (str)
        {
                astrncpy(newField->name, str, HSFIELD_NAME_SIZE);
        }
        else
        {
                lm->LogA(L_ERROR, "hs_field", a, "loaded field with unknown name (%s)", cfgname);
                astrncpy(newField->name, "unknown", HSFIELD_NAME_SIZE);
        }

        str = cfg->GetStr(a->cfg, buffer, "event");
        if (str)
        {
                astrncpy(newField->event, str, 16);
        }
        else
        {
                lm->LogA(L_ERROR, "hs_field", a, "loaded field with unknown event (%s)", cfgname);
                astrncpy(newField->event, cfgname, 16);
        }

        newField->property = cfg->GetInt(a->cfg, buffer, "property", 1);

        newField->radius = cfg->GetInt(a->cfg, buffer, "radius", 64);

        str = cfg->GetStr(a->cfg, buffer, "weapon");
        newField->wpn.alternate = 0;
        newField->wpn.level = 0;
        newField->wpn.shrap = 0;
        newField->wpn.shrapbouncing = 0;
        newField->wpn.shraplevel = 0;
        newField->wpn.type = W_BULLET;
        if (str)
        {
                if (strstr(str, "level2"))
                        newField->wpn.level = 1;
                else if (strstr(str, "level3"))
                        newField->wpn.level = 2;
                else if (strstr(str, "level4"))
                        newField->wpn.level = 3;

                if (strstr(str, "gun"))
                {
                        if (strstr(str, "bounce"))
                                newField->wpn.type = W_BOUNCEBULLET;

                        if (strstr(str, "multi"))
                                newField->wpn.alternate = 1;
                }
                else if (strstr(str, "bomb"))
                {
                        if (strstr(str, "thor"))
                                newField->wpn.type = W_THOR;
                        else if (strstr(str, "prox"))
                                newField->wpn.type = W_PROXBOMB;
                        else
                                newField->wpn.type = W_BOMB;

                        if (strstr(str, "shrap"))
                        {
                                newField->wpn.shrap = 31;

                                if (strstr(str, "shrap2"))
                                        newField->wpn.shraplevel = 1;
                                else if (strstr(str, "shrap3"))
                                        newField->wpn.shraplevel = 2;
                                else if (strstr(str, "shrap4"))
                                        newField->wpn.shraplevel = 3;

                                if (strstr(str, "bounce"))
                                        newField->wpn.shrapbouncing = 1;
                        }
                }
                else if (strstr(str, "repel"))
                {
                        newField->wpn.type = W_REPEL;
                }
                else if (strstr(str, "burst"))
                {
                        newField->wpn.type = W_BURST;
                }
        }

        MYGLOCK;
        LLAdd(&ad->fields, newField);
        MYGUNLOCK;
        lm->LogA(L_DRIVEL, "hs_field", a, "added field %s (%s)", cfgname, newField->name);

}

local hs_field_instance *hs_field_instance_iterate(LinkedList *list, hs_field_instance_func func, const void *extra)
{
        Link *link;
        hs_field_instance *result = 0;
        hs_field_instance *data;

        MYGLOCK;
        FOR_EACH(list, data, link)
        {
                int found;
                found = func(list, data, extra);
                if (found)
                {
                        result = data;
                        break;
                }
        }
        MYGUNLOCK;
        return result;
}

local void beginFieldInstance(Arena *a, Player *p, hs_field *type)
{
        BDEF_AD(a);

        int i;
        char nameBuffer[24];
        hs_field_instance *newInst = amalloc(sizeof(hs_field_instance));
        DEF_T_A(a);

        snprintf(nameBuffer, sizeof(nameBuffer) - 1, "<%i-%.17s>", p->pid, type->name);
        nameBuffer[sizeof(nameBuffer) - 1] = 0;

        for (i = 1; i < sizeof(nameBuffer); ++i)
                nameBuffer[i] = tolower(nameBuffer[i]);

        newInst->fake = fake->CreateFakePlayer(nameBuffer, p->arena, SHIP_SHARK, p->p_freq);
        //money->setExp(newInst->fake, money->getExp(p));

        newInst->p = p;
        newInst->a = a;
        newInst->type = type;
        newInst->endTime = current_ticks() + type->duration;
        newInst->x = p->position.x;
        newInst->y = p->position.y;

        if (*type->event)
                items->triggerEvent(p, p->p_ship, type->event);

        MYGLOCK;
        for (i = 0; i < 4; ++i)
        {
                newInst->LVZIds[i] = type->nextLVZId[i];

                obj->Toggle(&t, newInst->LVZIds[i], 1);

                switch (i)
                {
                case UPPERLEFT:
                        obj->Move(&t, newInst->LVZIds[i], newInst->x - type->radius, newInst->y - type->radius, 0, 0);
                        break;
                case UPPERRIGHT:
                        obj->Move(&t, newInst->LVZIds[i], newInst->x + type->radius - type->LVZSize, newInst->y - type->radius, 0, 0);
                        break;
                case LOWERRIGHT:
                        obj->Move(&t, newInst->LVZIds[i], newInst->x + type->radius - type->LVZSize, newInst->y + type->radius - type->LVZSize, 0, 0);
                        break;
                case LOWERLEFT:
                        obj->Move(&t, newInst->LVZIds[i], newInst->x - type->radius, newInst->y + type->radius - type->LVZSize, 0, 0);
                        break;
                }
        }

        hs_field_iterate(&ad->fields, updateNextLVZId, type->LVZIdBase);

        LLAdd(&ad->fieldInstances, newInst);
        MYGUNLOCK;

        ml->SetTimer(fireExclamationPoint, type->delay, type->delay, newInst, newInst);
}

local void endFieldInstance(Arena *a, hs_field_instance *inst)
{
        BDEF_AD(a);
        short ids[4] = {inst->LVZIds[0], inst->LVZIds[1], inst->LVZIds[2], inst->LVZIds[3]};
        char ons[4] = {0,0,0,0};
        DEF_T_A(a);

        ml->ClearTimer(fireExclamationPoint, inst);

        obj->ToggleSet(&t, ids, ons, 4);

        lm->LogA(L_DRIVEL, "hs_field", a, "destroyed instance %s", inst->fake->name);
        fake->EndFaked(inst->fake);



        MYGLOCK;
        LLRemove(&ad->fieldInstances, inst);
        MYGUNLOCK;
        afree(inst);
}

local int inSquare(Arena *a, int ship, int sx, int sy, int r, int x, int y)
{
        BDEF_AD(a);
        int R;
        if (ship < SHIP_WARBIRD || ship > SHIP_SHARK)
                return 0;
        R = ad->cfg_shipRadii[ship];

        if ((x + R) < (sx - r))
                return 0;
        if ((x - R) > (sx + r))
                return 0;
        if ((y + R) < (sy - r))
                return 0;
        if ((y - R) > (sy + r))
                return 0;

        return 1;
}

local int getOneInstanceFromPlayer(LinkedList *list, hs_field_instance *data, const void *player)
{
        if (data->p == player)
                return 1;
        return 0;
}

local int removeAllInstancesFromPlayer(LinkedList *list, hs_field_instance *data, const void *player)
{
        if (player && (data->p != player))
                return 0;

        endFieldInstance(data->a, data);

        return 0;
}

local int determineRotation(int xspeed, int yspeed)
{
        yspeed *= -1;

        if (!xspeed)
        {
                if (yspeed >= 0)
                        return 0;
                else
                        return 20;
        }
        else if (!yspeed)
        {
                if (xspeed >= 0)
                        return 10;
                else
                        return 30;
        }
        else
        {
                double degrees;
                int ssru;
                double theta = -atan((double)yspeed / (double)xspeed);
                theta += (M_PI / 2);

                //add pi (180 d) when x is negative
                if (xspeed < 0)
                {
                        theta += M_PI;
                }

                degrees = theta * 57.2957795130823;
                ssru = (int)(degrees / 9.0);

                return ssru;
        }

        return 0;
}

local int randomRotation()
{
        return prng->Number(0,39);
}

local void fireWeapon(Player *victim, hs_field_instance *inst)
{
        unsigned status = STATUS_STEALTH | STATUS_CLOAK | STATUS_UFO;

        struct S2CWeapons packet = {
                S2C_WEAPON, victim->position.rotation, current_ticks() & 0xFFFF, victim->position.x, victim->position.yspeed,
                inst->fake->pid, victim->position.xspeed, 0, status, 0,
                victim->position.y, 10 /*bounty*/
        };

        if (!inst->type->track)
        {
                packet.rotation = randomRotation();
        }
        else
        {
                packet.rotation = determineRotation(packet.xspeed, packet.yspeed);
        }


        inst->fake->position.x = packet.x;
        inst->fake->position.y = packet.y;

        packet.weapon = inst->type->wpn;

        game->DoWeaponChecksum(&packet);

        net->SendToOne(victim, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);

        //now let's see if we can clear our position out so people don't hit the field fake with their own weapons.
        packet.x = 0;
        packet.y = 0;
        packet.xspeed = 0;
        packet.yspeed = 0;
        packet.rotation = 0;
        packet.weapon.type = W_NULL;
        packet.time = TICK_MAKE(packet.time + 1) & 0xffff;
        inst->fake->position.x = 0;
        inst->fake->position.y = 0;
        game->DoWeaponChecksum(&packet);
        net->SendToOne(victim, (byte*)&packet, sizeof(struct S2CWeapons) - sizeof(struct ExtraPosData), NET_RELIABLE);
}

local int fireExclamationPoint(void *param)
{
        hs_field_instance *inst = (hs_field_instance *)param;
        Link *link;
        Player *p;

        if (current_ticks() > inst->endTime)
        {
                endFieldInstance(inst->a, inst);
                return 0;
        }

        PDLOCK;
        FOR_EACH_PLAYER(p)
        {
                if (!IS_IN(p, inst->a))
                        continue;
                if (IS_SPEC(p))
                        continue;
                if (IS_ON_FREQ(p, inst->a, inst->p->pkt.freq))
                        continue;
				if (p->flags.is_dead)
						continue;
                if (inSquare(p->arena, p->p_ship, inst->x, inst->y, inst->type->radius, p->position.x, p->position.y))
                {
                        fireWeapon(p, inst);
                }
        }
        PDUNLOCK;

        return 1;
}

local void shipfreqchange(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
        BDEF_AD(p->arena);

        hs_field_instance_iterate(&ad->fieldInstances, removeAllInstancesFromPlayer, p);
}

local void playeraction(Player *p, int action, Arena *a)
{
        BDEF_PD(p);

        if ((p->type != T_VIE) && (p->type != T_CONT))
                return;

        //actions applying to an arena.
        if (a)
        {
                BDEF_AD(a);

                if (action == PA_ENTERARENA)
                {
                        pdat->dead = 0;
                        pdat->lastField = 0;
                }
                else if (action == PA_LEAVEARENA)
                {
                        //generic cleanup...
                        ml->ClearTimer(handle_respawn, p);
                        hs_field_instance_iterate(&ad->fieldInstances, removeAllInstancesFromPlayer, p);
                }
        }
}

local void eventaction(Player *p, int eventID)
{
        BDEF_AD(p->arena);
        int propertyVal;
        hs_field *type = 0;
        hs_field_instance *prev;

        if (IS_SPEC(p))
                return;

        if ((eventID < FIELD_CALLBACK_ID) || (eventID > FIELD_CALLBACK_ID_MAX))
                return;

        prev = hs_field_instance_iterate(&ad->fieldInstances, getOneInstanceFromPlayer, p);
        if (prev)
        {
                chat->SendMessage(p, "You may only launch one Attack Field at a time!");
                return;
        }

        propertyVal = eventID - FIELD_CALLBACK_ID;

        type = hs_field_iterate(&ad->fields, getFieldByPropertyVal, &propertyVal);

        if (type)
        {
                beginFieldInstance(p->arena, p, type);
                chat->SendMessage(p, "%s Field created.", type->name);
        }
        else
        {
                lm->Log(L_ERROR, "eventID produced nonexistant attack-field-property %i", propertyVal);
                return;
        }
}

local int handle_respawn(void *_p)
{
        Player *p = (Player *)_p;
        BDEF_PD(p);
        BDEF_AD(p->arena);
        if (!p->arena)
                return 0;

        hs_field_instance_iterate(&ad->fieldInstances, removeAllInstancesFromPlayer, p);

    pdat->dead = 0;
    return 0;
}

local void playerkill(Arena *a, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
    int intEnterDelay = cfg->GetInt(killed->arena->cfg, "Kill", "EnterDelay", 0);

    if (intEnterDelay > 0)
    {
        BDEF_PD(killed);

        pdat->dead = 1;
        ml->SetTimer(handle_respawn, intEnterDelay + 100, 0, killed, killed);
    }
}

local void Cfield(const char *cmd, const char *params, Player *p, const Target *target)
{
        BDEF_AD(p->arena);
        BDEF_PD(p);
        int propertyVal;
        int sum;

        hs_field *type = NULL;
        hs_field_instance *prev;

        if (IS_SPEC(p))
                return;

        if (!items->getPropertySum(p, p->p_ship, "fieldlauncher", 0))
        {
                chat->SendMessage(p, "You need a Field Launcher to use Attack Fields!");
                return;
        }

        if (pdat->dead)
        {
                chat->SendMessage(p, "You cannot launch a field while you're dead!");
                return;
        }

        if (pdat->lastField != 0 && TICK_DIFF(current_ticks(), pdat->lastField) <
        items->getPropertySum(p, p->p_ship, "fielddelay", 0))
        {
                chat->SendMessage(p, "Your Field Launcher is currently recharging!");
                return;
        }

        prev = hs_field_instance_iterate(&ad->fieldInstances, getOneInstanceFromPlayer, p);
        if (prev)
        {
                chat->SendMessage(p, "You may only launch one Attack Field at a time!");
                return;
        }

        if (*params)
                type = hs_field_iterate(&ad->fields, getFieldByName, params);

        if (type)
        {
                if (items->getPropertySum(p, p->p_ship, "field", 0) & type->property)
                {
                        pdat->lastField = current_ticks();
                        beginFieldInstance(p->arena, p, type);
                        chat->SendMessage(p, "%s Field created.", type->name);
                }
                else
                {
                        chat->SendMessage(p, "You do not have that type of Field available.");
                        return;
                }
        }
        else if (!*params)
        {
                sum = items->getPropertySum(p, p->p_ship, "field", 0);
                if (sum)
                {
                        int i;
                        for (i = 1; i <= sum; i *= 2)
                        {
                                if (sum & i)
                                {
                                        propertyVal = i;
                                        type = hs_field_iterate(&ad->fields, getFieldByPropertyVal, &propertyVal);
                                        if (type)
                                        {
                                                pdat->lastField = current_ticks();
                                                beginFieldInstance(p->arena, p, type);
                                                chat->SendMessage(p, "%s Field created.", type->name);
                                                return;
                                        }
                                }
                        }
                        lm->LogP(L_ERROR, "hs_field", p, "somehow failed to get field even though the property value is non zero!");
                }
                else
                {
                        chat->SendMessage(p, "You do not have any Fields!");
                        return;
                }
        }
        else
        {
                chat->SendMessage(p, "That type of Field does not exist.");
                return;
        }
}
