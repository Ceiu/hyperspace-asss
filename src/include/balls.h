
/* dist: public */

#ifndef __BALLS_H
#define __BALLS_H

#define MAXBALLS 8

/* Iballs
 * this module will handle all ball-related network communication.
 */


typedef enum
{
	/* pyconst: enum, "BALL_*" */
	BALL_NONE,    /* the ball doesn't exist */
	BALL_ONMAP,   /* the ball is on the map or has been fired */
	BALL_CARRIED, /* the ball is being carried */
	BALL_WAITING  /* the ball is waiting to be spawned again */
} ballstate_t;

/* called when the number of balls changes */
#define CB_BALLCOUNTCHANGE "ballcountchange"
typedef void (*BallCountChangeFunc)(Arena *arena, int newcount, int oldcount);
/* pycb: arena, int, int */

/* called when a player picks up a ball */
#define CB_BALLPICKUP "ballpickup"
typedef void (*BallPickupFunc)(Arena *arena, Player *p, int bid);
/* pycb: arena, player, int */

/* called when a player fires a ball */
#define CB_BALLFIRE "ballfire"
typedef void (*BallFireFunc)(Arena *arena, Player *p, int bid);
/* pycb: arena, player, int */

/* called when a player scores a goal */
#define CB_GOAL "goal"
typedef void (*GoalFunc)(Arena *arena, Player *p, int bid, int x, int y);
/* pycb: arena, player, int, int, int */


struct BallData
{
	/* pytype: struct, struct BallData, balldata */

	/* the state of this ball */
	int state;

	/* the coordinates of the ball */
	int x, y, xspeed, yspeed; 

	/* the player that is carrying or last touched the ball */
	Player *carrier;

	/* freq of carrier */
	int freq;

	/* the time that the ball was last fired (will be 0 for
	 * balls being held). for BALL_WAITING, this time is the
	 * time when the ball will be re-spawned. */
	ticks_t time;

	/* the time the server last got an update on ball data.
	 * it might differ from the 'time' field due to lag. */
	ticks_t last_update;
};

typedef struct ArenaBallData
{
	/* the number of balls currently in play. 0 if the arena has no ball game. */
	int ballcount;

	/* points to an array of ball states. */
	struct BallData *balls;

	/* points to an array of previous ball states. */
	struct BallData *previous;
} ArenaBallData;


#define I_BALLS "balls-5"

typedef struct Iballs
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*SetBallCount)(Arena *arena, int ballcount);
	/* sets the number of balls in the arena. if the new count is higher
	 * than the current one, new balls are spawned. if it's lower, the
	 * dead balls are "phased" in the upper left corner. */
	/* pyint: arena, int -> void  */

	void (*PlaceBall)(Arena *arena, int bid, struct BallData *newpos);
	/* sets the parameters of the ball to those in the given BallData
	 * struct */
	/* pyint: arena, int, balldata -> void */

	void (*EndGame)(Arena *arena);
	/* ends the ball game */
	/* pyint: arena -> void  */

	void (*SpawnBall)(Arena *arena, int bid);
	/* respawns the specified ball. no effect on balls that don't exist. */
	/* pyint: arena, int -> void */

	ArenaBallData * (*GetBallData)(Arena *arena);
	void (*ReleaseBallData)(Arena *arena);
	/* always release the ball data when you're done using it */
} Iballs;


#define A_BALLS "balls-adv"

typedef struct Aballs
{
	ADVISER_HEAD_DECL

	/* called on a pickup request.
	 * returning FALSE disallows the ballpickup entirely.
	 * returning TRUE changes the ball state to the data pointed to by newbd.
	 * by default newbd is the usual state of the player carrying the ball, but other advisers might change this.
	 */
	int (*AllowBallPickup)(Arena *a, Player *p, int bid, struct BallData *newbd);

	/* called when a player tries to fire a ball.
	 * returning FALSE diallows the ball fire, causing the ball to be restuck to the player.
	 * note that there will be no ball timer using this behavior.
	 * returning TRUE changes the ball state to the data pointed to by newbd.
	 * by default newbd is the state of the ball as if no advisers interfered (traveling on the map)
	 * isForced specifies whether the client is firing the ball or the module is forcing a ball fire.
	 */
	int (*AllowBallFire)(Arena *a, Player *p, int bid, int isForced, struct BallData *newbd);

	/* called when a client attempts to score a goal.
	 * returning TRUE disallows the ball goal. note that continuum will continue sending goal packets
	 * several times in this case. the ball state may be changed by modifying newbd.
	 * returning FALSE allows the ball to be scored.
	 */
	int (*BlockBallGoal)(Arena *a, Player *p, int bid, int x, int y, struct BallData *newbd);

} Aballs;


#endif

