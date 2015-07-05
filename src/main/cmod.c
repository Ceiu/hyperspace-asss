
/* dist: public */

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#ifndef WIN32
#include <dlfcn.h>
#include <unistd.h>
#else
#include <direct.h>
#define dlopen(a,b) ((a) ? LoadLibrary(a) : GetModuleHandle(NULL))
#define dlsym(a,b) ((void*)GetProcAddress(a,b))
#define dlclose(a) FreeLibrary(a)
#endif

#include "asss.h"

/* all module entry points must be of this type */
typedef int (*ModMain)(int action, Imodman *mm, Arena *arena);

typedef struct c_mod_data_t
{
	void *handle;
	ModMain main;
	int ismyself;
} c_mod_data_t;

local Imodman *mm;

#define LOG0(lev, fmt) \
	if (lm) lm->Log(L_SYNC | lev, fmt); \
	else fprintf(stderr, "%c " fmt "\n", lev);
#define LOG1(lev, fmt, a1) \
	if (lm) lm->Log(L_SYNC | lev, fmt, a1); \
	else fprintf(stderr, "%c " fmt "\n", lev, a1);
#define LOG2(lev, fmt, a1, a2) \
	if (lm) lm->Log(L_SYNC | lev, fmt, a1, a2); \
	else fprintf(stderr, "%c " fmt "\n", lev, a1, a2);

local int load_c_module(const char *spec_, mod_args_t *args)
{
	char buf[PATH_MAX], spec[PATH_MAX], *modname, *filename, *path;
	int ret;
	c_mod_data_t *cmd;
	Ilogman *lm = NULL;

	/* make copy of specifier */
	astrncpy(spec, spec_, sizeof(spec));

	modname = strchr(spec, ':');
	if (modname)
	{
		*modname = 0;
		modname++;
		filename = spec;
	}
	else
	{
		modname = spec;
		filename = "internal";
	}

	lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

	LOG2(L_INFO, "<cmod> loading C module '%s' from '%s'", modname, filename);

	/* get a struct for our private data */
	cmd = amalloc(sizeof(c_mod_data_t));
	args->privdata = cmd;

	if (!strcasecmp(filename, "internal"))
	{
		path = NULL;
		cmd->ismyself = 1;
	}
#ifdef CFG_RESTRICT_MODULE_PATH
	else if (strstr(filename, "..") || filename[0] == '/')
	{
		LOG1(L_ERROR, "<cmod> refusing to load filename: %s", filename);
		goto die;
	}
#else
	else if (filename[0] == '/')
	{
		/* filename is an absolute path */
		path = filename;
	}
#endif
	else
	{
		char cwd[PATH_MAX], dir[NAME_MAX];
		const char *tmp = NULL;
		getcwd(cwd, sizeof(cwd));
		path = NULL;
		while (strsplit(CFG_CMOD_SEARCH_PATH, ":", dir, sizeof(dir), &tmp))
		{
			if (snprintf(buf, sizeof(buf), "%s/%s/%s"
#ifndef WIN32
						".so",
#else
						".dll",
#endif
						cwd, dir, filename) > sizeof(buf))
				continue;
			if (access(buf, F_OK) == 0)
			{
				path = buf;
				break;
			}
		}
		if (!path)
		{
			LOG1(L_ERROR, "<cmod> can't find file: %s", filename);
			goto die;
		}
	}

	cmd->handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
	if (!cmd->handle)
	{
#ifndef WIN32
		LOG1(L_ERROR, "<cmod> error in dlopen: %s", dlerror());
#else
		LPVOID lpMsgBuf;

		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR) &lpMsgBuf,
				0,
				NULL);
		LOG1(L_ERROR, "<cmod> error in LoadLibrary: %s", (LPCTSTR)lpMsgBuf);
		LocalFree(lpMsgBuf);
#endif
		goto die;
	}

	snprintf(buf, sizeof(buf), "MM_%s", modname);
	cmd->main = (ModMain)dlsym(cmd->handle, buf);
	if (!cmd->main)
	{
#ifndef WIN32
		LOG1(L_ERROR, "<cmod> error in dlsym: %s", dlerror());
#else
		LPVOID lpMsgBuf;
		FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_FROM_SYSTEM |
				FORMAT_MESSAGE_IGNORE_INSERTS,
				NULL,
				GetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
				(LPTSTR) &lpMsgBuf,
				0,
				NULL);
		LOG1(L_ERROR, "<cmod> error in GetProcAddress: %s", (LPCTSTR)lpMsgBuf);
		LocalFree(lpMsgBuf);
#endif
		if (!cmd->ismyself)
			dlclose(cmd->handle);
		goto die;
	}

	/* load info if it exists */
	snprintf(buf, sizeof(buf), "info_%s", modname);
	args->info = dlsym(cmd->handle, buf);

	astrncpy(args->name, modname, sizeof(args->name));

	ret = cmd->main(MM_LOAD, mm, ALLARENAS);

	if (ret != MM_OK)
	{
		LOG1(L_ERROR, "<cmod> error loading module '%s'", modname);
		if (!cmd->ismyself)
			dlclose(cmd->handle);
		goto die;
	}

	if (lm)
		mm->ReleaseInterface(lm);

	return MM_OK;

die:
	afree(cmd);
	if (lm)
		mm->ReleaseInterface(lm);
	return MM_FAIL;
}


local int unload_c_module(mod_args_t *args)
{
	Ilogman *lm = NULL;
	c_mod_data_t *cmd = args->privdata;

	lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
	LOG1(L_INFO, "<cmod> unloading C module '%s'", args->name);
	if (lm)
		mm->ReleaseInterface(lm);

	if (cmd->main)
		if ((cmd->main)(MM_UNLOAD, mm, ALLARENAS) == MM_FAIL)
			return MM_FAIL;
	if (cmd->handle && !cmd->ismyself)
		dlclose(cmd->handle);
	afree(cmd);
	return MM_OK;
}


local int loader(int action, mod_args_t *args, const char *line, Arena *arena)
{
	c_mod_data_t *cmd = args->privdata;

	switch (action)
	{
		case MM_LOAD:
			return load_c_module(line, args);

		case MM_UNLOAD:
			return unload_c_module(args);

		case MM_ATTACH:
		case MM_DETACH:
		case MM_POSTLOAD:
		case MM_PREUNLOAD:
			return cmd->main(action, mm, arena);

		default:
			return MM_FAIL;
	}
}


void RegCModLoader(Imodman *mm_)
{
	mm = mm_;
	mm->RegModuleLoader("c", loader);
}

void UnregCModLoader(void)
{
	mm->UnregModuleLoader("c", loader);
}

