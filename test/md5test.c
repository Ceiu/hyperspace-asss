
#define ___dummy \
: /*
set -e
gcc -I../src/include ../src/main/md5.c md5test.c -o md5test -lz
./md5test
exit
*/
#include <time.h>

#include <stdio.h>
#include <zlib.h>

#include "md5.h"

#define REPS 1000000

struct
{
	unsigned ip;
	unsigned short port;
	unsigned time;
	unsigned sec1;
	unsigned short sec2;
} data;

void do_md5(void *data, int len)
{
	struct MD5Context ctx;
	char digest[16];

	MD5Init(&ctx);
	MD5Update(&ctx, data, sizeof(data));
	MD5Final(digest, &ctx);
}

void do_crc(void *data, int len)
{
	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, data, len);
}

void dotime(void(*func)(void*,int), char *name)
{
	clock_t t1, t2;
	int i = REPS;

	srand(time(NULL));

	data.ip = 372650688;
	data.port = 32773;
	data.time = time(NULL) / 5;
	data.sec1 = rand();
	data.sec2 = rand();

	t1 = clock();

	while (i--)
		func(&data, sizeof(data));

	t2 = clock();

	printf("%s: %f\n", name, (double)(t2-t1)/(double)REPS);
}


int main()
{
	dotime(do_md5, "md5");
	dotime(do_crc, "crc");
	dotime(do_crc, "crc");
	dotime(do_md5, "md5");
	dotime(do_md5, "md5");
	dotime(do_crc, "crc");
}

