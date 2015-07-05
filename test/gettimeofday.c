
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>


int main()
{
	struct timeval tv;
	int r;
	unsigned int v;

	while (1)
	{
		r = gettimeofday(&tv, NULL);
		v = tv.tv_sec * 100 + tv.tv_usec / 10000;
		printf("gettimeofday returned %i: (%u) %i %i       \r",r ,v, tv.tv_sec, tv.tv_usec);
		fflush(stdout);
		usleep(100);
	}
}

