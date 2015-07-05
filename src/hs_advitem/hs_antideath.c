#include "asss.h"
#include "hscore.h"
#include "hscore_spawner.h"

#define ANTIDEATH_EVENT_ID 101

typedef struct pdata
{
        ticks_t last_ad;
} pdata;

local Imodman *mm;
local Ilogman *lm;
local Inet *net;
local Ihscoreitems *items;
local Iconfig *cfg;
local Imainloop *ml;
local Iplayerdata *pd;
local Iflagcore *fc;
local Igame *game;
local Ichat *chat;

local int pdkey;

local int timer_callback(void *clos)
{
        Player *p = clos;
        pdata *pdata = PPDATA(p, pdkey);

        Target t;
        //Ihscorespawner *spawner;
        t.type = T_PLAYER;
        t.u.p = p;

        if(!fc->CountPlayerFlags(p))
        {
                game->ShipReset(&t);

				/*
                spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
                if (spawner)
                {
                        spawner->respawn(p);
                        mm->ReleaseInterface(spawner);
                }
                */

                pdata->last_ad = current_ticks();

                items->triggerEvent(p, p->p_ship, "ad");
        }
        else
        {
                chat->SendMessage(p, "Resurrection aborted due to flags!");
        }

        return FALSE;
}

local int trigger_callback(void *clos)
{
        Player *p = clos;

        pdata *pdata = PPDATA(p, pdkey);

        int cooldown = items->getPropertySum(p, p->p_ship, "adcooldown", 0);
        int delay = items->getPropertySum(p, p->p_ship, "addelay", 0);

		if (TICK_DIFF(current_ticks(), pdata->last_ad) > cooldown)
        {
                int total_delay;
                if (delay < 0)
                {
                        int respawnTime = cfg->GetInt(p->arena->cfg, "Kill", "EnterDelay", 0);
                        total_delay = respawnTime + delay;
                        if (total_delay < 1) total_delay = 1;
                }
                else
                {
                        total_delay = delay;
                }

                ml->ClearTimer(timer_callback, p);
                ml->SetTimer(timer_callback, total_delay, total_delay, p, p);

                items->triggerEvent(p, p->p_ship, "pread");
        }

        return FALSE;
}

local void eventActionCallback(Player *p, int eventID)
{
        if (eventID == ANTIDEATH_EVENT_ID)
        {
                ml->ClearTimer(trigger_callback, p);
                ml->SetTimer(trigger_callback, 0, 0, p, p);
        }
}

local void playerActionCallback(Player *p, int action, Arena *arena)
{
		if (action == PA_ENTERARENA)
		{
				pdata *pdata = PPDATA(p, pdkey);
				pdata->last_ad = current_ticks();
		}
}

EXPORT const char info_hs_antideath[] = "v1.1 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_antideath(int action, Imodman *_mm, Arena *arena)
{
        if (action == MM_LOAD)
        {
                mm = _mm;

                lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
                net = mm->GetInterface(I_NET, ALLARENAS);
                items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
                cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
                ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
                pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
                fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
                game = mm->GetInterface(I_GAME, ALLARENAS);
                chat = mm->GetInterface(I_CHAT, ALLARENAS);

                if (!lm || !net || !items || !cfg || !ml || !pd || !fc || !game || !chat)
                {
                        mm->ReleaseInterface(lm);
                        mm->ReleaseInterface(net);
                        mm->ReleaseInterface(items);
                        mm->ReleaseInterface(cfg);
                        mm->ReleaseInterface(ml);
                        mm->ReleaseInterface(pd);
                        mm->ReleaseInterface(fc);
                        mm->ReleaseInterface(game);
                        mm->ReleaseInterface(chat);
                        return MM_FAIL;
                }

                pdkey = pd->AllocatePlayerData(sizeof(pdata));
                if (pdkey == -1) return MM_FAIL;

				mm->RegCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);
                mm->RegCallback(CB_EVENT_ACTION, eventActionCallback, ALLARENAS);

                return MM_OK;
        }
        else if (action == MM_UNLOAD)
        {
                mm->UnregCallback(CB_EVENT_ACTION, eventActionCallback, ALLARENAS);
				mm->UnregCallback(CB_PLAYERACTION, playerActionCallback, ALLARENAS);

                ml->ClearTimer(timer_callback, NULL);
                ml->ClearTimer(trigger_callback, NULL);

                pd->FreePlayerData(pdkey);

                mm->ReleaseInterface(lm);
                mm->ReleaseInterface(net);
                mm->ReleaseInterface(items);
                mm->ReleaseInterface(cfg);
                mm->ReleaseInterface(ml);
                mm->ReleaseInterface(pd);
                mm->ReleaseInterface(fc);
                mm->ReleaseInterface(game);
                mm->ReleaseInterface(chat);

                return MM_OK;
        }
        return MM_FAIL;
}
