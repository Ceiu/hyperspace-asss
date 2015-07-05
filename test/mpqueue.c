
/* 2>/dev/null
gcc -I../src/include -I../src -o mpqueue mpqueue.c ../src/main/util.c -lpthread
exit # */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "util.h"



struct QueueData
{
	int d;
	char *s;
};



LinkedList *queue;
pthread_mutex_t queuemtx = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queuecond = PTHREAD_COND_INITIALIZER;


void * reader(void *foo)
{
	Link *l;
	struct QueueData *qd;
	int done = 0;

	while (!done)
	{
		pthread_mutex_lock(&queuemtx);
		
		while ( !(l = LLGetHead(queue)) )
		{
			printf("waiting...\n");
/*			pthread_mutex_unlock(&queuemtx);
			usleep(200000);
			pthread_mutex_lock(&queuemtx); */
			pthread_cond_wait(&queuecond, &queuemtx);
		}

		qd = l->data;
		LLRemove(queue,qd);

		pthread_mutex_unlock(&queuemtx);

		printf("%d: %s\n", qd->d, qd->s);
		free(qd);
	}
}


int main()
{
	pthread_t th;
	int i;

	queue = LLAlloc();

	printf("creating thread\n");
	pthread_create(&th, NULL, reader, NULL);


	for (i = 0; i < 10000; i++)
	{
		struct QueueData *qd;

		/* printf("writing %d\n", i); */
	   
		qd = malloc(sizeof(struct QueueData));

		qd->d = i;
		qd->s = "foo!";

		pthread_mutex_lock(&queuemtx);

		LLAdd(queue, qd);

		pthread_cond_signal(&queuecond);

		pthread_mutex_unlock(&queuemtx);

		if (rand() > RAND_MAX/2) sleep(1);
	}

	printf("finished writing data to queue\n");

	pthread_join(th, NULL);

	LLFree(queue);

}

