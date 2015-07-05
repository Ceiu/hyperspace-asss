
/* dist: public */

#ifdef WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock.h>
#include <limits.h>
#include <malloc.h>
#include <io.h>
#include <windef.h>
#include <wincon.h>
#include <direct.h>
#include <unistd.h>

#define EXPORT __declspec(dllexport)

#ifndef NDEBUG
#define inline
#else
#define inline __inline
#endif

#define strcasecmp(a,b) stricmp((a),(b))
#define strncasecmp(a,b,c) strnicmp((a),(b),(c))
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 256
#endif
//#define usleep(x) Sleep((x)/1000)
#define sleep(x) Sleep((x)*1000)
#define mkdir(a,b) _mkdir(a)
#ifndef alloca
#define alloca _alloca
#define access _access
#endif
#ifndef S_ISDIR
#define S_ISDIR(a) ((a) & _S_IFDIR)
#endif
#ifndef mktemp
#define mktemp(a) _mktemp(a)
#define chdir(a) _chdir(a)
#endif
#ifndef R_OK
#define R_OK 4
#endif

typedef int socklen_t;

#define BROKEN_VSNPRINTF

/* a few things that windows is missing */
const char * strcasestr(const char* haystack, const char* needle);
int mkstemp(char *template);


/* directory listing */
typedef struct DIR DIR;

struct dirent
{
	char d_name[NAME_MAX];
};

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
void closedir(DIR *dir);

int inet_aton(const char *cp, struct in_addr *pin);
const char* inet_ntop(int af, const void* src, char* dst, int cnt);

#endif

