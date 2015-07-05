/*
 * Defines some basic scoring algorithms for use with the turf_reward module
 *
 * dist: public
 */

#include <stdio.h>
#include <string.h>
#include "asss.h"
#include "turf/turf_reward.h"


local Iplayerdata *playerdata; /* player data */
local Ilogman     *logman;     /* logging services */
local Ichat       *chat;       /* message players */
local Iturfreward *turfreward;


/* some macros to make it easier */
#define CALC_REWARD_FAILED \
	ta->calcState.status = TR_CALC_SUCCESS; \
	ta->calcState.award  = TR_AWARD_NONE;   \
	ta->calcState.update = 0;               \
	return;
	
#define CALC_REWARD_SUCCESS \
	ta->calcState.status = TR_CALC_SUCCESS; \
	ta->calcState.award  = TR_AWARD_TEAM;   \
	ta->calcState.update = 1;


/* function prototypes */
/* connected to interface */
/* decides which of the basic scoring algorithms to use */
local void calcReward(Arena *arena, TurfArena *ta);
local void rewardMessage(Player *player, struct TurfPlayer *tplayer, 
	struct TurfArena *tarena, RewardMessage_t messageType, unsigned int pointsAwarded);
local void removeTurfPlayer(struct TurfPlayer *pPlayer);
local void removeTurfTeam(struct TurfTeam *pTeam);

/* reward calculation functions */
local void crPeriodic(Arena *arena, TurfArena *ta); /* TR_STYLE_PERIODIC */
local void crStandard(Arena *arena, TurfArena *ta); /* TR_STYLE_STANDARD */
local void crWeights(Arena *arena, TurfArena *ta);  /* TR_STYLE_WEIGHTS */
local void crFixedPts(Arena *arena, TurfArena *ta); /* TR_STYLE_FIXED_PTS */

local int teamPointsCompareLT(const void *a, const void *b);

/* register with interface so that this module does scoring for turf_reward */
local struct Iturfrewardpoints myint =
{
	INTERFACE_HEAD_INIT(I_TURFREWARD_POINTS, "trp-basic")
	calcReward,
	rewardMessage,
	removeTurfPlayer,
	removeTurfTeam
};


EXPORT const char info_points_turf_reward[]
	= "v2.2 by GiGaKiLLeR <gigamon@hotmail.com>";

EXPORT int MM_points_turf_reward(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		playerdata = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		logman     = mm->GetInterface(I_LOGMAN,     ALLARENAS);
		chat       = mm->GetInterface(I_CHAT,       ALLARENAS);
		turfreward = mm->GetInterface(I_TURFREWARD, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (myint.head.global_refcount)
			return MM_FAIL;
		mm->ReleaseInterface(playerdata);
		mm->ReleaseInterface(logman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(turfreward);
		return MM_OK;
	}
	else if (action == MM_ATTACH) /* only for certain arenas */
	{
		mm->RegInterface(&myint, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregInterface(&myint, arena);
		return MM_OK;
	}
	return MM_FAIL;
}


local void calcReward(Arena *arena, TurfArena *ta)
{
	switch(ta->settings.reward_style)
	{
	case TR_STYLE_PERIODIC:  crPeriodic(arena, ta); return;
	case TR_STYLE_STANDARD:  crStandard(arena, ta); return;
	case TR_STYLE_STD_BTY:   crStandard(arena, ta); return;
	case TR_STYLE_WEIGHTS:   crWeights(arena, ta);  return;
	case TR_STYLE_FIXED_PTS: crFixedPts(arena, ta); return;
	case TR_STYLE_DISABLED:  return;
	}
	
	/* unknown reward style, failed calculations, no reward or update.  It's 
	 * possible that someone forgot to take this module out of loading from the 
	 * conf when they wrote their own custom reward module */
	CALC_REWARD_FAILED
}


local void rewardMessage(Player *player, struct TurfPlayer *tplayer, 
	struct TurfArena *ta, RewardMessage_t messageType, unsigned int pointsAwarded)
{
	char message[128];

	if(messageType == TR_RM_INVALID_TEAM)
	{
		chat->SendSoundMessage(player, SOUND_DING, 
			"Reward: 0/0 (minimum team requirements not met)");
		return;
	}

	if(messageType == TR_RM_NON_PLAYER)
	{
		chat->SendSoundMessage(player, SOUND_DING, 
			"Reward: 0/%lu (not playing)", ta->numPoints);
		return;
	}

	snprintf(message, sizeof(message), 
		"Reward: %d/%lu", pointsAwarded, ta->numPoints);
	
	if(messageType==TR_RM_PLAYER_MAX)
		strcat(message, " [MAX] ");

	switch(ta->calcState.award)
	{
	case TR_AWARD_PLAYER:
		chat->SendSoundMessage(player, SOUND_DING, "%s", message);
		break;
	case TR_AWARD_TEAM:
		chat->SendSoundMessage(player, SOUND_DING, "%s", message);
		break;
	case TR_AWARD_BOTH:
	{
		int pointsFreq   = tplayer->team->numPoints;
		int pointsPlayer = tplayer->points;

		chat->SendSoundMessage(player, SOUND_DING,
			"%s [%i:%i]", message, pointsPlayer, pointsFreq);
		break;
	}
	default:
		break;
	}
}


local void removeTurfPlayer(struct TurfPlayer *pPlayer)
{
	/* noop */
}


local void removeTurfTeam(struct TurfTeam *pTeam)
{
	/* noop */
}


local void crStandard(Arena *arena, TurfArena *ta)
{
	TurfTeam *pTeam = NULL;
	Link *l;

	/* make sure cfg settings are valid (we dont want any crashes in here) */
	if(ta->settings.min_players_on_team < 1)
		ta->settings.min_players_on_team = 1;
	if(ta->settings.min_flags < 1)
		ta->settings.min_flags = 1;
	if(ta->settings.min_weights < 1)
		ta->settings.min_weights = 1;

	/* check that map has flags */
	if (ta->numFlags < 1)
	{
		/* no flags, therefore no weights, stop right here */
		logman->LogA(L_WARN, "points_turf_reward", arena, "map has no flags.");
		
		/* fail calculaions */
		CALC_REWARD_FAILED
	}

	/* check that numWeights for arena > 0 */
	if (ta->numWeights < 1 || ta->sumPerCapitaWeights<=0)
	{
		/* no team owns flags */
		logman->LogA(L_DRIVEL, "points_turf_reward", arena, 
			"no one owns any weights.");
		chat->SendArenaMessage(arena, "Notice: All flags are unowned.");
		chat->SendArenaSoundMessage(arena, SOUND_DING,
			"Reward:0 (arena minimum requirements not met)");
		
		/* fail requirements */
		CALC_REWARD_FAILED
	}

	/* check if there are enough players for rewards */
	if ( ta->numPlayers < ta->settings.min_players_in_arena )
	{
		logman->LogA(L_DRIVEL, "points_turf_reward", arena,
			"Not enough players in arena for rewards.  Current:%i Minimum:%i",
			ta->numPlayers,
			ta->settings.min_players_in_arena);
		chat->SendArenaMessage(arena, "Notice: not enough players for rewards.");
		chat->SendArenaSoundMessage(arena, SOUND_DING,
			"Reward:0 (arena minimum requirements not met)");
		
		/* fail requirements */
		CALC_REWARD_FAILED
	}

	/* check if there are enough teams for rewards */
	if( ta->numValidTeams < ta->settings.min_teams )
	{
		logman->LogA(L_DRIVEL, "points_turf_reward", arena,
			"Not enough teams in arena for rewards.  Current:%i Minimum:%i",
			ta->numTeams,
			ta->settings.min_teams);
		chat->SendArenaMessage(arena, "Notice: not enough teams for rewards.");
		chat->SendArenaSoundMessage(arena, SOUND_DING,
			"Reward:0 (arena minimum requirements not met)");

		/* fail requirements */
		CALC_REWARD_FAILED
	}

	/* figure out percent of jackpot team will recieve and how many points
	 * that relates to */
	if(ta->settings.reward_style == TR_STYLE_STD_BTY)
		ta->numPoints = (ta->bountyExchanged<0) ? 0 : ta->bountyExchanged;
	else
		ta->numPoints = ta->settings.reward_modifier * ta->numPlayers;

	if( ta->numPoints == 0 )
	{
		/* no points to award */
		CALC_REWARD_SUCCESS
	}
		
	/* at least one team passed minimum requirements, award them points */
	for(l = LLGetHead(&ta->validTeams) ; l ; l=l->next)
	{
		pTeam = l->data;
		pTeam->percent = 
			(double)(pTeam->perCapitaWeights / ta->sumPerCapitaWeights * 100.0);

		/* double check, min_players_on_freq should have already weeded any out */
		if(pTeam->numPlayers>0)
			pTeam->numPoints = (int)(ta->numPoints * (pTeam->percent/100) / pTeam->numPlayers);
		else
		{
			/* this should never ever happen */
			logman->LogA(L_WARN, "points_turf_reward", arena,
				"When calculating numPoints, a team that passed min requirements had 0 players. Check that min_players_freq > 0.");
		}
	}
	
	/* success! award and update */
	CALC_REWARD_SUCCESS
}


local void crPeriodic(Arena *arena, TurfArena *ta)
{
	int modifier = ta->settings.reward_modifier;
	TurfTeam *pTeam;
	Link *l;

	crStandard(arena, ta);

	if(modifier == 0)  /* means dings disabled */
	{
		/* no one gets points */
		for(l = LLGetHead(&ta->validTeams) ; l ; l=l->next)
		{
			pTeam = l->data;

			pTeam->percent   = 0;
			pTeam->numPoints = 0;
		}
		
		/* success! update only */
		ta->calcState.status = TR_CALC_SUCCESS;
		ta->calcState.award  = TR_AWARD_NONE;
		ta->calcState.update = 1;
	}

	if(modifier > 0)
	{
		/* # points = modifier * # flags owned */
		for(l = LLGetHead(&ta->validTeams) ; l ; l=l->next)
		{
			pTeam = l->data;

			pTeam->percent   = 0;
			pTeam->numPoints = modifier * pTeam->numFlags;
		}
	}
	else
	{
		/* # points = modifier * # flags owned * # players in arena */
		int numPlayers = ta->numPlayers;

		for(l = LLGetHead(&ta->validTeams) ; l ; l=l->next)
		{
			pTeam = l->data;

			pTeam->percent   = 0;
			pTeam->numPoints = numPlayers * (-modifier) * pTeam->numFlags;
		}
	}

	/* success! award and update */
	CALC_REWARD_SUCCESS
}


local void crWeights(Arena *arena, TurfArena *ta)
{
	Link *l;

	for(l = LLGetHead(&ta->validTeams) ; l ; l=l->next)
	{
		TurfTeam *pTeam = l->data;

		pTeam->percent   = 0;
		pTeam->numPoints = (pTeam->numWeights<0) ? 0 : pTeam->numWeights;
	}

	CALC_REWARD_SUCCESS
}


local void crFixedPts(Arena *arena, TurfArena *ta)
{
	Link *l;
	int pts = 1000;

	crStandard(arena, ta);
	
	/* sort teas into 1st, 2nd, 3rd... place */
	LLSort(&ta->validTeams, teamPointsCompareLT);

	/* CHECK: I am unsure if LLSort sorts asc or desc */
		
	/* TODO: add settings to be able to control this */
	
	for(l = LLGetHead(&ta->validTeams) ; l ; l=l->next)
	{
		TurfTeam *pTeam = l->data;

		pTeam->percent   = 0;
		pTeam->numPoints = (pts < 0) ? 0 : pts;
		if(pts >= 0)
			pts -= 500;
	}

	CALC_REWARD_SUCCESS
}

local int teamPointsCompareLT(const void *a, const void *b)
{
	return ((TurfTeam*)a)->numPoints < ((TurfTeam*)b)->numPoints;
}

