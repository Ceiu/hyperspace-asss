
/* dist: public */

#ifndef __CLOCKS_H
#define __CLOCKS_H


/** @file
 * defines the Iclock interface, which is useful for timing events.
 */

typedef struct Clock
{
	//allocate with NewClock()
	//free with FreeClock()
	int last_reading;
	ticks_t start_time;
	ticks_t event_check_time;
	int count_up : 1;
	int is_running : 1;
	int event_check : 1; //even after a clock is stopped, make sure events are checked.
} Clock;

typedef enum ClockType
{
	CLOCK_COUNTSUP=0,
	CLOCK_COUNTSDOWN
} ClockType;

typedef enum ClockEventType
{
	CLOCKEVENT_SPECIFICTIME,
	CLOCKEVENT_INTERVAL
} ClockEventType;

typedef enum ClockEventResult
{
	DISCONTINUE_EVENT=0,
	KEEP_EVENT=1
} ClockEventResult;

/** clock event functions must be of this type.
 * @param param is a closure argument
 * @return whether to preserve this clock event (true), or remove it (false)
 */
typedef int (*ClockEventFunc)(Clock *clock, int time, void *clos);


/** the interface id for Iclock */
#define I_CLOCKS "akd-clocks"

/** the interface struct for Iclock */
typedef struct Iclocks
{
	INTERFACE_HEAD_DECL

	Clock * (*NewClock)(ClockType, int time);
	void (*FreeClock)(Clock *);
	int (*ReadClock)(Clock *);
	void (*SetClock)(Clock *, int time);
	void (*StartClock)(Clock *);
	void (*StopClock)(Clock *);
	void (*ChangeClock)(Clock *, ClockType);
	void (*RegisterClockEvent)(Clock *, ClockEventType type, int param, ClockEventFunc, void *clos);
	void (*ClearEventsWithClos)(Clock *, void *clos);
	int (*isClockRunning)(Clock *);
	void (*HoldForSynchronization)(void);
	void (*DoneHolding)(void);

} Iclocks;


#endif

