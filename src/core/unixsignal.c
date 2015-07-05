
/* dist: public */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <execinfo.h>

#include "asss.h"
#include "log_file.h"
#include "persist.h"

local Imodman *mm;

local volatile sig_atomic_t gotsig;


local void handle_sighup(void)
{
	Ilog_file *lf = mm->GetInterface(I_LOG_FILE, ALLARENAS);
	Iconfig *cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
	Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
	if (lf)
		lf->ReopenLog();
	if (cfg)
	{
		cfg->FlushDirtyValues();
		cfg->CheckModifiedFiles();
	}
	if (persist)
		persist->StabilizeScores(0, 0, NULL);
	mm->ReleaseInterface(lf);
	mm->ReleaseInterface(cfg);
	mm->ReleaseInterface(persist);
}

local void handle_sigint(void)
{
	Imainloop *ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
	if (ml)
		ml->Quit(EXIT_NONE);
	mm->ReleaseInterface(ml);
}

local void handle_sigterm(void)
{
	handle_sigint();
}

local void handle_sigusr1(void)
{
	Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
	persist->StabilizeScores(5, 0, NULL);
	mm->ReleaseInterface(persist);
}

local void handle_sigusr2(void)
{
	char buf[256];
	Ichat *chat;
	FILE *f = fopen("MESSAGE", "r");

	if (f)
	{
		if (fgets(buf, sizeof(buf), f) && (chat = mm->GetInterface(I_CHAT, ALLARENAS)))
		{
			RemoveCRLF(buf);
			chat->SendArenaMessage(ALLARENAS, "%s", buf);
			mm->ReleaseInterface(chat);
		}
		fclose(f);
		unlink("MESSAGE");
	}
}

local void handle_sigsegv(int sig)
{
#if CFG_HANDLE_SEGV == 1
	void *bt[100];
	int n = backtrace(bt, 100);
	fcloseall();

	if (sig == SIGSEGV)
		write(2, "Seg fault, backtrace:\n", 22);
	else if (sig == SIGFPE)
		write(2, "FP exception, backtrace:\n", 25);
	else
		write(2, "Deadlock, backtrace:\n", 21);

	backtrace_symbols_fd(bt, n, 2);
#elif CFG_HANDLE_SEGV == 2
	char cmd[128];
	char *type;
	fcloseall();
	memset(cmd, 0, sizeof(cmd));

	if (sig == SIGSEGV)
		type = "segv";
	else if (sig == SIGFPE)
		type = "fpe";
	else
		type = "abrt";

	snprintf(cmd, sizeof(cmd), "/bin/sh bin/backtrace bin/asss %d %s", getpid(), type);
	system(cmd);
	
	if (sig == SIGSEGV)
		write(2, "Segmentation fault (backtrace dumped)\n", 38);
	else if (sig == SIGFPE)
		write(2, "FP exception (backtrace dumped)\n", 32);
	else
		write(2, "Deadlock (backtrace dumped)\n", 28);
#endif
	_exit(1);
}


local void check_signals(void)
{
	switch (gotsig)
	{
		case SIGHUP:  gotsig = 0; handle_sighup();  break;
		case SIGINT:  gotsig = 0; handle_sigint();  break;
		case SIGTERM: gotsig = 0; handle_sigterm(); break;
		case SIGUSR1: gotsig = 0; handle_sigusr1(); break;
		case SIGUSR2: gotsig = 0; handle_sigusr2(); break;
	}
}


local void sigfunc(int sig)
{
	gotsig = sig;
}

local void init_signals(void)
{
	struct sigaction sa;

	sa.sa_handler = sigfunc;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;

	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

#ifdef CFG_HANDLE_SEGV
	sa.sa_handler = handle_sigsegv;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
#endif
}

local void deinit_signals(void)
{
	struct sigaction sa;

	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGFPE, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
}

local void write_pid(const char *fn)
{
	FILE *f = fopen(fn, "w");
	fprintf(f, "%d\n", getpid());
	fclose(f);
}

local int check_pid_file(const char *fn)
{
	FILE *f = fopen(fn, "r");
	if (f)
	{
		char buf[128];
		if (fgets(buf, sizeof(buf), f) &&
		    atoi(buf) > 1 &&
		    kill(atoi(buf), 0) == 0)
				return TRUE;
		fclose(f);
	}
	return FALSE;
}

EXPORT const char info_unixsignal[] = CORE_MOD_INFO("unixsignal");

EXPORT int MM_unixsignal(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		if (CFG_PID_FILE && check_pid_file(CFG_PID_FILE))
		{
			fprintf(stderr, "E <unixsignal> found previous asss still running\n");
			return MM_FAIL;
		}
		mm->RegCallback(CB_MAINLOOP, check_signals, ALLARENAS);
		init_signals();
		return MM_OK;
	}
	else if (action == MM_POSTLOAD)
	{
		if (CFG_PID_FILE)
			write_pid(CFG_PID_FILE);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		deinit_signals();
		mm->UnregCallback(CB_MAINLOOP, check_signals, ALLARENAS);
		if (CFG_PID_FILE)
			unlink(CFG_PID_FILE);
		return MM_OK;
	}
	else
		return MM_FAIL;
}

