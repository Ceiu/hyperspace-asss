
/* dist: public */

#ifndef __MAINLOOP_H
#define __MAINLOOP_H


/** @file
 * this file contains Imainloop and related definitions. it deals with
 * timers and server shutdown.
 */

/** timer functions must be of this type.
 * @param param is a closure argument
 * @return true if the timer wants to continue running, false if it
 * wants to be cancelled
 */
typedef int (*TimerFunc)(void *param);

/** timer cleanup functions must be of this type.
 * @param param the same closure argument that gets passed to the timer
 * function
 */
typedef void (*CleanupFunc)(void *param);

/** threadpool work functions must be of this type. */
typedef void (*WorkFunc)(void *param);

/** this callback is called once per iteration of the main loop.
 * probably a few hundred times per second.
 */
#define CB_MAINLOOP "mainloop"
/** the type of CB_MAINLOOP callbacks */
typedef void (*MainLoopFunc)(void);
/* no python in the main loop */


/** the interface id for Imainloop */
#define I_MAINLOOP "mainloop-3"

/** the interface struct for Imainloop */
typedef struct Imainloop
{
	INTERFACE_HEAD_DECL

	/** Starts a timed event.
	 * @param func the TimerFunc to call
	 * @param initialdelay how long to wait from now until the first
	 * call (in ticks)
	 * @param interval how long to wait between calls (in ticks)
	 * @param param a closure argument that will get passed to the timer
	 * function
	 * @param key a key that can be used to selectively cancel timers
	 */
	void (*SetTimer)(TimerFunc func, int initialdelay, int interval,
			void *param, void *key);

	/** Clears timers.
	 * This can either clear all timers running a specific function, or
	 * a subset of them with a specific key.
	 * You should always call this (or CleanupTimer) during module
	 * unloading if you set any timers.
	 * @param func the timer function you want to clear
	 * @param key timers that match this key will be removed. using NULL
	 * means to clear all timers with the given function, regardless of
	 * key.
	 */
	void (*ClearTimer)(TimerFunc func, void *key);

	/** Clears timers, with a cleanup handler.
	 * Does the same as ClearTimer, but takes an optional cleanup
	 * handler, which will get called with each timer's closure
	 * argument.
	 * @see Imainloop::ClearTimer
	 * @param cleanup a CleanupFunc to call once for each timer being
	 * cancelled
	 */
	void (*CleanupTimer)(TimerFunc func, void *key, CleanupFunc cleanup);

	/** Runs the server main loop.
	 * This is called exactly once, from main()
	 */
	int (*RunLoop)(void);
	/** Signals the main loop to quit.
	 * @param code the exit code to be returned to the OS
	 */
	void (*Quit)(int code);

	/** Runs the given function on some other thread. */
	void (*RunInThread)(WorkFunc func, void *param);
} Imainloop;


#endif

