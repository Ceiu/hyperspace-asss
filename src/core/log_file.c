
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "asss.h"
#include "log_file.h"


local void LogFile(const char *);
local void FlushLog(void);
local void ReopenLog(void);
local int flush_timer(void *dummy);
local int reopen_timer(void *dummy);

local FILE *logfile;
local pthread_mutex_t logmtx = PTHREAD_MUTEX_INITIALIZER;

local Iconfig *cfg;
local Ilogman *lm;
local Imainloop *ml;

local Ilog_file _lfint =
{
	INTERFACE_HEAD_INIT(I_LOG_FILE, "log_file")
	FlushLog, ReopenLog
};

EXPORT const char info_log_file[] = CORE_MOD_INFO("log_file");

EXPORT int MM_log_file(int action, Imodman *mm, Arena *arenas)
{
	if (action == MM_LOAD)
	{
		int fp;
		int nfp;

		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

		if (!cfg || !lm || !ml) return MM_FAIL;

		logfile = NULL;
		ReopenLog();

		mm->RegCallback(CB_LOGFUNC, LogFile, ALLARENAS);

		/* cfghelp: Log:FileFlushPeriod, global, int, def: 10
		 * How often to flush the log file to disk (in minutes). */
		fp = cfg->GetInt(GLOBAL, "Log", "FileFlushPeriod", 10);
		if (fp > 0)
			ml->SetTimer(flush_timer, fp * 60 * 100, fp * 60 * 100, NULL, NULL);

		/* cfghelp: Log:UseDatedLogs, global, bool, def: 0
		 * Whether to use filenames in the format YYMMDD.log */
		if (cfg->GetInt(GLOBAL, "Log", "UseDatedLogs", 0))
		{
			/* cfghelp: Log:NewFilePeriod, global, int, def: 3
			 * How often to open a new log file (in days).
			 * Has no effect when writing to an undated log file.
			 * If less than 1, the same file will be used until the server restarts. */
			nfp = cfg->GetInt(GLOBAL, "Log", "NewFilePeriod", 3);
			if (nfp > 0)
				ml->SetTimer(reopen_timer, 100 * 60 * 60 * 24 * nfp, 100 * 60 * 60 * 24 * nfp, NULL, NULL);
		}

		mm->RegInterface(&_lfint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_lfint, ALLARENAS))
			return MM_FAIL;

		ml->ClearTimer(reopen_timer, NULL);
		ml->ClearTimer(flush_timer, NULL);
		mm->UnregCallback(CB_LOGFUNC, LogFile, ALLARENAS);

		pthread_mutex_lock(&logmtx);
		if (logfile)
			fclose(logfile);
		pthread_mutex_unlock(&logmtx);

		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(ml);
		return MM_OK;
	}
	return MM_FAIL;
}


void LogFile(const char *s)
{
	if (lm->FilterLog(s, "log_file"))
	{
		pthread_mutex_lock(&logmtx);
		if (logfile)
		{
			struct tm _tm;
			time_t t;
			char t3[128];

			time(&t);
			alocaltime_r(&t, &_tm);

			strftime(t3, sizeof(t3), CFG_TIMEFORMAT, &_tm);
			fputs(t3, logfile);
			fputs(" ", logfile);
			fputs(s, logfile);
			fputs("\n", logfile);
		}
		pthread_mutex_unlock(&logmtx);
	}
}

void FlushLog(void)
{
	pthread_mutex_lock(&logmtx);
	if (logfile) fflush(logfile);
	pthread_mutex_unlock(&logmtx);
}

int reopen_timer(void *dummy)
{
	ReopenLog();
	return TRUE;
}

int flush_timer(void *dummy)
{
	FlushLog();
	return TRUE;
}

void ReopenLog(void)
{
	int useDatedLogs;
	char finalName[256];

	pthread_mutex_lock(&logmtx);

	if (logfile)
		fclose(logfile);

	/* cfghelp is in mm_load */
	useDatedLogs = cfg->GetInt(GLOBAL, "Log", "UseDatedLogs", 0);

	if (useDatedLogs)
	{
		char datedName[64];
		const char *datedLogPath;
		time_t currentTime;

		/* cfghelp: Log:DatedLogsPath, global, string, def: log
		 * If using dated log files, the path to put the files in. */
		datedLogPath = cfg->GetStr(GLOBAL, "Log", "DatedLogsPath");
		if (!datedLogPath) datedLogPath = "log";

		time(&currentTime);
		strftime(datedName, sizeof(datedName), "%Y%m%d", localtime(&currentTime));

		snprintf(finalName, sizeof(finalName), "%s/%s.log", datedLogPath, datedName);
	}
	else
	{
		const char *logName;

		/* cfghelp: Log:LogFile, global, string, def: asss.log
		 * The name of the log file.
		 * Has no effect when using dated log files. */
		logName = cfg->GetStr(GLOBAL, "Log", "LogFile");
		if (!logName) logName = "asss.log";

		/* if it has a /, treat it as an absolute path. otherwise, prepend
		 * 'log/' */
		if (strchr(logName, '/'))
			astrncpy(finalName, logName, sizeof(finalName));
		else
			snprintf(finalName, sizeof(finalName), "log/%s", logName);
	}

	logfile = fopen(finalName, "a");

	pthread_mutex_unlock(&logmtx);

	LogFile("\nI <log_file> opening log file ==================================\n");
}

