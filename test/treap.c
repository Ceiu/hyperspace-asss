
/* 2>/dev/null
gcc -g -I ../src/include -I ../src -o treap treap.c ../src/main/util.c -lpthread
exit # */

#include <stdlib.h>
#include <stdio.h>
#include "util.h"


void print(TreapHead *node, void *dummy)
{
	printf("  %10d (pri %10d)\n", node->key, node->pri);
}


void sumdepths(TreapHead *t, int *sum, int *max, int *leaves, int lev)
{
	if (t)
	{
		*sum += lev;
		if (t->left == NULL && t->right == NULL) {
			*leaves += 1;
		}
		if (lev > *max)
			*max = lev;
		sumdepths(t->left, sum, max, leaves, lev + 1);
		sumdepths(t->right, sum, max, leaves, lev + 1);
	}
}


void avgdepth(int n, int trials)
{
	TreapHead *t = NULL;
	int i, avg, q, max, leaves;

	for (q = 0; q < trials; q++)
	{
		for (i = 0; i < n; i++)
		{
			TreapHead *n = malloc(sizeof(*n));
			n->key = rand();
			TrPut(&t, n);
		}

		avg = 0;
		max = 0;
		leaves = 0;
		sumdepths(t, &avg, &max, &leaves, 1);
		printf("average depth: %f  max depth: %d\n", (double)avg/(double)n, max);

		TrEnum(t, tr_enum_afree, NULL);
		t = NULL;
	}
}


void getdel(int n)
{
	TreapHead *t = NULL;
	int i, avg, q, max, leaves;

	printf("adding nodes...\n");
	for (i = 0; i < n; i++)
	{
		TreapHead *n = amalloc(sizeof(*n));
		n->key = rand() % 10000;
		TrPut(&t, n);
	}

	for (;;)
	{
		char op;
		int key;

		printf("nodes:\n");
		TrEnum(t, print, NULL);

		printf("op key> ");
		scanf("%c %d", &op, &key);
		if (op == 'g')
		{
			TreapHead *n = TrGet(t, key);
			if (n)
				printf("found: key = %d\n", n->key);
			else
				printf("not found\n");
		}
		else if (op == 'd')
			afree(TrRemove(&t, key));
		else if (op == 'q')
			break;
	}
	TrEnum(t, tr_enum_afree, NULL);
}


int main(int argc, char *argv[])
{
	srand(current_ticks());

	avgdepth(10000, 50);

	getdel(atoi(argv[1]));
}


