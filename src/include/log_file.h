
/* dist: public */

#ifndef __LOG_FILE_H
#define __LOG_FILE_H


#define I_LOG_FILE "log_file-2"

typedef struct Ilog_file
{
	INTERFACE_HEAD_DECL

	void (*FlushLog)(void);
	/* flushes the current log file to disk */

	void (*ReopenLog)(void);
	/* closes and reopens the current log file */
} Ilog_file;

#endif

