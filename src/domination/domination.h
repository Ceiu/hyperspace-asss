
#ifndef DOMINATION_H
#define DOMINATION_H

#define DOM_NEUTRAL_FREQ -1

/**
 * Possible states for the domination game
 */
typedef enum {
  /* Unknown game state */
  DOM_GAME_STATE_UNKNOWN,

  /* No game is active, flags are disabled and will be neutralized if touched */
  DOM_GAME_STATE_INACTIVE,

  /* A domination game is active */
  DOM_GAME_STATE_ACTIVE,

  /* A game is active, but currently paused. */
  DOM_GAME_STATE_PAUSED,

  /* The current game has just finished and modules are processing the end-of-game event */
  DOM_GAME_STATE_FINISHED,

  /* An error occurred at some point during the game. Once in this state, any existing game will be
   * lost */
  DOM_GAME_STATE_ERROR
} DomGameState;

/**
 * Possible states for a region
 */
typedef enum {
  /* Unknown region state */
  DOM_REGION_STATE_UNKNOWN,

  /* Region is completely uncontrolled -- no flags are controlled */
  DOM_REGION_STATE_NEUTRAL,

  /* Region is being captured by a team */
  DOM_REGION_STATE_CAPTURING,

  /* Region is fully controlled by a single team */
  DOM_REGION_STATE_CONTROLLED
} DomRegionState;

/**
 * Possible states for a flag
 */
typedef enum {
  /* Unknown flag state */
  DOM_FLAG_STATE_UNKNOWN,

  /* Flag has not yet been touched by a team */
  DOM_FLAG_STATE_NEUTRAL,

  /* Flag has been touched and will flip to capturing or controlled if it is not touched for a bit */
  DOM_FLAG_STATE_CONTESTED,

  /* Flag has been touched and the team is capturing the flag */
  DOM_FLAG_STATE_CAPTURING,

  /* Flag has been captured and is now controlled by a team */
  DOM_FLAG_STATE_CONTROLLED,

} DomFlagState;

typedef u_int32_t DomAlertType;

/* The game will be starting soon */
#define DOM_ALERT_STARTING    1
/* A team has controlled every region and will win shortly if uncontested */
#define DOM_ALERT_DOMINATION  2

typedef enum {
  /* The alert is starting */
  DOM_ALERT_STATE_STARTING,

  /* The alert is ending */
  DOM_ALERT_STATE_ENDING
} DomAlertState;


typedef struct DomArena DomArena;
typedef struct DomTeam DomTeam;
typedef struct DomPlayer DomPlayer;
typedef struct DomRegion DomRegion;
typedef struct DomFlag DomFlag;


// Callbacks
/**
 * Called when the game is changing states
 */
#define CB_DOM_GAME_STATE_CHANGED "dom_game_state_changed-cb-1"
typedef void (*DomGameStateChangedFunc)(Arena *arena, DomGameState prev_state, DomGameState new_state);

/**
 * Called when a region is changing states
 */
#define CB_DOM_REGION_STATE_CHANGED "dom_region_state_changed-cb-1"
typedef void (*DomRegionStateChangedFunc)(Arena *arena, DomRegion *dregion, DomRegionState prev_state, DomRegionState new_state);

/**
 * Called when a flag is changing states
 */
#define CB_DOM_FLAG_STATE_CHANGED "dom_flag_state_changed-cb-1"
typedef void (*DomFlagStateChangedFunc)(Arena *arena, DomFlag *dflag, DomFlagState prev_state, DomFlagState new_state);

/**
 * Called when a Domination game alert is triggered or is cleared
 *
 * @param *arena
 *  The arena in which the alert occurred
 *
 * @param type
 *  The alert type
 *
 * @param state
 *  Whether or not the alert is starting or ending
 *
 * @param duration
 *  The duration of the alert, in ticks; will be zero when the alert is ending or when setting an
 *  alert which is not timed or does not persist
 */
#define CB_DOM_ALERT
typedef void (*DomAlertFunc)(Arena *arena, DomAlertType type, DomAlertState state, ticks_t duration);


// Interface
#define I_DOMINATION "domination-iface-1"
typedef struct Idomination {
  INTERFACE_HEAD_DECL

  DomFlag* (*GetDomFlag)(Arena *arena, int flag_id);
  DomTeam* (*GetDomTeam)(Arena *arena, int freq);
  DomRegion* (*GetDomRegion)(Arena *arena, const char *region_name);
  int (*GetFlagProvidedInfluence)(DomFlag *dflag);
  int (*GetFlagContestTime)(DomFlag *dflag);
  int (*GetFlagCaptureTime)(DomFlag *dflag);
  int (*GetFlagAcquiredInfluence)(DomFlag *dflag, DomTeam *dteam);
  DomTeam* (*GetFlagControllingTeam)(DomFlag *dflag);
  DomTeam* (*GetFlagEntityControllingTeam)(DomFlag *dflag);
  DomFlagState (*GetFlagState)(DomFlag *dflag);
  void (*SetFlagState)(DomFlag *dflag, DomFlagState state, DomTeam *controlling_team, int acquired_influence, DomTeam *flag_entity_team);
  char* (*GetTeamName)(DomTeam *dteam);
  int (*GetTeamRegionInfluence)(DomTeam *dteam, DomRegion *dregion);
  int (*GetTeamAcquiredRegionInfluence)(DomTeam *dteam, DomRegion *dregion);
  int (*GetTeamAcquiredControlPoints)(DomTeam *dteam);
  int (*GetRegionProvidedControlPoints)(DomRegion *dregion);
  int (*GetRegionRequiredInfluence)(DomRegion *dregion);
  int (*GetRegionPotentialInfluence)(DomRegion *dregion);
  int (*GetRegionMinimumInfluence)(DomRegion *dregion);
  DomTeam* (*GetRegionControllingTeam)(DomRegion *dregion);
  DomTeam* (*GetRegionInfluentialTeam)(DomRegion *dregion);
  DomRegionState (*GetRegionState)(DomRegion *dregion);
  void (*SetRegionState)(DomRegion *dregion, DomRegionState state, DomTeam *dteam, int influence);
  DomTeam* (*GetDominatingTeam)(Arena *arena);
  DomGameState (*GetGameState)(Arena *arena);
  void (*SetGameState)(Arena *arena, DomGameState state);
  void (*SetAlertState)(Arena *arena, DomAlertType type, char enabled, ticks_t duration);
} Idomination;

#endif
