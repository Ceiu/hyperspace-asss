
/* dist: public */

#ifndef __LOGMAN_H
#define __LOGMAN_H

/** @file
 * this file describes the Ilogman interface, used to add stuff to the
 * server log.
 */

/* Ilogman - manages logging
 *
 * Log itself is used to add a line to the server log. it accepts
 * a variable number of parameters, so you can use it like printf and
 * save yourself a call to sprintf.
 *
 * log messages should be in the form:
 * "<module> {arena} [player] did something"
 * for easy filtering and searching through log files.
 * arena or player may be left out if not applicable.
 * if a player name is not available, "[pid=123]" should be used
 * instead.
 *
 */


/* priority levels */
/* pyconst: define int, "L_*" */
#define L_DRIVEL     'D'  /**< really useless info */
#define L_INFO       'I'  /**< informative info */
#define L_MALICIOUS  'M'  /**< bad stuff from the client side */
#define L_WARN       'W'  /**< something bad, but not too bad */
#define L_ERROR      'E'  /**< something really really bad */
#define L_CRITICAL   'C'  /**< not an error, but equally important */

/** or this in with another log level to force the message to be logged
 ** synchronously.
 * you don't want to use this if you don't know what you're doing.
 */
#define L_SYNC       0x80


/** this callback is called for each log line.
 * log handlers should register to receive these events, and then do
 * something with each line. */
#define CB_LOGFUNC "log"
/** the type of log handlers. */
typedef void (*LogFunc)(const char *line);
/* pycb: string */


/** the Ilogman interface id */
#define I_LOGMAN "logman-2"

/** the Ilogman interface struct */
typedef struct Ilogman
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Adds a line to the server log.
	 * Lines should look like:
	 * "<module> {arena} [player] did something"
	 * arena or player may be left out if not applicable.
	 * [pid=%d] may be substituted if the player name isn't known yet.
	 * @param level one of the L_SOMETHING level codes.
	 * @param format a printf-style format string
	 */
	void (*Log)(char level, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: int, formatted -> void */


	/** Adds a line to the server log, specialized for arena-specific
	 ** messages.
	 * @param level a log level (L_SOMETHING)
	 * @param the module name (without angle brackets)
	 * @param a the arena this message is describing
	 * @param format a printf-style format string
	 */
	void (*LogA)(char level, const char *mod, Arena *a, const char *format, ...)
		ATTR_FORMAT(printf, 4, 5);
	/* pyint: int, string, arena, formatted -> void */

	/** Adds a line to the server log, specialized for player-specific
	 ** messages.
	 * @param level a log level (L_SOMETHING)
	 * @param the module name (without angle brackets)
	 * @param p the player this message is describing
	 * @param format a printf-style format string
	 */
	void (*LogP)(char level, const char *mod, Player *p, const char *format, ...)
		ATTR_FORMAT(printf, 4, 5);
	/* pyint: int, string, player, formatted -> void */


	/** Determines if a specific message should be logged by a specific
	 ** module.
	 * Log handlers can optionally call this function to support
	 * filtering of the log messages that go through them. The filters
	 * are be defined by an administrator in global.conf.
	 * @param line the log line that was received by the log handler
	 * @param modname the module name of the log handler (e.g., log_file)
	 */
	int (*FilterLog)(const char *line, const char *modname);
	/* pyint: string, string -> int */
} Ilogman;


#endif

