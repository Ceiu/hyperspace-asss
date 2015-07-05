
#define ___dummy \
: /*
set -e
gcc -g -D_REENTRANT -D_GNU_SOURCE rwlock.c -o rwlock -lpthread
exit
*/

#include <stdio.h>
#include <pthread.h>

int main(int argc, char *argv[])
{
	pthread_rwlock_t l;

	pthread_rwlock_init(&l, NULL);

	puts("acquiring read lock");
	pthread_rwlock_rdlock(&l);
	puts("got read lock");

	puts("acquiring read lock");
	pthread_rwlock_rdlock(&l);
	puts("got read lock");

	puts("acquiring write lock");
	pthread_rwlock_wrlock(&l);
	puts("got write lock");
}


