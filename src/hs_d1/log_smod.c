
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "asss.h"
#include "log_smod.h"


local void LogFile(const char *);
local void FlushLog(void);
local void ReopenLog(void);
local int flush_timer(void *dummy);

local FILE *logfile;
local pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

local Iconfig *cfg;
local Ilogman *lm;
local Imainloop *ml;
local Icmdman *cmd;

local Ilog_smod _lfint =
{
	INTERFACE_HEAD_INIT(I_LOG_SMOD, "log_smod")
	FlushLog, ReopenLog
};


local helptext_t smodlog_help =
"Targets: none\n"
"Args: {flush} or {reopen}\n"
"Administers the log file that the server keeps. There are two possible\n"
"subcommands: {flush} flushes the log file to disk (in preparation for\n"
"copying it, for example), and {reopen} tells the server to close and\n"
"re-open the log file (to rotate the log while the server is running).\n";

local void Csmodlog(const char *tc, const char *params, Player *p, const Target *target)
{
	if (!strcasecmp(params, "flush"))
		FlushLog();
	else if (!strcasecmp(params, "reopen"))
		ReopenLog();
}

EXPORT int MM_log_smod(int action, Imodman *mm, Arena *arenas)
{
	if (action == MM_LOAD)
	{
		int fp;

		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);

		if (!cfg || !lm || !ml || !cmd) return MM_FAIL;

		logfile = NULL;
		ReopenLog();

		mm->RegCallback(CB_LOGFUNC, LogFile, ALLARENAS);

		/* cfghelp: Log:FileFlushPeriod, global, int, def: 10
		 * How often to flush the log file to disk (in minutes). */
		fp = cfg->GetInt(GLOBAL, "Log", "FileFlushPeriod", 10);

		if (fp)
			ml->SetTimer(flush_timer, fp * 60 * 100, fp * 60 * 100, NULL, NULL);

		mm->RegInterface(&_lfint, ALLARENAS);
		cmd->AddCommand("smodlog", Csmodlog, ALLARENAS, smodlog_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("smodlog", Csmodlog, ALLARENAS);

		if (mm->UnregInterface(&_lfint, ALLARENAS))
			return MM_FAIL;
		if (logfile)
			fclose(logfile);
		ml->ClearTimer(flush_timer, NULL);
		mm->UnregCallback(CB_LOGFUNC, LogFile, ALLARENAS);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);		
		return MM_OK;
	}
	return MM_FAIL;
}


void LogFile(const char *s)
{
	if (lm->FilterLog(s, "log_smod"))
	{
		pthread_mutex_lock(&logmtx);
		if (logfile)
		{
			time_t t1;
			char t3[128];

			time(&t1);
			strftime(t3, sizeof(t3), CFG_TIMEFORMAT, localtime(&t1));
			fputs(t3, logfile);
			putc(' ', logfile);
			fputs(s, logfile);
			putc('\n', logfile);
		}
		pthread_mutex_unlock(&logmtx);
	}
}

void FlushLog(void)
{
	pthread_mutex_lock(&logmtx);
	if (logfile)
	{
		fflush(logfile);
		lm->Log('I', "<log_smod> Flushing log to disk.");
	}
	pthread_mutex_unlock(&logmtx);
}

int flush_timer(void *dummy)
{
	FlushLog();
	return TRUE;
}

void ReopenLog(void)
{
	const char *ln;
	char fname[256];

	pthread_mutex_lock(&logmtx);

	if (logfile)
		fclose(logfile);

	/* cfghelp: Log:LogFile, global, string, def: asss.log
	 * The name of the log file. */
	ln = cfg->GetStr(GLOBAL, "Log", "SmodLogFile");
	if (!ln) ln = "smod.log";

	/* if it has a /, treat it as an absolute path. otherwise, prepend
	 * 'log/' */
	if (strchr(ln, '/'))
		astrncpy(fname, ln, sizeof(fname));
	else
		snprintf(fname, sizeof(fname), "log/%s", ln);

	logfile = fopen(fname, "a");

	pthread_mutex_unlock(&logmtx);

	LogFile("I <log_smod> opening log file ==================================");
}



