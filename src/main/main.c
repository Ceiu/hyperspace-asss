
/* dist: public */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef WIN32
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <paths.h>
#include <unistd.h>
#include <fcntl.h>
#endif

#include "asss.h"
#include "cmod.h"
#include "app.h"
#include "persist.h"


local Imodman *mm;
local Ilogman *lm;
local Imainloop *ml;

local int dodaemonize, dochroot;
local struct
{
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	int done;
} wait = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0 };


local void ProcessArgs(int argc, char *argv[])
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "--daemonize") || !strcmp(argv[i], "-d"))
			dodaemonize = 1;
		else if (!strcmp(argv[i], "--chroot") || !strcmp(argv[i], "-c"))
			dochroot = 1;
		else
			/* this might be a directory */
			if (chdir(argv[i]) < 0)
			{
				fprintf(stderr, "Can't chdir to '%s'\n", argv[i]);
				exit(1);
			}
	}
}


local void CheckBin(const char *argv0)
{
#ifndef WIN32
	struct stat st;
	char binpath[PATH_MAX], *t;

	if (stat("bin", &st) == -1)
	{
		printf("No 'bin' directory found, attempting to locate one.\n");
		/* try argv[0] */
		astrncpy(binpath, argv0, sizeof(binpath));
		/* make sure asss exists */
		if (stat(binpath, &st) == -1)
			goto no_bin;
		/* get dir name */
		t = strrchr(binpath, '/');
		if (!t)
			goto no_bin;
		*t = 0;
		/* make sure this exists */
		if (stat(binpath, &st) == -1)
			goto no_bin;
		/* link it */
		if (symlink(binpath, "bin") == -1)
			printf("symlink failed. Module loading won't work\n");
		else
			printf("Made link from 'bin' to '%s'.\n", binpath);
		return;
no_bin:
		printf("Can't find suitable bin directory.\n");
	}
	else if (!S_ISDIR(st.st_mode))
		printf("'bin' isn't a directory.\n");
	else /* everything is fine */
		return;
	printf("Module loading won't work.\n");
#endif
}


local int finder(char *dest, int destlen, const char *ar, const char *name)
{
	astrncpy(dest, name, destlen);
	return 0;
}

local void error(const char *err)
{
	Error(EXIT_MODLOAD, "Error in modules.conf: %s", err);
}

local void LoadModuleFile(char *fname)
{
	char line[256];
	int ret;
	APPContext *ctx;

	ctx = APPInitContext(finder, error, NULL);
	APPAddFile(ctx, fname);

	while (APPGetLine(ctx, line, 256))
	{
		ret = mm->LoadModule(line);
		if (ret == MM_FAIL)
			Error(EXIT_MODLOAD, "Error in loading module '%s'", line);
	}

	APPFreeContext(ctx);
}


#ifndef WIN32
local int daemonize(int noclose)
{
	int fd;

	printf("forking into the background\n");

	switch (fork())
	{
		case -1:
			return -1;
		case 0:
			break;
		default:
			_exit(0);
	}

	if (setsid() == -1) return -1;
	if (noclose) return 0;

	fd = open(_PATH_DEVNULL, O_RDWR, 0);
	if (fd != -1)
	{
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > 2) close(fd);
	}
	return 0;
}
#else
local int daemonize(int noclose)
{
	printf("daemonize isn't supported on windows\n");
	return 0;
}
#endif


#ifndef WIN32

#include <pwd.h>

local int do_chroot(void)
{
	uid_t uid;

	/* if we're root because of a setuid binary */
	if (getuid() != geteuid())
		uid = getuid();
	else
	{
		struct passwd *pwd;
		const char *user;

		/* first get uid to set to */
		user = getenv("USER");
		if (!user)
		{
			fprintf(stderr, "$USER isn't set, can't chroot\n");
			return -1;
		}

		pwd = getpwnam(user);
		if (!pwd)
		{
			fprintf(stderr, "Can't get passwd entry for %s\n", user);
			return -1;
		}

		uid = pwd->pw_uid;
	}

	if (chroot(".") < 0)
	{
		perror("can't chroot to '.'");
		return -1;
	}

	if (chdir("/") < 0)
	{
		perror("can't chdir to '/'");
		return -1;
	}

	if (setuid(uid) < 0)
	{
		perror("can't setuid");
		return -1;
	}

	printf("Changed root directory and set uid to %d\n", (int)uid);
	return 0;
}
#else
local int do_chroot(void)
{
	printf("chroot isn't supported on windows\n");
	return 0;
}
#endif


#ifdef WIN32
local BOOL WINAPI winshutdown(DWORD shutdowntype)
{
	ml->Quit(EXIT_NONE);
	return TRUE;
}
#endif


local void syncdone(Player *dummy)
{
	wait.done = 1;
	pthread_cond_signal(&wait.cond);
}


int main(int argc, char *argv[])
{
	int code;

	/* seed random number generators */
	srand(current_ticks());

	/* set the timezone for the crt */
	tzset();

	ProcessArgs(argc, argv);

	CheckBin(argv[0]);

	printf("asss %s built at %s\n", ASSSVERSION, BUILDDATE);

	if (dochroot)
		if (do_chroot() != 0)
			Error(EXIT_CHROOT, "error changing root directory or dropping privileges");

	if (dodaemonize)
		daemonize(0);

	mm = InitModuleManager();
	RegCModLoader(mm);

	printf("Loading modules...\n");

	LoadModuleFile("conf/modules.conf");

#ifdef CFG_NO_RUNTIME_LOAD
	mm->frommain.NoMoreModules();
#endif

	mm->frommain.DoStage(MM_POSTLOAD);

	lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
	ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);

	if (!ml)
		Error(EXIT_MODLOAD, "mainloop module missing");

	if (lm) lm->Log(L_DRIVEL, "<main> entering main loop");

#ifdef WIN32
	/* safely handle console closing on windows */
	SetConsoleCtrlHandler(winshutdown, TRUE);
#endif

	code = ml->RunLoop();

	if (lm) lm->Log(L_DRIVEL|L_SYNC, "<main> exiting main loop");

	{
		/* send a nice message */
		Ichat *chat = mm->GetInterface(I_CHAT, ALLARENAS);
		if (chat)
		{
			chat->SendArenaMessage(ALLARENAS, "The server is %s now!",
					code == EXIT_RECYCLE ? "recycling" : "shutting down");
			mm->ReleaseInterface(chat);
		}
	}

	{
		/* try to save scores */
		Ipersist *persist = mm->GetInterface(I_PERSIST, ALLARENAS);
		if (persist)
		{
			if (lm) lm->Log(L_DRIVEL|L_SYNC, "<main> saving scores");
			pthread_mutex_lock(&wait.mtx);
			persist->StabilizeScores(0, 1, syncdone);
			while (!wait.done)
				pthread_cond_wait(&wait.cond, &wait.mtx);
			pthread_mutex_unlock(&wait.mtx);
			mm->ReleaseInterface(persist);
		}
	}

	mm->ReleaseInterface(lm);
	mm->ReleaseInterface(ml);

	if (lm) lm->Log(L_DRIVEL|L_SYNC, "<main> unloading modules");
	mm->frommain.DoStage(MM_PREUNLOAD);
	mm->frommain.UnloadAllModules();

	UnregCModLoader();
	DeInitModuleManager(mm);

	return code;
}



