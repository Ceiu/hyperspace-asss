
/* dist: public */

#include "asss.h"
#include "clocks.h"

typedef struct ClockEvent
{
	ClockEventType type;
	Clock *clock;
	int param;
	ClockEventFunc fn;
	void *clos;
} ClockEvent;

local LinkedList myclocks;
local LinkedList myclockevents;
local ticks_t mycounter;

local int synchroCounter;
local pthread_mutex_t mymutex;
#define LOCK pthread_mutex_lock(&mymutex)
#define UNLOCK pthread_mutex_unlock(&mymutex)

local Clock * NewClock(ClockType, int time);
local void FreeClock(Clock *);
local int ReadClock(Clock *);
local void SetClock(Clock *, int time);
local void StartClock(Clock *);
local void StopClock(Clock *);
local void ChangeClock(Clock *, ClockType);
local void RegisterClockEvent(Clock *, ClockEventType type, int param, ClockEventFunc, void *clos);
local void ClearEventsWithClos(Clock *, void *clos);
local int isClockRunning(Clock *);
local void HoldForSynchronization(void);
local void DoneHolding(void);

local void mainloop(void);

#ifndef FOR_EACH
#define FOR_EACH(l,v,u) \
	for ( \
			u = LLGetHead(l); \
			u && ((v = u->data, u = u->next) || 1); )
#endif

local Clock * NewClock(ClockType type, int time)
{
	Clock *clock = amalloc(sizeof(*clock));
	clock->last_reading = time;
	clock->count_up = (type == CLOCK_COUNTSUP)?1:0;
	clock->is_running = 0;
	clock->event_check = 0;

	LOCK;
	LLAdd(&myclocks, clock);
	UNLOCK;


	return clock;
}

local void FreeClock(Clock *clock)
{
	Link *link;
	ClockEvent *eve;


	LOCK;
	FOR_EACH(&myclockevents, eve, link)
	{
		if (eve->clock == clock)
		{
			afree(eve);
			LLRemove(&myclockevents, eve);
		}
	}

	afree(clock);
	LLRemove(&myclocks, clock);
	UNLOCK;

}

local int ReadClock(Clock *clock)
{
	int result;



	if (clock->is_running)
	{
		int time_elapsed = TICK_DIFF(mycounter, clock->start_time);

		if (clock->count_up)
		{
			result = clock->last_reading + time_elapsed;
		}
		else if (time_elapsed < clock->last_reading)
		{
			result = clock->last_reading - time_elapsed;
		}
		else
		{
			clock->is_running = 0;
			clock->last_reading = 0;
			clock->event_check = 1; //though this clock has stopped, we want to check its events one time.
			result = 0;
		}
	}
	else
	{
		result = clock->last_reading;
	}



	return result;
}

local void SetClock(Clock *clock, int time)
{


	if (clock->is_running)
	{
		clock->event_check_time = mycounter;
		clock->start_time = mycounter;
		clock->last_reading = time;
	}
	else
	{
		clock->event_check = 0;
		clock->last_reading = time;
	}


}

local void StartClock(Clock *clock)
{


	if (clock->is_running)
	{
		clock->last_reading = ReadClock(clock);
	}
	else if (!clock->event_check)
	{
		clock->event_check_time = mycounter;
	}

	clock->is_running = 1;
	clock->start_time = mycounter;


}

local void StopClock(Clock *clock)
{


	if (clock->is_running)
	{
		clock->last_reading = ReadClock(clock);
		clock->event_check = 1;
		clock->is_running = 0;
	}


}

local void ChangeClock(Clock *clock, ClockType type)
{


	if (clock->is_running)
	{
		//reset the current reading so we can change direction
		StartClock(clock);
	}

	clock->count_up = (type == CLOCK_COUNTSUP)?1:0;


}

local void RegisterClockEvent(Clock *clock, ClockEventType type, int param, ClockEventFunc fn, void *clos)
{
	ClockEvent *eve = amalloc(sizeof(*eve));
	eve->type = type;
	eve->clock = clock;
	eve->param = param;
	eve->fn = fn;
	eve->clos = clos;

	LOCK;
	LLAddFirst(&myclockevents, eve);
	UNLOCK;
}

local void ClearEventsWithClos(Clock *clock, void *clos)
{
	Link *link;
	ClockEvent *eve;

	LOCK;

	FOR_EACH(&myclockevents, eve, link)
	{
		if (eve->clock != clock)
			continue;
		if (eve->clos == clos)
		{
			afree(eve);
			LLRemove(&myclockevents, eve);
		}
	}

	UNLOCK;
}

local int isClockRunning(Clock *clock)
{
	int result;


	result = clock->is_running;


	return result;
}

local void HoldForSynchronization(void)
{

	++synchroCounter;

}

local void DoneHolding(void)
{

	--synchroCounter;

}

local void mainloop(void)
{
	Link *link;
	ClockEvent *eve;
	Clock *clock = 0;



	if (synchroCounter <= 0) //if someone unlocks twice they're horrible but no reason to be too picky, just means loss of synchronization ability
	{
		mycounter = current_millis();

		LOCK;

		FOR_EACH(&myclockevents, eve, link)
		{
			int reading;
			int oldreading;
			int old_time_elapsed;
			int callFn = 0;
			int eventTime;

			clock = eve->clock;

			if (!clock->is_running && !clock->event_check)
				continue;

			old_time_elapsed = TICK_DIFF(clock->event_check_time, clock->start_time);

			reading = ReadClock(clock);

			if (clock->count_up)
			{
				oldreading = clock->last_reading + old_time_elapsed;
			}
			else if (old_time_elapsed < clock->last_reading)
			{
				oldreading = clock->last_reading - old_time_elapsed;
			}
			else
			{
				//since we're here we have to assume some distance. (events should never have a resolution of 1ms anyway)
				oldreading = 1;
			}

			if (eve->type == CLOCKEVENT_INTERVAL)
			{
				//we want the event to be called when the clock has touched exact multiple of the interval (0, 1000, 2000, etc.)
				//if counting down we add (interval - 1)
				//counting up:
				//so in an interval of 1000, if the clock reads 1000, the newDividend == 1000 / 1000 == 1.
				//if the previous reading was 999, oldDividend == 999 / 1000 == 0.
				//counting down:
				//so in an interval of 1000, if the clock reads 1000, the newDividend == (1000 + 999) / 1000 == 1.
				//if the previous reading was 1001, oldDividend == (1001 + 999) / 1000 == 2.

				int newDividend;
				int oldDividend;

				if (clock->count_up)
				{
					newDividend = reading / eve->param;
					oldDividend = oldreading / eve->param;
				}
				else
				{
					newDividend = (reading + (eve->param - 1)) / eve->param;
					oldDividend = (oldreading + (eve->param - 1)) / eve->param;
				}

				if (newDividend != oldDividend)
				{
					eventTime = newDividend * eve->param;
					callFn = 1;
				}
			}
			else
			{
				//this one is easier. if both the new reading and the oldreading are less than/greater than the critical time, then this time hasn't been hit yet.
				if ((reading <= eve->param && oldreading > eve->param)
					|| (reading >= eve->param && oldreading < eve->param))
					callFn = 1;
			}

			if (callFn)
			{
				int fnResult = eve->fn(clock, eventTime, eve->clos);
				if (fnResult == DISCONTINUE_EVENT)
				{
					afree(eve);
					LLRemove(&myclockevents, eve);
				}
			}
		}

		FOR_EACH(&myclocks, clock, link)
		{
			clock->event_check_time = mycounter;
			clock->event_check = 0;
		}

		UNLOCK;
	}


}

local Iclocks myint =
{
	INTERFACE_HEAD_INIT(I_CLOCKS, "clocks")
	NewClock, FreeClock, ReadClock, SetClock,
	StartClock, StopClock, ChangeClock, RegisterClockEvent,
	ClearEventsWithClos, isClockRunning, HoldForSynchronization, DoneHolding
};

EXPORT int MM_clocks(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&mymutex, &attr);
		pthread_mutexattr_destroy(&attr);

		LLInit(&myclocks);
		LLInit(&myclockevents);

		synchroCounter = 0;

		mm->RegCallback(CB_MAINLOOP, mainloop, ALLARENAS);

		mm->RegInterface(&myint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_LOAD)
	{
		if (mm->UnregInterface(&myint, ALLARENAS))
			return MM_FAIL;

		mm->UnregCallback(CB_MAINLOOP, mainloop, ALLARENAS);

		pthread_mutex_destroy(&mymutex);

		LLEnum(&myclocks, afree);
		LLEnum(&myclockevents, afree);
		LLEmpty(&myclocks);
		LLEmpty(&myclockevents);


		return MM_OK;
	}
	return MM_FAIL;
}

