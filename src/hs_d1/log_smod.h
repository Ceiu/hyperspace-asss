
/* dist: public */

#ifndef __LOG_SMOD_H
#define __LOG_SMOD_H


#define I_LOG_SMOD "log_smod-1"

typedef struct Ilog_file
{
	INTERFACE_HEAD_DECL

	void (*FlushLog)(void);
	/* flushes the current log file to disk */

	void (*ReopenLog)(void);
	/* closes and reopens the current log file */
} Ilog_smod;

#endif

