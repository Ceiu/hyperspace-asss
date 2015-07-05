
/* dist: public */

#ifdef WIN32

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "win32compat.h"
#include "util.h"


/* taken from
 * http://www2.ics.hawaii.edu/~esb/2001fall.ics451/strcasestr.html */
const char * strcasestr(const char* haystack, const char* needle)
{
	int i;
	int nlength = strlen (needle);
	int hlength = strlen (haystack);

	if (nlength > hlength) return NULL;
	if (hlength <= 0) return NULL;
	if (nlength <= 0) return haystack;
	/* hlength and nlength > 0, nlength <= hlength */
	for (i = 0; i <= (hlength - nlength); i++)
		if (strncasecmp (haystack + i, needle, nlength) == 0)
			return haystack + i;
	/* substring not found */
	return NULL;
}


int mkstemp(char *templ)
{
	if (mktemp(templ) == NULL)
		return -1;

	return _open(templ, _O_CREAT | _O_RDWR | _O_BINARY | _O_EXCL, _S_IREAD | _S_IWRITE);
}


struct DIR
{
	struct _finddata_t fi;
	long fh;
	long lastres;
	struct dirent de;
};

DIR *opendir(const char *reqpath)
{
	char path[PATH_MAX];
	DIR *dir = amalloc(sizeof(*dir));
	snprintf(path, sizeof(path), "%s/*", reqpath);
	dir->fh = _findfirst(path, &dir->fi);
	dir->lastres = dir->fh;
	return dir;
}

struct dirent *readdir(DIR *dir)
{
	if (dir->lastres != -1)
	{
		astrncpy(dir->de.d_name, dir->fi.name, sizeof(dir->de.d_name));
		dir->lastres = _findnext(dir->fh, &dir->fi);
		return &dir->de;
	}
	else
		return NULL;
}

void closedir(DIR *dir)
{
	if (dir->fh != -1)
		_findclose(dir->fh);
	afree(dir);
}


int inet_aton(const char *cp, struct in_addr *pin)
{
	unsigned long rc;
	rc = inet_addr(cp);
	if (rc == (unsigned long)(-1) && strcmp(cp, "255.255.255.255"))
		return 0;
	pin->s_addr = rc;
	return 1;
}

const char* inet_ntop(int af, const void* src, char* dst, int cnt)
{
	struct sockaddr_in srcaddr;

	memset(&srcaddr, 0, sizeof(struct sockaddr_in));
	memcpy(&(srcaddr.sin_addr), src, sizeof(srcaddr.sin_addr));

	srcaddr.sin_family = af;
	if (WSAAddressToString((struct sockaddr*) &srcaddr, sizeof(struct sockaddr_in), 0, dst, (LPDWORD) &cnt) != 0) {
		DWORD rv = WSAGetLastError();
		printf("WSAAddressToString() : %d\n",rv);
		return NULL;
	}
	return dst;
}

#endif

