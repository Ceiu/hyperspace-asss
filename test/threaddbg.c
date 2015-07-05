
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>



void * thd(void *v)
{
	printf("in thread thd\n");
	sleep(1);
	printf("crashing:\n");
	*(int*)0 = 0;
}

int main(void)
{
	pthread_t t;

	printf("in main\n");
	printf("starting thd\n");

	pthread_create(&t, NULL, thd, NULL);
	printf("thd started\n");
	sleep(100);
}


