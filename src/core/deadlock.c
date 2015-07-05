
/* dist: public */

#ifndef WIN32
#include <unistd.h>
#endif
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "asss.h"

local pthread_t thd;
local volatile int counter;

local void * thread_check(void *dummy)
{
#ifdef WIN32
	/* nanosleep isn't a cancellation point on windows, so we use
	 * asynchronous cancellation there. */
	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
#endif

	/* wait a minute initially */
	fullsleep(60 * 1000);

	for (;;)
	{
		int seen = counter;
		/* and then 10 seconds each iteration */
		fullsleep(10 * 1000);
		if (counter == seen)
		{
			fprintf(stderr, "E <deadlock> deadlock detected, aborting\n");
			abort();
		}
	}
}

local void increment(void)
{
	counter++;
}

EXPORT const char info_deadlock[] = CORE_MOD_INFO("deadlock");

EXPORT int MM_deadlock(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		pthread_create(&thd, NULL, thread_check, NULL);
		mm->RegCallback(CB_MAINLOOP, increment, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_MAINLOOP, increment, ALLARENAS);
		pthread_cancel(thd);
		pthread_join(thd, NULL);
		return MM_OK;
	}
	return MM_FAIL;
}

