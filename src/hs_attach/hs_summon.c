#include "asss.h"
#include "hscore.h"
#include "hscore_shipnames.h"
#include "balls.h"

local Ilogman *lm;
local Inet *net;
local Icmdman *cmd;
local Ichat *chat;
local Ihscoreitems *items;
local Iconfig *cfg;
local Imainloop *ml;
local Imapdata *mapdata;
local Iplayerdata *pd;
local Iflagcore *fc;
local Iballs *balls;
local Igame *game;

typedef struct SummonInfo {

	u8 dead 	: 1;  	// If the player is currently dead.
	u8 blocking	: 1;    // If player is blocking summoning requests.
	u8 buffer   : 6;

} SummonInfo;

local int summonInfoKey;

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


local int handle_respawn(void *param)
{
    SummonInfo *s_info = (SummonInfo *)param;

    s_info->dead = 0;
    return FALSE;
}

local void handle_death(Player *player, byte *pkt, int len)
{
    int intEnterDelay = cfg->GetInt(player->arena->cfg, "Kill", "EnterDelay", 0);

    if(intEnterDelay > 0)
    {
        SummonInfo *s_info = PPDATA(player, summonInfoKey);

        s_info->dead = 1;
        ml->SetTimer(handle_respawn, intEnterDelay + 100, 0, s_info, s_info);
    }
}



local helptext_t summon_help =
"Targets: player\n"
"Args: none\n"
"Summons player to your location.\n";

local void Csummon(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;
		if (t->p_ship != SHIP_SPEC && p->p_ship != SHIP_SPEC && p->p_freq == t->p_freq && IS_HUMAN(t))
		{
			// Make sure the player has the "summoner" item...
			if(!items->getPropertySum(p, p->p_ship, "summon", 0))
			{
				chat->SendMessage(p, "Your ship is not equipped with a summoning device.");
				return;
			}

			// Prevent summoning while dead...
			SummonInfo *pSInfo = PPDATA(p, summonInfoKey);
			if(pSInfo->dead)
			{
                chat->SendMessage(p, "You cannot summon players while you're dead.");
				return;
			}

			// Prevent summoning players who are blocking it...
			SummonInfo *tSInfo = PPDATA(t, summonInfoKey);
			if(tSInfo->blocking)
			{
				chat->SendMessage(p, "Player %s is currently blocking summoning requests.", t->name);
				return;
			}

			// Prevent summoning players with flags...
			if(fc->CountPlayerFlags(t))
			{
				chat->SendMessage(p, "You cannot summon players that are carrying flags.");
				return;
			}

			// Prevent summoning players through aw...
			if(game->IsAntiwarped(t, NULL))
			{
				chat->SendMessage(p, "Player %s is antiwarped.", t->name);
				return;
			}

			// Prevent summoning players carrying a powerball
			if (isBallCarrier(t->arena, t))
			{
				chat->SendMessage(p, "Player %s is holding a powerball.", t->name);
				return;
			}

			// Prevent summoning to the twhub...
			Region *region = mapdata->FindRegionByName(p->arena, "tw_hub");
			if(region && mapdata->Contains(region, p->position.x >> 4, p->position.y >> 4))
			{
				chat->SendMessage(p, "You cannot summon players to the tw hub,");
				return;
			}

			// Make sure the player doesn't summon more players than allowed...
			int intAttached = 0;
			int intAttachLimit = cfg->GetInt(p->arena->cfg, shipNames[p->pkt.ship], "TurretLimit", 0);

			Link *link;
			Player *cp;
			FOR_EACH_PLAYER(cp)
			{
				// Need to make sure we don't count ourselves, since summoner allows us to do self-attaches.
				if(cp->pid != p->pid && cp->p_attached == p->pid)
					++intAttached;
			}

			if(intAttached >= intAttachLimit)
			{
				chat->SendMessage(p, "Cannot summon %s; Too many turrets already attached.", t->name);
				return;
			}

			// Everything is ok; summon player...
			struct SimplePacket attach = { S2C_TURRET, t->pid, p->pid };

			net->SendToArena(p->arena, NULL, (byte*)&attach, 5, NET_RELIABLE);
			t->p_attached = p->pid;

			items->triggerEvent(p, p->p_ship, "summon");
		}
	}
}

local int detachTimer(void *param)
{
	Player *p = param;

	struct SimplePacket detach = { S2C_TURRET, p->pid, -1 };
	net->SendToArena(p->arena, NULL, (byte*)&detach, 5, NET_RELIABLE);
	p->p_attached = -1;

	//don't reschedule
	return FALSE;
}

local helptext_t antisummon_help =
"Targets: none\n"
"Args: none\n"
"Enables or disables anti-summon.\n";

local void Cantisummon(const char *command, const char *params, Player *p, const Target *target)
{
    SummonInfo *pSInfo = PPDATA(p, summonInfoKey);

    if(target->type == T_ARENA)
    {
        pSInfo->blocking = !pSInfo->blocking;

        if(pSInfo->blocking)
            chat->SendMessage(p, "Anti-summoner engaged.");
        else
            chat->SendMessage(p, "Anti-summoner disengaged.");
    }
}


local helptext_t attach_help =
"Targets: player\n"
"Args: none\n"
"Attaches you to player on any frequency.\n";

local void Cattach(const char *command, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;

		if (items->getPropertySum(p, p->p_ship, "attach", 0))
		{
			if(!fc->CountPlayerFlags(p))
			{
				if (t->pkt.ship != SHIP_SPEC && IS_HUMAN(t))
				{
					//check if the target ship allows attaching
					int turretLimit = cfg->GetInt(p->arena->cfg, shipNames[t->pkt.ship], "TurretLimit", 0);
					if (turretLimit > 0)
					{
						struct SimplePacket attach = { S2C_TURRET, p->pid, t->pid };

						net->SendToArena(p->arena, NULL, (byte*)&attach, 5, NET_RELIABLE);
						p->p_attached = t->pid;

						//set for detach
						ml->SetTimer(detachTimer, 100, 0, p, p);

						items->triggerEvent(p, p->p_ship, "attach");
					}
					else
					{
						chat->SendMessage(p, "Target ship cannot hold any turrets.");
					}
				}
			}
			else
			{
				chat->SendMessage(p, "You cannot attach with flags.");
			}
		}
		else
		{
			chat->SendMessage(p, "Your ship is not equipped with an attaching device.");
		}
	}
}

EXPORT const char info_hs_summon[] = "v1.2 Dr Brain <drbrain@gmail.com>, Cerium <cerium@gmail.com>";

EXPORT int MM_hs_summon(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		fc = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);

		if (!lm || !cmd || !chat || !net || !items || !cfg || !ml || !mapdata || !pd || !fc || !balls || !game)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(mapdata);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(fc);
			mm->ReleaseInterface(balls);
			mm->ReleaseInterface(game);
			return MM_FAIL;
		}

		summonInfoKey = pd->AllocatePlayerData(sizeof(SummonInfo));

		cmd->AddCommand("summon", Csummon, ALLARENAS, summon_help);
		cmd->AddCommand("attach", Cattach, ALLARENAS, attach_help);
		cmd->AddCommand("antisummon", Cantisummon, ALLARENAS, antisummon_help);

		net->AddPacket(C2S_DIE, handle_death);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_DIE, handle_death);
		ml->ClearTimer(handle_respawn, 0);

		cmd->RemoveCommand("summon", Csummon, ALLARENAS);
		cmd->RemoveCommand("attach", Cattach, ALLARENAS);
		cmd->RemoveCommand("antisummon", Cantisummon, ALLARENAS);

		pd->FreePlayerData(summonInfoKey);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(fc);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(game);

		return MM_OK;
	}
	return MM_FAIL;
}

