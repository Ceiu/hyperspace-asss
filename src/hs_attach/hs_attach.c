#include "asss.h"
#include "persist.h"
#include "hscore.h"
#include "hscore_database.h"
#include "hscore_spawner.h"
#include "selfpos.h"

// TODO: cfghelp
// TODO: make an interface
// TODO: allow attaches without energy removal when a player could summon you?
// TODO: add energy removal on attach (w/ support for more prizes)
// TODO: move hscore stuff to another module
// TODO: add hscore events (via callbacks to above?)

typedef struct PData
{
	int block_summons;
	char attach_mask[8];
	char summon_mask[8];

	char attach_enemy;
	char summon_enemy;
	char summon_flags;
	char summon_ball;
	char attach_flags;
	char attach_ball;
	char attach_shield;
	char turret_limit;
	char summonable;
} PData;

typedef struct AData
{
	Region *noSummonRgn;

	char attach_mask[8];
	char summon_mask[8];

	char attach_enemy_mask;
	char summon_enemy_mask;
	char summon_flags_mask;
	char summon_ball_mask;
	char attach_flags_mask;
	char attach_ball_mask;
	char attach_shield_mask;
} AData;

// modules
local Imodman *mm;
local Ilogman *lm;
local Ichat *chat;
local Iconfig *cfg;
local Icmdman *cmd;
local Igame *game;
local Iplayerdata *pd;
local Iarenaman *aman;
local Iflagcore *flagcore;
local Iballs *balls;
local Ipersist *persist;
local Imainloop *ml;
local Inet *net;
local Ihscoreitems *items;
local Iselfpos *selfpos;
local Imapdata *mapdata;
local Ihscoredatabase *database;

local int pdata_key;
local int adata_key;

local void handle_fee(Player *source, Player *dest, int amount)
{
	if (amount)
	{
		int available = database->getMoney(source);
		int transfer = amount;
		if (available < amount) {
			transfer = available > 0 ? available : 0;
		}

		if (transfer > 0) {
			database->addMoney(dest, MONEY_TYPE_GIVE, transfer);
			database->addMoney(source, MONEY_TYPE_GIVE, -transfer);
		}
	}
}

local int GetPersistData(Player *p, void *data, int len, void *clos)
{
	PData *pdata = PPDATA(p, pdata_key);

	int *block_summons = (int*)data;

	*block_summons = pdata->block_summons;

	return sizeof(int);
}

local void SetPersistData(Player *p, void *data, int len, void *clos)
{
	PData *pdata = PPDATA(p, pdata_key);

	int *block_summons = (int*)data;

	pdata->block_summons = *block_summons;
}

local void ClearPersistData(Player *p, void *clos)
{
	PData *pdata = PPDATA(p, pdata_key);

	pdata->block_summons = 0;
}

local PlayerPersistentData my_persist_data =
{
	11506, INTERVAL_FOREVER, PERSIST_GLOBAL,
	GetPersistData, SetPersistData, ClearPersistData
};

local helptext_t antisummon_help =
"Targets: none\n"
"Args: none\n"
"Toggles your summonability.\n";

local void Cantisummon(const char *cmd, const char *params, Player *p, const Target *target)
{
	PData *pdata = PPDATA(p, pdata_key);
	if (pdata->block_summons)
	{
		pdata->block_summons = 0;
		chat->SendMessage(p, "You may now receive summons.");
	}
	else
	{
		pdata->block_summons = 1;
		chat->SendMessage(p, "You are now blocking summons.");
	}
}

local int is_ball_carrier(Player *p)
{
	ArenaBallData *abd;
	struct BallData bd;
	int i;
	int result = 0;

	abd = balls->GetBallData(p->arena);
	for (i = 0; i < abd->ballcount; ++i)
	{
		bd = abd->balls[i];
		if (bd.carrier == p && bd.state == BALL_CARRIED)
		{
			result = 1;
			break;
		}
	}
	balls->ReleaseBallData(p->arena);

	return result;
}

local int has_full_energy(Player *p)
{
	int full = 1;
	Ihscorespawner *spawner = mm->GetInterface(I_HSCORE_SPAWNER, p->arena);
	if (spawner)
	{
		int max = spawner->getFullEnergy(p);
		if (max != p->position.energy)
		{
			full = 0;
		}
	}
	mm->ReleaseInterface(spawner);

	return full;
}

local int has_space(Player *p, int attached)
{
	PData *pdata = PPDATA(p, pdata_key);
	int limit = pdata->turret_limit;

	if (limit)
	{
		return attached < limit;
	}
	else
	{
		return -1;
	}
}

local int get_turret_count(Player *p)
{
	Link *link;
	Player *i;
	int attached = 0;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		if (i != p && i->p_attached == p->pid)
		{
			attached++;
		}
	}
	pd->Unlock();

	return attached;
}

local void attach(Player *p, Player *to)
{
	// this function is called with all checks already done
	int to_radius = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[to->p_ship], "Radius", 14);
	int p_radius = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[p->p_ship], "Radius", 14);

	if (to_radius == 0) to_radius = 14;
	if (p_radius == 0) p_radius = 14;

	if (to_radius >= p_radius)
	{
		struct SimplePacket attach = { S2C_TURRET, p->pid, to->pid };

		net->SendToArena(p->arena, NULL, (byte*)&attach, 5, NET_RELIABLE);
		p->p_attached = to->pid;
	}
	else
	{
		Target target;
		target.type = T_PLAYER;
		target.u.p = p;
		game->WarpTo(&target, to->position.x >> 4, to->position.y >> 4);
	}
}

local void detach(Player *p)
{
	struct SimplePacket detach = { S2C_TURRET, p->pid, -1 };
	net->SendToArena(p->arena, NULL, (byte*)&detach, 5, NET_RELIABLE);
	p->p_attached = -1;
}

local void detach_all(Player *p)
{
	Player *i;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(i)
	{
		if (i != p && i->p_attached == p->pid)
		{
			detach(i);
		}
	}
	pd->Unlock();
}

local helptext_t summon_help =
"Targets: player\n"
"Args: none\n"
"Causes the target player to attach to your ship.\n";

local void Csummon(const char *cmd, const char *params, Player *p, const Target *target)
{
	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;
		AData *adata = P_ARENA_DATA(p->arena, adata_key);
		PData *pdata = PPDATA(p, pdata_key);
		PData *tdata = PPDATA(t, pdata_key);

		if (p != t)
		{
			if (!p->flags.is_dead)
			{
				if (!t->flags.is_dead)
				{
					if (tdata->summonable)
					{
						if (!tdata->block_summons)
						{
							if (t->p_ship != SHIP_SPEC)
							{
								if (p->p_ship != SHIP_SPEC)
								{
									if (IS_HUMAN(t))
									{
										if (!adata->noSummonRgn || !mapdata->Contains(adata->noSummonRgn, p->position.x>>4, p->position.y>>4))
										{
											int summon_enemies = (adata->summon_enemy_mask >> p->p_ship & 0x01) || pdata->summon_enemy;
											if (t->p_freq == p->p_freq || summon_enemies)
											{
												int mask = adata->summon_mask[p->p_ship] | pdata->summon_mask[p->p_ship];
												if (mask != 0)
												{
													if (mask >> t->p_ship & 0x01)
													{
														int summon_flags = (adata->summon_flags_mask >> p->p_ship & 0x01) || pdata->summon_flags;
														if (!flagcore->CountPlayerFlags(t) || summon_flags)
														{
															int summon_ball = (adata->summon_ball_mask >> p->p_ship & 0x01) || pdata->summon_ball;
															if (!is_ball_carrier(t) || summon_ball)
															{
																LinkedList list;
																LLInit(&list);

																if (!game->IsAntiwarped(t, &list))
																{
																	int space = has_space(p, get_turret_count(p));
																	if (space > 0)
																	{
																		attach(t, p);
																	}
																	else
																	{
																		game->WarpTo(target, p->position.x >> 4, p->position.y >> 4);
																	}
																}
																else
																{
																	int antiwarp_count = LLCount(&list);
																	if (antiwarp_count)
																	{
																		Player *aw = LLGetHead(&list)->data;
																		if (antiwarp_count == 1)
																		{
																			chat->SendMessage(p, "Player %s is antiwarped by %s.", t->name, aw->name);
																		}
																		else if (antiwarp_count == 2)
																		{
																			chat->SendMessage(p, "Player %s is antiwarped by %s and 1 other.", t->name, aw->name);
																		}
																		else
																		{
																			chat->SendMessage(p, "Player %s is antiwarped by %s and %d others.", t->name, aw->name, antiwarp_count - 1);
																		}
																		LLEmpty(&list);
																	}
																	else
																	{
																		chat->SendMessage(p, "Player %s is antiwarped.", t->name);
																	}
																}
															}
															else
															{
																chat->SendMessage(p, "You can't summon ball carriers from your %s.", cfg->SHIP_NAMES[p->p_ship]);
															}
														}
														else
														{
															chat->SendMessage(p, "You can't summon flag carriers from your %s.", cfg->SHIP_NAMES[p->p_ship]);
														}
													}
													else
													{
														chat->SendMessage(p, "You can't summon a %s from your %s.", cfg->SHIP_NAMES[t->p_ship], cfg->SHIP_NAMES[p->p_ship]);
													}
												}
												else
												{
													chat->SendMessage(p, "You can't summon from your %s.", cfg->SHIP_NAMES[p->p_ship]);
												}
											}
											else
											{
												chat->SendMessage(p, "You can't summon enemies from your %s.", cfg->SHIP_NAMES[p->p_ship]);
											}
										}
										else
										{
											chat->SendMessage(p, "Unable to summon to your current location. Try moving to open space.");
										}
									}
									else
									{
										chat->SendMessage(p, "You can't summon fake players.");
									}
								}
								else
								{
									chat->SendMessage(p, "You can't summon players while in spectator mode.");
								}
							}
							else
							{
								chat->SendMessage(p, "You can't summon spectators.");
							}
						}
						else
						{
							chat->SendMessage(p, "Player %s is blocking summons.", t->name);
						}
					}
					else
					{
						chat->SendMessage(p, "Player %s can't be summoned.", t->name);
					}
				}
				else
				{
					chat->SendMessage(p, "Player %s is dead.", t->name);
				}
			}
			else
			{
				chat->SendMessage(p, "You are dead.");
			}
		}
		else
		{
			chat->SendMessage(p, "You're already here!");
		}
	}
	else
	{
		chat->SendMessage(p, "You can only summon players");
	}
}

local helptext_t attach_help =
"Targets: player\n"
"Args: none\n"
"Causes your ship to attach to the target player.\n";

local void Cattach(const char *cmd, const char *params, Player *p, const Target *target)
{
	Arena *arena = p->arena;
	if (!arena)
		return;

	if (target->type == T_PLAYER)
	{
		Player *t = target->u.p;
		AData *adata = P_ARENA_DATA(arena, adata_key);
		PData *pdata = PPDATA(p, pdata_key);
		PData *tdata = PPDATA(t, pdata_key);
		int attach_fee = cfg->GetInt(arena->cfg, "Hyperspace", "AttachFee", 5);
		int summon_fee = cfg->GetInt(arena->cfg, "Hyperspace", "SummonFee", 10);

		if (p->p_attached == -1)
		{
			if (p != t)
			{
				int turrets = get_turret_count(p);
				if (!turrets)
				{
					if (!p->flags.is_dead)
					{
						if (!t->flags.is_dead)
						{
							if (t->p_ship != SHIP_SPEC)
							{
								if (p->p_ship != SHIP_SPEC)
								{
									// NOTE: unlike summoner, ?attach allows attaches to fakes

									int attach_to_enemies = (adata->attach_enemy_mask >> p->p_ship & 0x01) || pdata->attach_enemy;
									int is_enemy = t->p_freq != p->p_freq;
									if (!is_enemy || attach_to_enemies)
									{
										int attach_mask = adata->attach_mask[p->p_ship] | pdata->attach_mask[p->p_ship];
										int summoning = ((adata->summon_mask[t->p_ship] | tdata->summon_mask[t->p_ship]) >> p->p_ship) & 0x01;
										int inNoSummonRgn = (adata->noSummonRgn)?(mapdata->Contains(adata->noSummonRgn, t->position.x>>4, t->position.y>>4)):0;

										summoning = summoning && pdata->summonable && !is_enemy && !inNoSummonRgn;
										if (attach_mask != 0 || summoning)
										{
											if ((attach_mask >> t->p_ship & 0x01) || summoning)
											{
												int flags = flagcore->CountPlayerFlags(p);
												int attach_flags = (adata->attach_flags_mask >> p->p_ship & 0x01) || pdata->attach_flags;
												int summon_flags = (adata->summon_flags_mask >> t->p_ship & 0x01) || tdata->summon_flags;
												summoning = summoning && (summon_flags || !flags);
												if (!flags || attach_flags || summoning)
												{
													int has_ball = is_ball_carrier(p);
													int attach_ball = (adata->attach_ball_mask >> p->p_ship & 0x01) || pdata->attach_ball;
													int summon_ball = (adata->summon_ball_mask >> t->p_ship & 0x01) || tdata->summon_ball;
													summoning = summoning && (summon_ball || !has_ball);
													if (!has_ball || attach_ball || summoning)
													{
														LinkedList list;
														LLInit(&list);
														if (!game->IsAntiwarped(p, &list))
														{
															if (summoning || has_full_energy(p))
															{
																int space = has_space(t, get_turret_count(t));

																Target p_target;
																p_target.type = T_PLAYER;
																p_target.u.p = p;

																if (!summoning)
																{
																	int shield = (adata->attach_shield_mask >> p->p_ship & 0x01) || pdata->attach_shield;
																	game->GivePrize(&p_target, -PRIZE_FULLCHARGE, 1);
																	if (shield)
																	{
																		int bounty = p->position.bounty;
																		game->GivePrize(&p_target, PRIZE_SHIELD, 1);
																		selfpos->SetBounty(p, bounty);
																	}
																		handle_fee(p, t, attach_fee);
																}
																else
																{
																	handle_fee(p, t, summon_fee);
																}

																if (space > 0)
																{
																	attach(p, t);
																}
																else
																{
																	game->WarpTo(&p_target, t->position.x >> 4, t->position.y >> 4);
																}
															}
															else
															{
																chat->SendMessage(p, "You need full energy to attach.");
															}
														}
														else
														{
															int antiwarp_count = LLCount(&list);
															if (antiwarp_count)
															{
																Player *aw = LLGetHead(&list)->data;
																if (antiwarp_count == 1)
																{
																	chat->SendMessage(p, "You are antiwarped by %s.", aw->name);
																}
																else if (antiwarp_count == 2)
																{
																	chat->SendMessage(p, "You are antiwarped by %s and 1 other.", aw->name);
																}
																else
																{
																	chat->SendMessage(p, "You are antiwarped by %s and %d others.", aw->name, antiwarp_count - 1);
																}
																LLEmpty(&list);
															}
															else
															{
																chat->SendMessage(p, "You are antiwarped.");
															}
														}
													}
													else
													{
														chat->SendMessage(p, "You can't attach with a ball from your %s.", cfg->SHIP_NAMES[p->p_ship]);
													}
												}
												else
												{
													chat->SendMessage(p, "You can't attach with flags from your %s.", cfg->SHIP_NAMES[p->p_ship]);
												}
											}
											else
											{
												chat->SendMessage(p, "You can't attach to a %s from your %s.", cfg->SHIP_NAMES[t->p_ship], cfg->SHIP_NAMES[p->p_ship]);
											}
										}
										else
										{
											chat->SendMessage(p, "You can't attach from your %s.", cfg->SHIP_NAMES[p->p_ship]);
										}
									}
									else
									{
										chat->SendMessage(p, "You can't attach to enemies from your %s.", cfg->SHIP_NAMES[p->p_ship]);
									}
								}
								else
								{
									chat->SendMessage(p, "You can't attach to players while in spectator mode.");
								}
							}
							else
							{
								chat->SendMessage(p, "You can't attach to spectators.");
							}
						}
						else
						{
							chat->SendMessage(p, "Player %s is dead.", t->name);
						}
					}
					else
					{
						chat->SendMessage(p, "You are dead.");
					}
				}
				else
				{
					detach_all(p);
					// no message needed
				}
			}
			else
			{
				chat->SendMessage(p, "You can't attach to yourself!");
			}
		}
		else
		{
			detach(p);
		}
	}
	else
	{
		chat->SendMessage(p, "You can only attach to players");
	}
}

local void set_player_values(Player *p)
{
	int i;
	PData *pdata = PPDATA(p, pdata_key);
	int ship = p->p_ship;

	if (ship >= SHIP_WARBIRD && ship < SHIP_SPEC)
	{
		pdata->attach_enemy = items->getPropertySum(p, ship, "attachenemy", 0);
		pdata->summon_enemy = items->getPropertySum(p, ship, "summonenemy", 0);
		pdata->summon_flags = items->getPropertySum(p, ship, "summonflags", 0);
		pdata->summon_ball = items->getPropertySum(p, ship, "summonball", 0);
		pdata->attach_flags = items->getPropertySum(p, ship, "attachflags", 0);
		pdata->attach_ball = items->getPropertySum(p, ship, "attachball", 0);
		pdata->attach_shield = items->getPropertySum(p, ship, "attachshield", 0);
		pdata->turret_limit = items->getPropertySum(p, ship, "turretlimit", 0);
		pdata->summonable = !(items->getPropertySum(p, ship, "notsummonable", 0));
	}

	for (i = 0; i < 8; i++)
	{
		pdata->attach_mask[i] = items->getPropertySum(p, i, "attachmask", 0);
		pdata->summon_mask[i] = items->getPropertySum(p, i, "summonmask", 0);
	}
}

local int set_player_values_timer(void *param)
{
	Player *p = param;
	set_player_values(p);
	return FALSE;
}

local void item_changed_callback(Player *p, ShipHull *hull, Item *item, InventoryEntry *entry, int newCount, int oldCount)
{
	if (!hull || hull != database->getPlayerCurrentHull(p)) {
		return;
  }

  ml->SetTimer(set_player_values_timer, 0, 0, p, p);
}

local void reload_items_callback(void)
{
	Player *p;
	Link *link;
	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		set_player_values(p);
	}
	pd->Unlock();
}

local void shipsetChangedCallback(Player *p, int oldshipset, int newshipset)
{
  ml->SetTimer(set_player_values_timer, 0, 0, p, p);
}

local void ship_freq_change_callback(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
	set_player_values(p);
}

local void player_action(Player *p, int action, Arena *arena)
{
	if (action == PA_PREENTERARENA)
	{
		set_player_values(p);
	}
}

local void attach_packet(Player *p, byte *pkt, int len)
{
    if (len != 3) return;
    if (p->status != S_PLAYING || !p->arena) return;

    int tpid = ((struct SimplePacket*)pkt)->d1;
    if (tpid == -1) return;

    Player *t = pd->PidToPlayer(tpid);

    if (!t) return;
    if (p->arena != t->arena) return;

    Target target;
    target.type = T_PLAYER;
    target.u.p = t;

    Cattach(NULL, NULL, p, &target);
}

local void init_arena(Arena *arena)
{
	int i;
	AData *adata = P_ARENA_DATA(arena, adata_key);

	adata->noSummonRgn = mapdata->FindRegionByName(arena, "nosummon");

	for (i = 0; i < 8; i++)
	{
		adata->attach_mask[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "AttachMask", 0);
		adata->summon_mask[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "SummonMask", 0);
	}

	adata->attach_enemy_mask = cfg->GetInt(arena->cfg, "Attach", "AttachToEnemyMask", 0);
	adata->summon_enemy_mask = cfg->GetInt(arena->cfg, "Attach", "SummonEnemyMask", 0);
	adata->summon_flags_mask = cfg->GetInt(arena->cfg, "Attach", "SummonFlagsMask", 0);
	adata->summon_ball_mask = cfg->GetInt(arena->cfg, "Attach", "SummonBallMask", 0);
	adata->attach_flags_mask = cfg->GetInt(arena->cfg, "Attach", "AttachWithFlagsMask", 255);
	adata->attach_ball_mask = cfg->GetInt(arena->cfg, "Attach", "AttachWithBallMask", 0);
	adata->attach_shield_mask = cfg->GetInt(arena->cfg, "Attach", "AttachWithShieldMask", 255);

	lm->LogA(L_DRIVEL, "hs_attach", arena, "Loaded settings (%d)", adata->attach_mask[0]);
}

local void arena_action(Arena *arena, int action)
{
	if (action == AA_CREATE || action == AA_CONFCHANGED)
	{
		init_arena(arena);
	}
}

local void init_all()
{
	Arena *arena;
	Player *p;
	Link *link;

	pd->Lock();
	FOR_EACH_PLAYER(p)
	{
		set_player_values(p);
	}
	pd->Unlock();

	aman->Lock();
	FOR_EACH_ARENA(arena)
	{
		init_arena(arena);
	}
	aman->Unlock();
}

EXPORT const char info_hs_attach[] = "v1.1 Dr Brain <drbrain@gmail.com>";

EXPORT int MM_hs_attach(int action, Imodman *_mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = _mm;

		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		game = mm->GetInterface(I_GAME, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		flagcore = mm->GetInterface(I_FLAGCORE, ALLARENAS);
		balls = mm->GetInterface(I_BALLS, ALLARENAS);
		persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
		selfpos = mm->GetInterface(I_SELFPOS, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		database = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);

		if (!lm || !chat || !cfg || !cmd || !game || !pd || !aman || !flagcore || !balls || !persist || !ml || !net || !items || !selfpos || !mapdata || !database)
		{
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(chat);
			mm->ReleaseInterface(cfg);
			mm->ReleaseInterface(cmd);
			mm->ReleaseInterface(game);
			mm->ReleaseInterface(pd);
			mm->ReleaseInterface(aman);
			mm->ReleaseInterface(flagcore);
			mm->ReleaseInterface(balls);
			mm->ReleaseInterface(persist);
			mm->ReleaseInterface(ml);
			mm->ReleaseInterface(net);
			mm->ReleaseInterface(items);
			mm->ReleaseInterface(selfpos);
			mm->ReleaseInterface(mapdata);
			mm->ReleaseInterface(database);

			return MM_FAIL;
		}

		pdata_key = pd->AllocatePlayerData(sizeof(PData));
		if (pdata_key == -1) return MM_FAIL;

		adata_key = aman->AllocateArenaData(sizeof(AData));
		if (adata_key == -1) return MM_FAIL;

		persist->RegPlayerPD(&my_persist_data);

		init_all();

		cmd->AddCommand("attach", Cattach, ALLARENAS, attach_help);
		cmd->AddCommand("summon", Csummon, ALLARENAS, summon_help);
		cmd->AddCommand("antisummon", Cantisummon, ALLARENAS, antisummon_help);

		net->AddPacket(C2S_ATTACHTO, attach_packet);

		mm->RegCallback(CB_PLAYERACTION, player_action, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, arena_action, ALLARENAS);
		mm->RegCallback(CB_SHIPFREQCHANGE, ship_freq_change_callback, ALLARENAS);
		mm->RegCallback(CB_ITEM_COUNT_CHANGED, item_changed_callback, ALLARENAS);
		mm->RegCallback(CB_HS_ITEMRELOAD, reload_items_callback, ALLARENAS);
		mm->RegCallback(CB_SHIPSET_CHANGED, shipsetChangedCallback, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		persist->UnregPlayerPD(&my_persist_data);

		cmd->RemoveCommand("attach", Cattach, ALLARENAS);
		cmd->RemoveCommand("summon", Csummon, ALLARENAS);
		cmd->RemoveCommand("antisummon", Cantisummon, ALLARENAS);

		net->RemovePacket(C2S_ATTACHTO, attach_packet);

		mm->UnregCallback(CB_PLAYERACTION, player_action, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, arena_action, ALLARENAS);
		mm->UnregCallback(CB_SHIPFREQCHANGE, ship_freq_change_callback, ALLARENAS);
		mm->UnregCallback(CB_ITEM_COUNT_CHANGED, item_changed_callback, ALLARENAS);
		mm->UnregCallback(CB_HS_ITEMRELOAD, reload_items_callback, ALLARENAS);
		mm->UnregCallback(CB_SHIPSET_CHANGED, shipsetChangedCallback, ALLARENAS);

		ml->ClearTimer(set_player_values_timer, NULL);

		pd->FreePlayerData(pdata_key);
		aman->FreeArenaData(adata_key);

		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(flagcore);
		mm->ReleaseInterface(balls);
		mm->ReleaseInterface(persist);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(items);
		mm->ReleaseInterface(selfpos);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(database);

		return MM_OK;
	}
	return MM_FAIL;
}
