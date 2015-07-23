
#ifndef DOMINATION_H
#define DOMINATION_H

/**
 * Possible states for the domination game
 */
typedef enum {
  /* No game is active, flags are disabled and will be neutralized if touched */
  DOM_GAME_STATE_INACTIVE,

  /* No game is active, but the countdown to a new game has started */
  DOM_GAME_STATE_STARTING,

  /* A domination game is active */
  DOM_GAME_STATE_ACTIVE,

  /* A game is active, but currently paused. */
  DOM_GAME_STATE_PAUSED,

  /* The current game has just finished and modules are processing the end-of-game event */
  DOM_GAME_STATE_FINISHED,

  /* An error occurred at some point during the game. Once in this state, any existing game will
   * lost and no new games will be started. */
  DOM_GAME_STATE_ERROR
} DomGameState;

/**
 * Possible states for a region
 */
typedef enum {
  /* Region is completely uncontrolled -- no flags are controlled */
  DOM_REGION_STATE_NEUTRAL,

  /* Region is being captured by a team */
  DOM_REGION_STATE_CAPTURING,

  /* Region is contested, but all contesting teams have an equal amount of influence */
  DOM_REGION_STATE_CONTESTED,

  /* Region is fully controlled by a single team */
  DOM_REGION_STATE_CONTROLLED

} DomRegionState;

/**
 * Possible states for a flag
 */
typedef enum {
  /* Flag has not yet been touched by a team */
  DOM_FLAG_STATE_NEUTRAL,

  /* Flag has been touched and will flip to capturing or controlled if it is not touched for a bit */
  DOM_FLAG_STATE_CONTESTED,

  /* Flag has been touched and the team is capturing the flag */
  DOM_FLAG_STATE_CAPTURING,

  /* Flag has been captured and is now controlled by a team */
  DOM_FLAG_STATE_CONTROLLED,

} DomFlagState;

typedef struct DomArena DomArena;
typedef struct DomTeam DomTeam;
typedef struct DomPlayer DomPlayer;
typedef struct DomRegion DomRegion;
typedef struct DomFlag DomFlag;


// Callbacks
/**
 * Called when a region is changing states
 */
#define CB_DOM_REGION_STATE_CHANGED "dom_flag_region_changed-1"
typedef void (*DomRegionStateChangeFunc)(Arena *arena, DomRegion *region, DomRegionState old_state, DomRegionState new_state);

/**
 * Called when a flag is changing states
 */
#define CB_DOM_FLAG_STATE_CHANGED "dom_flag_state_changed-1"
typedef void (*DomFlagStateChangeFunc)(Arena *arena, DomFlag *flag, DomFlagState old_state, DomFlagState new_state);



#endif
