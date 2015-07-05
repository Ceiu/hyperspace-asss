#include "asss.h"

#include "arenaman.h"
#include "game.h"
#include "logman.h"
#include "watchdamage.h"
#include "kill.h"

local Imodman *mm;
local Iarenaman *aman;
local Igame *game;
local Ilogman *lm;
local Iwatchdamage *wd;
local Ikill *kill;
local Iplayerdata *pd;
local Iflagcore *flag;

#define KILLER_NAME "<engine failure>" // Convenient for hyperspace, as it avoids fake player clutter

local int adkey;

typedef struct ad_suicides {
    Killer* killer;         // Our killer fake player
    int attached;           // Whether or not we're attached to the arena.
} ad_suicides;


local void player_action(Player *, int action, Arena *);
local void player_damaged(Arena *, Player *, struct S2CWatchDamage *, int count);

////////////////////////////////////////////////////////////////////////////////

local void player_action(Player *player, int action, Arena *arena) {
    ad_suicides *adata = P_ARENA_DATA(arena, adkey);

    if(adata->attached) {
        switch(action) {
            case PA_ENTERARENA:
                if(IS_STANDARD(player)) {
                    wd->ModuleWatch(player, 1);
                }
                break;

            case PA_LEAVEARENA:
                if(IS_STANDARD(player)) {
                    wd->ModuleWatch(player, 0);
                }
                break;
        }
    }
}

local void player_damaged(Arena *arena, Player *player, struct S2CWatchDamage *dmgdata, int count) {
    ad_suicides *adata = P_ARENA_DATA(arena, adkey);

    if(adata->attached && adata->killer) {
        for(int i = 0; i < count; ++i) {
            if (dmgdata->damage[i].shooteruid == player->pid) {
                int dmg_dealt = dmgdata->damage[i].damage;
                int dmg_energy = dmgdata->damage[i].energy;

                if(dmg_dealt > dmg_energy) {
                    lm->LogP(L_DRIVEL, "suicides", player, "Player suicided from a bomb (Damage: %d, Energy: %d).", dmg_dealt, dmg_energy);
                    kill->Kill(player, adata->killer, 0, 0);
                    break;
                }
            }
        }
    }
}

local void edit_death(Arena *arena, Player **killer, Player **killed, int *bounty) {
    ad_suicides *adata = P_ARENA_DATA(arena, adkey);
    Player *p = *killed;

    if (adata->attached && adata->killer) {
        if (*killer == kill->GetKillerPlayer(adata->killer)) {
            /* Neut the flags if the player kills himself while carrying flags */
            if (flag->CountPlayerFlags(p) > 0) {
                Iflaggame *fgame = mm->GetInterface(I_FLAGGAME, arena);

                if (fgame) {
                    int num_flags = flag->CountFlags(arena);
                    FlagInfo fis[num_flags];

                    flag->GetFlags(arena, 0, fis, num_flags);

                    for (int i = 0; i < num_flags; i++) {
                        /* Only neut the flags the player was carrying */
                        if (fis[i].carrier == p)
                            fgame->Cleanup(arena, i, CLEANUP_KILL_CANTCARRY, p, p->p_freq);
                    }

                    mm->ReleaseInterface(fgame);
                }
            }

            /* Swap the fake player with the suiciding player */
            *killer = *killed;
        }
    }
}

local Akill kill_adviser =
{
    ADVISER_HEAD_INIT(A_KILL)

    NULL,
    edit_death
};

////////////////////////////////////////////////////////////////////////////////

local void getInterfaces(Imodman *mm) {
    aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
    game = mm->GetInterface(I_GAME, ALLARENAS);
    lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
    wd = mm->GetInterface(I_WATCHDAMAGE, ALLARENAS);
    kill = mm->GetInterface(I_KILL, ALLARENAS);
    pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
    flag = mm->GetInterface(I_FLAGCORE, ALLARENAS);
}

local void releaseInterfaces(Imodman *mm) {
    mm->ReleaseInterface(pd);
    mm->ReleaseInterface(kill);
    mm->ReleaseInterface(aman);
    mm->ReleaseInterface(game);
    mm->ReleaseInterface(lm);
    mm->ReleaseInterface(wd);
    mm->ReleaseInterface(flag);
}

EXPORT const char info_suicides[] = "v1.02 by Arnk Kilo Dylie, Chris \"Ceiu\" Rog, SpiderNL, monkey";
EXPORT int MM_suicides(int action, Imodman *mm_, Arena *arena) {
    Link *link;
    Player *p;
    ad_suicides *adata;

    switch(action) {
        case MM_LOAD:
            mm = mm_;

            getInterfaces(mm);

            if(!aman || !game || !lm || !wd || !kill || !pd) {
                releaseInterfaces(mm);
                return MM_FAIL;
            }

            adkey = aman->AllocateArenaData(sizeof(ad_suicides));
            return MM_OK;

        case MM_UNLOAD:
            releaseInterfaces(mm);
            return MM_OK;

        case MM_ATTACH:
            adata = P_ARENA_DATA(arena, adkey);

            // Register callbacks for the arena...
            mm->RegCallback(CB_PLAYERACTION, player_action, arena);
            mm->RegCallback(CB_PLAYERDAMAGE, player_damaged, arena);

            // Create killer...
            if(!adata->killer)
                adata->killer = kill->LoadKiller(KILLER_NAME, arena, SHIP_SHARK, 9999);

            // Turn on damage monitoring for everyone in the arena...
            pd->Lock();
            for(link = LLGetHead(&pd->playerlist); link && (p = link->data); link = link->next) {
                if(p->arena == arena && IS_STANDARD(p)) {
                    wd->ModuleWatch(p, 1);
                }
            }
            pd->Unlock();

            // Register adviser for the arena...
            mm->RegAdviser(&kill_adviser, arena);

            // Set attached flag...
            adata->attached = 1;

            return MM_OK;

        case MM_DETACH:
            adata = P_ARENA_DATA(arena, adkey);

            // Clear attached flag...
            adata->attached = 0;

            // Turn off damage monitoring...
            pd->Lock();
            for(link = LLGetHead(&pd->playerlist); link && (p = link->data); link = link->next) {
                if(p->arena == arena && IS_STANDARD(p))
                    wd->ModuleWatch(p, 0);
            }
            pd->Unlock();

            // Remove killer if necessary (again, should be)...
            if(adata->killer) {
                kill->UnloadKiller(adata->killer);
                adata->killer = 0;
            }

            // Release callbacks...
            mm->UnregCallback(CB_PLAYERACTION, player_action, arena);
            mm->UnregCallback(CB_PLAYERDAMAGE, player_damaged, arena);

            // Release adviser...
            mm->UnregAdviser(&kill_adviser, arena);

            return MM_OK;
    }

    return MM_FAIL;
}