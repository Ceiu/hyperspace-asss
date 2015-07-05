
/* dist: public */

#ifndef WIN32
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <limits.h>
#else
#include <direct.h>
#endif

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "asss.h"
#include "pathutil.h"
#include "filetrans.h"
#include "log_file.h"


/* global data */

local Iplayerdata *pd;
local Iconfig *cfg;
local Ichat *chat;
local Ilogman *lm;
local Icmdman *cmd;
local Icapman *capman;
local Ilog_file *logfile;
local Ifiletrans *filetrans;
local Imodman *mm;


local helptext_t admlogfile_help =
"Targets: none\n"
"Args: {flush} or {reopen}\n"
"Administers the log file that the server keeps. There are two possible\n"
"subcommands: {flush} flushes the log file to disk (in preparation for\n"
"copying it, for example), and {reopen} tells the server to close and\n"
"re-open the log file (to rotate the log while the server is running).\n";

local void Cadmlogfile(const char *tc, const char *params, Player *p, const Target *target)
{
	if (!strcasecmp(params, "flush"))
		logfile->FlushLog();
	else if (!strcasecmp(params, "reopen"))
		logfile->ReopenLog();
}


local helptext_t delfile_help =
"Targets: none\n"
"Args: <server pathname>\n"
"Delete a file from the server. Paths are relative to the current working\n"
"directory.\n";

local void Cdelfile(const char *tc, const char *params, Player *p, const Target *target)
{
	char wd[PATH_MAX], path[PATH_MAX];

	filetrans->GetWorkingDirectory(p, wd, sizeof(wd));
	snprintf(path, sizeof(path), "%s/%s", wd, params);

	if (!is_valid_path(path))
		chat->SendMessage(p, "Invalid path or filename.");
	else if (remove(path))
		chat->SendMessage(p, "Error deleting '%s': %s", path, strerror(errno));
	else
		chat->SendMessage(p, "Deleted.");
}


local helptext_t renfile_help =
"Targets: none\n"
"Args: <old filename>:<new filename>\n"
"Rename a file on the server. Paths are relative to the current working\n"
"directory.\n";

local void Crenfile(const char *tc, const char *params, Player *p, const Target *target)
{
	char wd[PATH_MAX], oldpath[PATH_MAX], newpath[PATH_MAX];
	const char *newfile;

	filetrans->GetWorkingDirectory(p, wd, sizeof(wd));
	newfile = delimcpy(newpath, params, sizeof(newpath), ':');

	if (!newfile)
	{
		chat->SendMessage(p, "Bad syntax.");
		return;
	}

	snprintf(oldpath, sizeof(oldpath), "%s/%s", wd, newpath);
	snprintf(newpath, sizeof(newpath), "%s/%s", wd, newfile);

	if (!is_valid_path(oldpath))
		chat->SendMessage(p, "Invalid old path.");
	else if (!is_valid_path(newpath))
		chat->SendMessage(p, "Invalid new path.");
	else if (
#ifdef WIN32
			remove(newpath) ||
#endif
			rename(oldpath, newpath))
		chat->SendMessage(p, "Error renaming '%s' to '%s': %s", oldpath,
				newpath, strerror(errno));
	else
		chat->SendMessage(p, "Renamed.");
}


local helptext_t getfile_help =
"Targets: none\n"
"Args: <filename>\n"
"Transfers the specified file from the server to the client. The filename\n"
"is considered relative to the current working directory.\n";

local void Cgetfile(const char *tc, const char *params, Player *p, const Target *target)
{
	char wd[PATH_MAX], path[PATH_MAX];

	if (!IS_STANDARD(p))
	{
		chat->SendMessage(p, "Your client doesn't support file transfers.");
		return;
	}

	filetrans->GetWorkingDirectory(p, wd, sizeof(wd));
	snprintf(path, sizeof(path), "%s/%s", wd, params);

	if (!is_valid_path(path))
		chat->SendMessage(p, "Invalid path.");
	else
	{
		int res = filetrans->SendFile(p, path, get_basename(path), 0);
		if (res == MM_FAIL)
			chat->SendMessage(p, "Error sending '%s': %s", path, strerror(errno));
	}
}


typedef struct upload_t
{
	Player *p;
	int unzip;
	const char *setting;
	Arena *arena;
	char serverpath[1];
} upload_t;


local void uploaded(const char *fname, void *clos)
{
	upload_t *u = clos;
	int r = 0;

	if (fname && u->unzip)
	{
		chat->SendMessage(u->p, "Zip inflated to: %s", u->serverpath);
		/* unzip it to the destination directory */
#ifndef WIN32 /* should use popen, since more portable */
		r = fork();
		if (r == 0)
		{
			/* in child, perform unzip */
			close(0); close(1); close(2);
			r = fork();
			if (r == 0)
			{
				/* in child of child */
				/* -qq to be quieter
				 * -a to auto-convert line endings between dos and unix
				 * -o to overwrite all files
				 * -j to ignore paths specified in the zip file (for security)
				 * -d to specify the destination directory */
				execlp("unzip", "unzip", "-qq", "-a", "-o", "-j",
						"-d", u->serverpath, fname, NULL);
			}
			else if (r > 0)
			{
				/* in parent of child. wait for unzip to finish, then
				 * unlink the file */
				waitpid(r, NULL, 0);
				unlink(fname);
			}
			_exit(0);
		}
		else if (r < 0)
#endif
			lm->Log(L_WARN, "<admincmd> can't fork to unzip uploaded .zip file");
	}
	else if (fname)
	{
		/* move it to the right place */
#ifdef WIN32
		remove(u->serverpath);
#endif
		r = rename(fname, u->serverpath);

		if (r < 0)
		{
			lm->LogP(L_WARN, "admincmd", u->p, "couldn't rename file '%s' to '%s'",
					fname, u->serverpath);
			chat->SendMessage(u->p, "Couldn't upload file to '%s'", u->serverpath);
			remove(fname);
		}
		else
		{
			chat->SendMessage(u->p, "File received: %s", u->serverpath);
			if (u->setting && cfg)
			{
				if (u->p->arena && u->p->arena == u->arena)
				{
					char info[128];
					time_t t;
					struct tm _tm;

					time(&t);
					alocaltime_r(&t, &_tm);

					snprintf(info, 100, "set by %s with ?putmap on ", u->p->name);
					strftime(info + strlen(info), sizeof(info) - strlen(info),
							"%a %b %d %H:%M:%S %Y", &_tm);
					cfg->SetStr(u->p->arena->cfg, u->setting, NULL,
							u->serverpath, info, TRUE);
					chat->SendMessage(u->p, "Set %s=%s", u->setting, u->serverpath);
				}
				else
				{
					chat->SendMessage(u->p, "Changed arenas! Aborting map upload!");
				}
			}
		}
	}

	afree(u);
}


local helptext_t putfile_help =
"Targets: none\n"
"Args: <client filename>[:<server filename>]\n"
"Transfers the specified file from the client to the server.\n"
"The server filename, if specified, will be considered relative to the\n"
"current working directory. If omitted, the uploaded file will be placed\n"
"in the current working directory and named the same as on the client.\n";

local void Cputfile(const char *tc, const char *params, Player *p, const Target *target)
{
	char clientfile[256], wd[PATH_MAX], serverpath[PATH_MAX];
	const char *t;

	if (!IS_STANDARD(p))
	{
		chat->SendMessage(p, "Your client doesn't support file transfers.");
		return;
	}

	filetrans->GetWorkingDirectory(p, wd, sizeof(wd));

	t = delimcpy(clientfile, params, sizeof(clientfile), ':');
	snprintf(serverpath, sizeof(serverpath), "%s/%s", wd, t ? t : get_basename(clientfile));

	if (is_valid_path(serverpath))
	{
		int ret;
		upload_t *u = amalloc(sizeof(*u) + strlen(serverpath));

		u->p = p;
		u->unzip = 0;
		u->setting = NULL;
		strcpy(u->serverpath, serverpath);

		ret = filetrans->RequestFile(p, clientfile, uploaded, u);
		if (ret == MM_FAIL)
		{
			chat->SendMessage(p, "Error sending file.");
			afree(u);
		}
	}
	else
		chat->SendMessage(p, "Invalid server path.");
}


local helptext_t putzip_help =
"Targets: none\n"
"Args: <client filename>[:<server directory>]\n"
"Uploads the specified zip file to the server and unzips it in the\n"
"specified directory (considered relative to the current working directory),\n"
"or if none is provided, the working directory itself. This can be used to\n"
"efficiently send a large number of files to the server at once, while\n"
"preserving directory structure.\n";

local void Cputzip(const char *tc, const char *params, Player *p, const Target *target)
{
	char clientfile[256], wd[PATH_MAX], serverpath[PATH_MAX];
	const char *t;

	filetrans->GetWorkingDirectory(p, wd, sizeof(wd));

	t = delimcpy(clientfile, params, sizeof(clientfile), ':');
	if (t)
		snprintf(serverpath, sizeof(serverpath), "%s/%s", wd, t);
	else
		astrncpy(serverpath, wd, sizeof(serverpath));

	if (is_valid_path(serverpath))
	{
		upload_t *u = amalloc(sizeof(*u) + strlen(serverpath));

		u->p = p;
		u->unzip = 1;
		u->setting = NULL;
		strcpy(u->serverpath, serverpath);

		filetrans->RequestFile(p, clientfile, uploaded, u);
	}
}


local helptext_t putmap_help =
"Targets: none\n"
"Args: <map file>\n"
"Transfers the specified map file from the client to the server.\n"
"The map will be placed in maps/uploads/<arenabasename>.lvl,\n"
"and the setting General:Map will be changed to the name of the\n"
"uploaded file.\n";

local void Cputmap(const char *tc, const char *params, Player *p, const Target *target)
{
	char serverpath[256];
	upload_t *u;

	/* make sure these exist */
	mkdir("maps", 0666);
	mkdir("maps/uploads", 0666);

	snprintf(serverpath, sizeof(serverpath),
			"maps/uploads/%s.lvl", p->arena->basename);

	u = amalloc(sizeof(*u) + strlen(serverpath));

	u->p = p;
	u->unzip = 0;
	u->setting = "General:Map";
	u->arena = p->arena;
	strcpy(u->serverpath, serverpath);

	filetrans->RequestFile(p, params, uploaded, u);
}


local helptext_t cd_help =
"Targets: none\n"
"Args: [<server directory>]\n"
"Changes working directory for file transfer. Note that the specified path\n"
"must be an absolute path; it is not considered relative to the previous\n"
"working directory. If no arguments are specified, return to the server's\n"
"root directory.\n";

local void Ccd(const char *tc, const char *params, Player *p, const Target *target)
{
	struct stat st;
	if (!*params)
		params = ".";
	if (!is_valid_path(params))
		chat->SendMessage(p, "Invalid path.");
	else if (stat(params, &st) < 0)
		chat->SendMessage(p, "The specified path doesn't exist.");
	else if (!S_ISDIR(st.st_mode))
		chat->SendMessage(p, "The specified path isn't a directory.");
	else
	{
		filetrans->SetWorkingDirectory(p, params);
		chat->SendMessage(p, "Changed working directory.");
	}
}


local helptext_t pwd_help =
"Targets: none\n"
"Args: none\n"
"Prints the current working directory. A working directory of \".\"\n"
"indicates the server's root directory.\n";

local void Cpwd(const char *tc, const char *params, Player *p, const Target *target)
{
	char wd[PATH_MAX];
	filetrans->GetWorkingDirectory(p, wd, sizeof(wd));
	chat->SendMessage(p, "Current working directory: %s", wd);
}


local helptext_t makearena_help =
"Targets: none\n"
"Args: <arena name>\n"
"Creates a directory for the new directory under 'arenas/'\n";

local void Cmakearena(const char *tc, const char *params, Player *p, const Target *target)
{
	char buf[128];
	const char *c;
	FILE *f;

	/* check for a legal arena name. this should match the logic in
	 * arenaman.c */
	for (c = params; *c; c++)
		if (*c == '#' && c == params)
			/* initial # ok */;
		else if (!isalnum(*c) || isupper(*c) || *params == '\0')
		{
			chat->SendMessage(p, "Illegal arena name.");
			return;
		}

	snprintf(buf, sizeof(buf), "arenas/%s", params);
	if (mkdir(buf, 0755) < 0)
	{
		int err = errno;
		chat->SendMessage(p, "Error creating directory '%s': %s", buf, strerror(err));
		lm->Log(L_WARN, "<admincmd> error creating directory '%s': %s", buf, strerror(err));
		return;
	}

	snprintf(buf, sizeof(buf), "arenas/%s/arena.conf", params);
	f = fopen(buf, "w");
	if (!f)
	{
		int err = errno;
		chat->SendMessage(p, "Error creating file '%s': %s", buf, strerror(err));
		lm->Log(L_WARN, "<admincmd> error creating file '%s': %s", buf, strerror(err));
		return;
	}

	fputs("\n#include arenas/(default)/arena.conf\n", f);
	fclose(f);

	chat->SendMessage(p, "Successfully created %s.", params);
}


local helptext_t botfeature_help =
"Targets: none\n"
"Args: [+/-{seeallposn}] [+/-{seeownposn}]\n"
"Enables or disables bot-specific features. {seeallposn} controls whether\n"
"the bot gets to see all position packets. {seeownposn} controls whether\n"
"you get your own mirror position packets.\n";

local void Cbotfeature(const char *tc, const char *params, Player *p, const Target *target)
{
	char buf[64];
	const char *tmp = NULL;

	while (strsplit(params, " ,", buf, sizeof(buf), &tmp))
	{
		int on;

		if (buf[0] == '+')
			on = 1;
		else if (buf[0] == '-')
			on = 0;
		else
		{
			chat->SendMessage(p, "Bad syntax!");
			continue;
		}

		if (!strcmp(buf+1, "seeallposn"))
			p->flags.see_all_posn = on;
		else if (!strcmp(buf+1, "seeownposn"))
			p->flags.see_own_posn = on;
		else
			chat->SendMessage(p, "Unknown bot feature!");
	}
}

EXPORT const char info_admincmd[] = CORE_MOD_INFO("admincmd");

EXPORT int MM_admincmd(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		logfile = mm->GetInterface(I_LOG_FILE, ALLARENAS);
		filetrans = mm->GetInterface(I_FILETRANS, ALLARENAS);
		if (!pd || !chat || !lm || !cmd || !capman || !logfile || !filetrans)
			return MM_FAIL;

		cmd->AddCommand("admlogfile", Cadmlogfile, ALLARENAS, admlogfile_help);
		cmd->AddCommand("getfile", Cgetfile, ALLARENAS, getfile_help);
		cmd->AddCommand("putfile", Cputfile, ALLARENAS, putfile_help);
		cmd->AddCommand("putzip", Cputzip, ALLARENAS, putzip_help);
		cmd->AddCommand("putmap", Cputmap, ALLARENAS, putmap_help);
		cmd->AddCommand("makearena", Cmakearena, ALLARENAS, makearena_help);
		cmd->AddCommand("botfeature", Cbotfeature, ALLARENAS, botfeature_help);
		cmd->AddCommand("cd", Ccd, ALLARENAS, cd_help);
		cmd->AddCommand("pwd", Cpwd, ALLARENAS, pwd_help);
		cmd->AddCommand("delfile", Cdelfile, ALLARENAS, delfile_help);
		cmd->AddCommand("renfile", Crenfile, ALLARENAS, renfile_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		cmd->RemoveCommand("admlogfile", Cadmlogfile, ALLARENAS);
		cmd->RemoveCommand("getfile", Cgetfile, ALLARENAS);
		cmd->RemoveCommand("putfile", Cputfile, ALLARENAS);
		cmd->RemoveCommand("putzip", Cputzip, ALLARENAS);
		cmd->RemoveCommand("putmap", Cputmap, ALLARENAS);
		cmd->RemoveCommand("makearena", Cmakearena, ALLARENAS);
		cmd->RemoveCommand("botfeature", Cbotfeature, ALLARENAS);
		cmd->RemoveCommand("cd", Ccd, ALLARENAS);
		cmd->RemoveCommand("pwd", Cpwd, ALLARENAS);
		cmd->RemoveCommand("delfile", Cdelfile, ALLARENAS);
		cmd->RemoveCommand("renfile", Crenfile, ALLARENAS);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(logfile);
		mm->ReleaseInterface(filetrans);
		return MM_OK;
	}
	return MM_FAIL;
}


