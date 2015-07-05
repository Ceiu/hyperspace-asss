
/* dist: public */

#include "statcodes.h"

const char *get_stat_name(int st)
{
	switch (st)
	{
		case STAT_KILL_POINTS: return "kill points";
		case STAT_FLAG_POINTS: return "flag points";
		case STAT_KILLS: return "kills";
		case STAT_DEATHS: return "deaths";
		case STAT_ASSISTS: return "assists";
		case STAT_TEAM_KILLS: return "team kills";
		case STAT_TEAM_DEATHS: return "team deaths";
		case STAT_ARENA_TOTAL_TIME: return "total time (this arena)";
		case STAT_ARENA_SPEC_TIME: return "spec time (this arena)";
		case STAT_DAMAGE_TAKEN: return "damage taken";
		case STAT_DAMAGE_DEALT: return "damage dealt";
		case STAT_FLAG_PICKUPS: return "flag pickups";
		case STAT_FLAG_CARRY_TIME: return "flag time";
		case STAT_FLAG_DROPS: return "flag drops";
		case STAT_FLAG_NEUT_DROPS: return "neutral drops";
		case STAT_FLAG_KILLS: return "flag kills";
		case STAT_FLAG_DEATHS: return "flag deaths";
		case STAT_FLAG_GAMES_WON: return "flag games won";
		case STAT_FLAG_GAMES_LOST: return "flag games lost";
		case STAT_TURF_TAGS: return "turf flags tagged";
		case STAT_BALL_CARRIES: return "ball carries";
		case STAT_BALL_CARRY_TIME: return "ball time";
		case STAT_BALL_GOALS: return "goals";
		case STAT_BALL_ASSISTS: return "assists";
		case STAT_BALL_STEALS: return "steals";
		case STAT_BALL_DELAYED_STEALS: return "delayed steals";
		case STAT_BALL_TURNOVERS: return "turnovers";
		case STAT_BALL_DELAYED_TURNOVERS: return "delayed turnovers";
		case STAT_BALL_SAVES: return "saves";
		case STAT_BALL_CHOKES: return "chokes";
		case STAT_BALL_GAMES_WON: return "ball games won";
		case STAT_BALL_GAMES_LOST: return "ball games lost";
		case STAT_KOTH_GAMES_WON: return "koth games won";
	}
	return "unknown";
}

const char *get_interval_name(int interval)
{
	switch (interval)
	{
		case INTERVAL_FOREVER: return "forever";
		case INTERVAL_RESET: return "per-reset";
		case INTERVAL_MAPROTATION: return "per-map-rotation";
		case INTERVAL_GAME: return "per-game";
	}
	return "unknown";
}

