
/* 2>/dev/null
gcc -o sockthread sockthread.c -lpthread
exit # */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <paths.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>


int sock;


void * thread1(void *dummy)
{
	int data[10];
	int ret;

	printf("reading from socket\n");
	ret = recv(sock, data, sizeof(data), 0);
	printf("done reading!: %i (%i)\n", ret, errno);
}


int main()
{
	pthread_t th;
	struct sockaddr_in sin;
	int i, ret;

	sock = socket(PF_INET, SOCK_DGRAM, 0);

	sin.sin_family = AF_INET;
	sin.sin_port = htons(5000);
	sin.sin_addr.s_addr = inet_addr("192.168.0.1");
	memset(sin.sin_zero, 0, sizeof(sin.sin_zero));
	connect(sock, &sin, sizeof(sin));

	printf("creating thread\n");
	pthread_create(&th, NULL, thread1, NULL);

	printf("sleeping 3 seconds\n");
	sleep(3);

	for (i = 0; i < 10; i++)
	{
		printf("attempting to write to socket\n");
		ret = send(sock, &sin, sizeof(sin), 0);
		printf("write returned: %i (%i)\n", ret, errno);
	}

	pthread_join(th, NULL);

}

