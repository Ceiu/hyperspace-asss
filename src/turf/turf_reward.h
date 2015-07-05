
/* dist: public */

#ifndef __TURF_REWARD_H
#define __TURF_REWARD_H

/*
 * Iturfrewardpoints - score calculation interface for turf_reward
 * other modules can define other scoring algorithms using data from
 * the turf_reward data structure by registering with this interface
 */

/*
 * Iturfreward - interface for turf_reward
 * used by points_turf_reward for multiarena scoring locking/unlocking
 */

#include "fg_turf.h"

/* prototypes */
struct TurfArena;
struct Iturfrewardpoints;


/* turf_reward specific callbacks */

/* note CB_TURFTAG in fg_turf.h */

#define CB_TURFSTEAL "turfsteal"
typedef void (*TurfStealFunc)(Arena *arena, Player *p, int fid);
/* pycb: arena, player, int */

/* called when a flag is 'recovered' (note: CB_TURFTAG will still be called)
 * possible use would be to have a module that manipulates lvz objects telling
 * player that the flag tagged was recovered */
#define CB_TURFRECOVER "turfrecover"
typedef void (*TurfRecoverFunc)(Arena *arena, int fid, int pid, int freq,
	int dings, int weight, int recovered);
/* pycb: arena, int, int, int, int, int, int */

/* called when a flag is 'lost' (note: CB_TURFTAG will still be called) possible
 * use would be to have a module that manipulates lvz objects telling players
 * that a flag was lost */
#define CB_TURFLOST "turflost"
typedef void (*TurfLostFunc)(Arena *arena, int fid, int pid, int freq,
	int dings, int weight, int recovered);
/* pycb: arena, int, int, int, int, int, int */

/* A special callback!! - the turf_arena data is LOCKED when this is called
 * This is called AFTER players are awarded points (good time for history stuff
 * and/or stats output).  Any function registering with this callback must not
 * require a long processing time. */
#define CB_TURFPOSTREWARD "turfpostreward"
typedef void (*TurfPostRewardFunc)(Arena *arena, struct TurfArena *ta);

/* called during a flag game victory */
/* NOT CURRENTLY IMPLEMENTED */
#define CB_TURFVICTORY "turfvictory"
typedef void (*TurfVictoryFunc) (Arena *arena);
/* pycb: arena */


/* for linked list for data on teams that have a chance to 'recover' */
typedef struct TurfFlagPrevious
{
	int lastOwned;       /* how many dings ago the flag was owned */

	char taggerName[24]; /* name of player that tagged flag before stolen */
	int freq;            /* previous flag owner's freq */
	int dings;           /* previous # of dings */
	int weight;          /* previous weight of flag */
	int recovered;       /* number of times was recovered */

	ticks_t tagTC;       /* time flag was originally tagged in ticks */
	ticks_t lostTC;      /* time flag was lost in ticks */
} TurfFlagPrevious;

/* to hold extra flag data for turf flags */
typedef struct TurfFlag
{
	struct TurfTeam   *team;   /* team that owns flag     */
	struct TurfPlayer *tagger; /* player that tagged flag */

	int dings;       /* # of dings the flag has been owned for */
	int weight;      /* weight of the flag (how much it's worth) */
	int recovered;   /* # of times flag was recovered */

	ticks_t tagTC;   /* time flag was originally tagged in ticks */
	ticks_t lastTC;  /* time flag was last tagged in ticks */

	LinkedList old;  /* linked list of TurfFlagPrevious storing data of flag's */
	                 /* previous owners who have a chance to 'recover' it */
} TurfFlag;

typedef struct TurfPlayer
{
	char name[24];           /* name of player */

	struct TurfTeam *team;   /* pointer to player's team */
	LinkedList flags;        /* flags the player tagged and still owns */

	unsigned int points;     /* points to award player */
	unsigned int tags;       /* # of flags tagged */
	unsigned int steals;     /* # of flags player stole from enemy */
	unsigned int recoveries; /* # of flags recovered from enemy */
	unsigned int lost;       /* # of flags tagged by this player that were lost */

	unsigned int kills;      /* # of kills */
	unsigned int bountyTaken;/* sum of bounty taken from others for kills */
	unsigned int deaths;     /* # of deaths */
	unsigned int bountyGiven;/* sum of bounty given to others for deaths */

	void *extra;             /* use to hook on your own data */

	/* internal data members, do not touch these!! */
	int isActive;            /* flag to tell if node is to an active player */
	int pid;                 /* pid of player (fastest way to get Player */
	                         /* pointer from  player module) */
} TurfPlayer;

typedef struct TurfTeam
{
	int freq;                /* freq # */

	int numFlags;            /* # of flags team owns */
	double percentFlags;     /* % of the flags owned */
	double perCapitaFlags;   /* flags per player on team */

	long int numWeights;     /* sum of weights for owned flags */
	double percentWeights;   /* % of the total weights */
	double perCapitaWeights; /* weights per player on team */

	unsigned int tags;       /* # of flag tagged */
	unsigned int steals;     /* # of flags stolen */
	unsigned int recoveries; /* # of flag recovered */
	unsigned int lost;       /* # of flags lost */

	unsigned int kills;      /* # of kills */
	unsigned int bountyTaken;/* sum of bounty taken from others for kills */
	unsigned int deaths;     /* # of deaths */
	unsigned int bountyGiven;/* sum of bounty given to others for deaths */

	int numPlayers;          /* # of players on the team */
	double percent;          /* % of jackpot to recieve */
	unsigned int numPoints;  /* # of points to award to team */

	LinkedList flags;        /* linked list of TurfFlags owned */
	LinkedList players;      /* all TurfPlayers linked to team */
	LinkedList playersPts;   /* TurfPlayers playing (get pts) */
    LinkedList playersNoPts; /* TurfPlayers not playing; in spec/safe zone; (no pts) */

	void *extra;             /* use to hook on your own data */
} TurfTeam;


/* cfg settings for turf reward */
typedef struct TurfSettings
{
	int reward_style;           /* change reward algorithms */
	int min_players_on_team;    /* min # of players needed on a freq for that */
	                            /* freq to recieve reward pts */
	int min_players_in_arena;   /* min # of players needed in the arena for */
	                            /* anyone to recieve reward pts */
	int min_teams;              /* min # of teams needed for anyone to */
	                            /* recieve reward pts */
	int min_flags;              /* min # of flags needed to be owned by freq */
	                            /* in order to recieve reward pts */
	double min_percent_flags;   /* min % of flags needed to be owned by freq */
	                            /* in order to recieve reward pts */
	int min_weights;            /* min # of weights needed to be owned by */
	                            /* freq in order to recieve reward pts */
	double min_percent_weights; /* min % of weights needed to be owned by */
	                            /* freq in order to recieve reward pts */
	double min_percent;         /* min percentage of jackpot needed to */
	                            /* recieve an award */
	int reward_modifier;
	int max_points;             /*maximum # of points to award a single person */

	int spec_recieve_points;    /* do spectators recieve rewards */
	int safe_recieve_points;    /* do players in safe zones recieve rewards */

	int recovery_cutoff;        /* recovery cutoff style to be used */
	int recover_dings;
	int recover_time;
	int recover_max;

	int weight_calc;
	int set_weights;            /* number of weights that were set from cfg */
	int *weights;               /* array of weights from cfg */
/*
	int min_kills_arena;   todo: min # of kills needed for anyone to recieve rewards
	int min_kills_freq;    todo: min # of kills needed by a freq for that freq to recieve rewards
	int min_tags_arena;    todo: min # of tags needed in arena for anyone to recieve rewards
	int min_tags_freq;     todo: min # of tags needed by a freq for that freq to recieve rewards
*/
	/* data for timer */
	int timer_initial;          /* initial timer delay */
	int timer_interval;         /* interval for timer to repeat */
} TurfSettings;

typedef struct TurfState
{
	enum
	{
		TR_CALC_SUCCESS, /* CalcReward completed successfully */
		TR_CALC_FAIL     /* no futher reward events */
	} status;

	enum
	{
		TR_AWARD_PLAYER, /* reward per player */
		TR_AWARD_TEAM,   /* reward per team */
		TR_AWARD_BOTH,   /* reward by both player and team */
		TR_AWARD_NONE    /* no rewards */
	} award;

	int update:1; /* update flags? 1=yes 0=no */
} TurfState;

/* turf reward data for an arena */
typedef struct TurfArena
{
	TurfSettings settings;       /* cfg settings */
	struct Iturfrewardpoints *trp; /* turf reward scoring interface */

	ticks_t dingTime;            /* time of last ding */

	/* reward data */
	int numPlayers;              /* # of ppl playing (not inc. spec) */
	int numValidPlayers;         /* # of ppl on teams that passed requirements */
	int numTeams;                /* # of teams */
	int numValidTeams;           /* # of teams that passed requirements */
	int numInvalidTeams;         /* # of teams that didn't pass requirements */

	int numFlags;                /* # of flags on the map */
	double sumPerCapitaFlags;    /* sum of all teams percapita flags */

	long int numWeights;         /* the complete # of flag weights */
	double sumPerCapitaWeights;  /* sum of all teams percapitas weights */

	unsigned long numPoints; /* # of points to split up */

	unsigned int tags;           /* # of flag tags during reward interval */
	unsigned int steals;         /* # of flag steals during reward interval */
	unsigned int recoveries;     /* # of flag recoveries during reward interval */
	unsigned int lost;           /* # of flag losses during reward interval */

	unsigned int kills;          /* # of kills during reward interval */
	unsigned int bountyExchanged;/* sum of bounties from kills */

	struct TurfFlag *flags;      /* pointer to array of turf flags */

	LinkedList teams;            /* all TurfTeams */
	LinkedList validTeams;       /* teams that passed minimum requirements */
	LinkedList invalidTeams;     /* teams that didn't pass minimum requirements */

	LinkedList players;          /* all TurfPlayers */
	LinkedList playersPts;       /* players playing and not in nonPlayers */
	LinkedList playersNoPts;     /* spectators, ppl entering arena, etc */

	/* this struct is used to store the status set by a scoring module */
	TurfState calcState;
} TurfArena; 

typedef enum RewardMessage_t
{
	TR_RM_PLAYER,      /* message to a player */
	TR_RM_PLAYER_MAX,  /* message to a player, maximum pts awarded */
	TR_RM_INVALID_TEAM,/* message to a player on an invalid team */
	TR_RM_NON_PLAYER   /* message to a non-player */
} RewardMessage_t;

#define I_TURFREWARD_POINTS "turfreward-points"
typedef struct Iturfrewardpoints
{
	INTERFACE_HEAD_DECL
	void (*CalcReward)(Arena *arena, struct TurfArena *ta);
	/*
	 * This will be called by the turf_reward module when points should be 
	 * awarded. It should figure out and fill in data stored in the TurfArena
	 * struct *ta (parameter).  
	 *
	 * Upon call to this function: 
	 * - The ta->teams linked list has a TurfTeam node for every team that
	 *   existed at any time during the past reward period.
	 * - The ta->validTeams linked list has a TurfTeam node for every team that 
	 *   passed the minimum requirements (conf settings).
	 * - The ta->players linked list already had a TurfPlayer node for every 
	 *   player that existed at any time during the past reward period.  A 
     *   player that played on more than one team will have separate nodes that
	 *   are linked to each team.  Every TurfPlayer node is guaranteed to be
	 *   linked to a TurfTeam node.
	 * - The ta->nonPlayers linked list contains nodes for all non-players.  A
	 *   non-player is:
	 *     1. someone marked as not playing (ex. entering arena)
	 *     2. someone in spectator mode (based on conf setting)
	 *     3. someone in a safe zone (based on conf setting)
	 * - Each team in ta->validTeams has
	 *     1. flags linked list of TurfFlags owned by the team
	 *     2. players linked list of TurfPlayers 'linked' to the team
	 *        Note: a TurfPlayer node is 'linked' if:
	 *          - player had played on the team at anytime during the previous
	 *            reward period
	 *          - team owns a flag that was tagged by that player
	 *     3. playersPts linked list of TurfPlayers that are playing
	 *     4. playersNoPts linkes list of TurfPlayers that are not playing
	 *        (same as ta->nonPlayers but for each team)
	 *
	 * To calculate, a normal scoring module will go through each team in 
	 * ta->validTeams and use the team->playersPts linked list to access
	 * each player's statistics.
	 *
	 * The module that registers this interface should fill in numPoints for 
	 * each team, each player, or both.  Obviously, to not award a team points, 
	 * set numPoints = 0.  By default, numPoints=0 for all players and teams. 
	 * The module has access to the arena's TurfArena data for the arena through 
	 * *ta.  There is no restriction on what can be altered within the TurfArena
	 * structure. Of course, bad data can be filled in, so use caution.
	 *
	 * The module that registers this interface should fill in the ta->calcState
	 * structure in order to tell turf_reward what to do after calling this
	 * function.
	 *
	 * The turf_reward lock for this arena is already in place upon call to this
	 * function.
	 *
	 * Parameters:
	 *     *arena - the arena being scored
	 *     *ta    - pointer to the TurfReward data for the specified arena
	 */
	 
	 void (*RewardMessage)(Player *player, 
	                       struct TurfPlayer *tplayer, 
	                       struct TurfArena *tarena,
	                       RewardMessage_t messageType,
	                       unsigned int pointsAwarded);
	 /* When awarding a player points, turf_reward calls this function.  The
	  * connected reward algorithm/points module can define a function to
	  * send a customized message to the player being awarded.  However, of
	  * course it is not limited to only doing that.
	  *
	  * Noe: Player data is already locked prior to this call.
	  *
	  * Parameters: 
	  *   player  - pointer to player being awarded
	  *   tplayer - pointer to the appropriate the player's turf_reward data
	  *   tarena  - pointer to the entire arena's data
	  *   messageType   - type of reward message
	  *                   (see definition of RewardMessage_t enum)
	  *   pointsAwarded - # of points awarded to player (this is usually 
	  *                   equal to tplayer->points, unless it is over max)
	  */

	/* Called before removing a TurfPlayer node 
	 * - useful for cleanup when attaching data to void* extra */
	void (*RemoveTurfPlayer)(struct TurfPlayer *pPlayer);

	/* Called before removing a TurfTeam node
	 * - useful for cleanup when attaching data to void* extra */
	void (*RemoveTurfTeam)(struct TurfTeam *pTeam);
} Iturfrewardpoints;


#define I_TURFREWARD "turfreward-1"
typedef struct Iturfreward
{
	INTERFACE_HEAD_DECL

	void (*ResetFlagGame)(Arena *arena);
	/* a utility function to reset all flag data INCLUDING flag data in
	 * the flags module */

	void (*ResetTimer)(Arena *arena);
	/* a utility function to reset the ding timer for an arena */

	int (*DoReward)(Arena *arena);
	/* a utility function to force a ding to occur immedately */

	struct TurfArena * (*GetTurfData)(Arena *arena);
	void (*ReleaseTurfData)(Arena *arena);
	/* gets turf data for an arena. always release it when you're done */
} Iturfreward;


/* for enum conf settings */
/* reward style settings */
#define TR_STYLE_MAP(F) \
	F(TR_STYLE_DISABLED)  /* disable rewards */  \
	F(TR_STYLE_PERIODIC)  /* simple periodic scoring */  \
	F(TR_STYLE_STANDARD)  /* standard weighted scoring */  \
	F(TR_STYLE_STD_BTY)   /* standard + pot based on bounty exchanged */  \
	F(TR_STYLE_WEIGHTS)   /* number of weights = number of points awarded */  \
	F(TR_STYLE_FIXED_PTS) /* each team gets a fixed # of points based on 1st, 2nd, 3rd, ... place */

/* weight calculation settings */
#define TR_WEIGHT_MAP(F) \
	F(TR_WEIGHT_DINGS) /* weight calculation based on dings */  \
	F(TR_WEIGHT_TIME)  /* weight calculation based on time  */

/* recovery system settings */
#define TR_RECOVERY_MAP(F) \
	F(TR_RECOVERY_DINGS)          /* recovery cutoff based on RecoverDings */  \
	F(TR_RECOVERY_TIME)           /* recovery cutoff based on RecoverTime */  \
	F(TR_RECOVERY_DINGS_AND_TIME) /* recovery cutoff based on both RecoverDings and RecoverTime */

DEFINE_ENUM(TR_STYLE_MAP)
DEFINE_ENUM(TR_WEIGHT_MAP)
DEFINE_ENUM(TR_RECOVERY_MAP)

#endif

