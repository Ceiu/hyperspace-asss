
/* 2>/dev/null
gcc -O2 -Wall -I../src -o signs signs.c
./signs
rm signs
exit # */

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#define TICK_DIFF(a,b) ((int)(((a)<<1)-((b)<<1)) >>1)

typedef unsigned int ticks_t;

int main()
{
	ticks_t t1 = 3;
	ticks_t t2 = 6000U;
	ticks_t t3 = (1<<31) - 13U;

	printf("t1-t2=%d\n", TICK_DIFF(t1,t2));
	printf("t2-t1=%d\n", TICK_DIFF(t2,t1));

	printf("t1-t3=%d\n", TICK_DIFF(t1,t3));
	printf("t3-t1=%d\n", TICK_DIFF(t3,t1));

	return 1;
}

