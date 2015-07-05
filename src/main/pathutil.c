
/* dist: public */

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef WIN32
#include <unistd.h>
#include <limits.h>
#endif

#include "defs.h"
#include "pathutil.h"


int macro_expand_string(
		char *dest,
		int destlen,
		char *source,
		struct replace_table *repls,
		int replslen,
		char macrochar)
{
	char *curpos, *end;

	curpos = dest;
	end = dest + destlen - 1;

	while (*source && curpos < end)
	{
		char it = *source;
		if (it == macrochar)
		{
			int i;
			source++;
			it = *source++;
			if (it == macrochar)
			{
				/* doubled macro char, insert literal */
				*curpos++ = macrochar;
			}
			else
			{
				for (i = 0; i < replslen; i++)
				{
					if (repls[i].repl == it)
					{
						const char *with = repls[i].with;
						i = replslen + 7;
						if (strlen(with) < ((end-curpos)-1))
						{
							/* custom strcat */
							while ((*curpos++ = *with++)) ;
							curpos--; /* back up over the null */
						}
						else
						{
							/* overflow of destination buffer */
							return -1;
						}
					}
				}
				if (i == replslen)
				{
					/* no replacement found */
					return -1;
				}
			}
		}
		else
		{
			*curpos = it;
			source++, curpos++;
		}
	}

	if (curpos >= end)
	{
		/* overflowed destionation */
		return -1;
	}

	/* success. write null terminator and return */
	*curpos = 0;
	return curpos - dest;
}


#define MAXFILENAME PATH_MAX

int find_file_on_path(
		char *dest,
		int destlen,
		const char *searchpath,
		struct replace_table *repls,
		int replslen)
{
	int done = 0;
	char *path, file[MAXFILENAME];

	path = alloca(strlen(searchpath)+1);
	strcpy(path, searchpath);

	while (!done)
	{
		int res;
		char *t;

		/* find end of first filename */
		t = strchr(path, ':');
		if (t)
			*t = 0;
		else
			done = 1;

		/* do macro replace */
		res = macro_expand_string(
				file,
				sizeof(file),
				path,
				repls,
				replslen,
				'%');

		/* check the file access */
		if (res != -1)
		{
			/* printf("DEBUG: trying file %s\n", file); */
			if (access(file, R_OK) == 0)
			{
				struct stat st;
				/* we hit one, check if it's a directory */
				stat(file, &st);
				if (!S_ISDIR(st.st_mode))
				{
					if (strlen(file) >= destlen)
					{
						/* buffer isn't big enough */
						return -1;
					}
					else
					{
						/* it's big enough. hurrah! */
						strcpy(dest, file);
						return 0;
					}
				}
			}
		}

		/* line up for next try */
		path = t+1;
	}
	return -1;
}


int is_valid_path(const char *path)
{
	const char *s = path;
	char ch, last = 0;

	if (!path)
		return FALSE;

	ch = *s;

	/* bad first characters: /
	 */
	if (ch == '/')
		return FALSE;

	/* bad chars: nonprintables, :, \
	 * bad sequences: .. //
	 */
	while (ch)
	{
		if ((ch < ' ' || ch > '~' || ch == ':') ||
		    ((ch == '.' || ch == '/' || ch == '\\') && last == ch))
			return FALSE;

		last = ch;
		ch = *++s;
	}

	/* bad last chars: /
	 */
	if (last == '/')
		return FALSE;

	return TRUE;
}


const char *get_basename(const char *path)
{
	const char *c = path + strlen(path);
	for (;;)
	{
		if (c == path) return path;
		c--;
		if (*c == '/' || *c == '\\') return c + 1;
	}
}


