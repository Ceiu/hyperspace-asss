
#include <stdio.h>
#include <sys/utsname.h>

int main()
{
	struct utsname un;
	uname(&un);

	printf("sys: %s\n", un.sysname);
	printf("node: %s\n", un.nodename);
	printf("rel: %s\n", un.release);
	printf("ver: %s\n", un.version);
	printf("mach: %s\n", un.machine);
}

