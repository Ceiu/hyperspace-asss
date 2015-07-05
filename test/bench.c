
/* 2>/dev/null
gcc -DNOTREAP -I ../src/include -I ../src -o bench bench.c ../src/main/util.c -lpthread
exit # */


#include "util.h"
#include <stdio.h>

typedef struct TreapHead
{
	struct TreapHead *left, *right;
	int pri;
	const char *key;
} TreapHead;


TreapHead **tr_find(TreapHead **root, const char *key)
{
	for (;;)
		if ((*root) == NULL)
			return NULL;
		else if (!strcasecmp((*root)->key, key))
			return root;
		else if (strcasecmp((*root)->key, key) < 0)
			root = &(*root)->right;
		else
			root = &(*root)->left;
}

TreapHead *TrGet(TreapHead *root, const char *key)
{
	TreapHead **p = tr_find(&root, key);
	return p ? *p : NULL;
}

#define TR_ROT_LEFT(node)              \
do {                                   \
    TreapHead *tmp = (*node)->right;   \
    (*node)->right = tmp->left;        \
    tmp->left = *node;                 \
    *node = tmp;                       \
} while(0)

#define TR_ROT_RIGHT(node)             \
do {                                   \
    TreapHead *tmp = (*node)->left;    \
    (*node)->left = tmp->right;        \
    tmp->right = *node;                \
    *node = tmp;                       \
} while(0)                             \

void TrPut(TreapHead **root, TreapHead *node)
{
	if (*root == NULL)
	{
		node->pri = rand();
		node->left = node->right = NULL;
		*root = node;
	}
	else if (strcasecmp((*root)->key, node->key) < 0)
	{
		TrPut(&(*root)->right, node);
		/* the node might now be the right child of root */
		if ((*root)->pri > (*root)->right->pri)
			TR_ROT_LEFT(root);
	}
	else
	{
		TrPut(&(*root)->left, node);
		/* the node might now be the left child of root */
		if ((*root)->pri > (*root)->left->pri)
			TR_ROT_RIGHT(root);
	}
}

TreapHead *TrRemove(TreapHead **root, const char *key)
{
	TreapHead **node, *tmp;

	node = tr_find(root, key);
	if (node == NULL)
		return NULL;

	while ((*node)->left || (*node)->right)
		if ((*node)->left == NULL)
		{
			TR_ROT_LEFT(node);
			node = &(*node)->left;
		}
		else if ((*node)->right == NULL)
		{
			TR_ROT_RIGHT(node);
			node = &(*node)->right;
		}
		else if ((*node)->right->pri < (*node)->left->pri)
		{
			TR_ROT_LEFT(node);
			node = &(*node)->left;
		}
		else
		{
			TR_ROT_RIGHT(node);
			node = &(*node)->right;
		}

	tmp = *node;
	*node = NULL;
	return tmp;
}

void TrEnum(TreapHead *root, void *clos, void (*func)(TreapHead *node, void *clos))
{
	if (root)
	{
		TreapHead *t;
		TrEnum(root->left, clos, func);
		/* save right child now because func might free it */
		t = root->right;
		func(root, clos);
		TrEnum(t, clos, func);
	}
}

void tr_enum_afree(TreapHead *node, void *dummy)
{
	afree(node);
}



const char *words[] =
{
	"Masai",
	"smokefarthings",
	"amphibologically",
	"intermontane",
	"nondiagrammatic",
	"Ramistical",
	"saprine",
	"microclimatic",
	"nuclei",
	"vicaress",
	"reclination",
	"cosmetology",
	"synectic",
	"sumption",
	"overhonestly",
	"noibwood",
	"outmost",
	"preboast",
	"monerozoic",
	"shuddery",
	"undripping",
	"transdialect",
	"Quadrumana",
	"leucodermatous",
	"cosiness",
	"nosism",
	"primordialism",
	"patrico",
	"forsaken",
	"multipolar",
	"trochantinian",
	"osteocarcinoma",
	"bider",
	"clubbism",
	"Mycteria",
	"semicombined",
	"nikethamide",
	"isotomous",
	"tongueful",
	"Hafgan",
	"monosaccharose",
	"baddish",
	"Feronia",
	"rickey",
	"plumbojarosite",
	"cucurbit",
	"advisedness",
	"lichenographical",
	"Thyreocoridae",
	"redressable",
	"varices",
	"works",
	"Voetian",
	"jailbird",
	"offertorial",
	"pallidly",
	"badan",
	"rectus",
	"unprofiteering",
	"dimity",
	"orthosymmetrically",
	"semidirect",
	"ichthyornithoid",
	"interwind",
	"parastatic",
	"kremlin",
	"unrosined",
	"overbet",
	"upas",
	"Yajna",
	"malposed",
	"Glaucidium",
	"anemosis",
	"erythrocytolysin",
	"parer",
	"microtheos",
	"earthnut",
	"unmaneged",
	"maneuver",
	"homodont",
	"plebeianize",
	"witchedly",
	"intralogical",
	"puissantness",
	"radiologist",
	"unnegotiable",
	"nowaday",
	"Picramnia",
	"allocryptic",
	"cherish",
	"unitemized",
	"raker",
	"rectilineally",
	"overcentralization",
	"irresolvability",
	"Dasyproctidae",
	"patrist",
	"woody",
	"seton",
	"proem",
	NULL
};

void add_t(TreapHead **t, const char *val)
{
	TreapHead *n = amalloc(sizeof(*n));
	n->key = val;
	TrPut(t, n);
}

void test1(int hsize, int trials)
{
	TreapHead *t = NULL;
	HashTable *h = HashAlloc();
	const char **w = words;
	unsigned int st, end, trial;

	st = clock();
	for (trial = 0; trial < trials; trial++)
	{
		/* add all words */
		for (w = words; *w; w++)
			add_t(&t, *w);
		/* remove all words */
		//for (w--; w > words; w--)
		for (w = words; *w; w++)
			afree(TrRemove(&t, *w));
	}
	end = clock();

	printf("adding and removing from treap: %f\n", (double)(end-st)/(double)trials);

	/* add all words */
	for (w = words; *w; w++)
		add_t(&t, astrdup(*w));
	st = clock();
	for (trial = 0; trial < trials; trial++)
	{
		for (w = words; *w; w++)
			TrGet(t, *w);
		for (w--; w > words; w--)
			TrGet(t, *w);
	}
	end = clock();
	/* remove all words */
	for (w = words; *w; w++)
		afree(TrRemove(&t, *w));

	printf("looking up words in treap: %f\n", (double)(end-st)/(double)trials);


	st = clock();
	for (trial = 0; trial < trials; trial++)
	{
		/* add all words */
		for (w = words; *w; w++)
			HashAdd(h, *w, (void*)*w);
		for (w = words; *w; w++)
			HashRemove(h, *w, (void*)*w);
	}
	end = clock();

	printf("adding and removing from hash: %f\n", (double)(end-st)/(double)trials);

	/* add all words */
	for (w = words; *w; w++)
		HashAdd(h, *w, (void*)*w);
	st = clock();
	for (trial = 0; trial < trials; trial++)
	{
		for (w = words; *w; w++)
		{
/* 			LinkedList *l = HashGet(h, *w); */
/* 			LLFree(l); */
			HashGetOne(h, *w);
		}
		for (w--; w > words; w--)
		{
/* 			LinkedList *l = HashGet(h, *w); */
/* 			LLFree(l); */
			HashGetOne(h, *w);
		}
	}
	end = clock();
	for (w = words; *w; w++)
		HashRemove(h, *w, (void*)*w);

	printf("looking up in hash: %f\n", (double)(end-st)/(double)trials);
}


int main(int argc, char *argv[])
{
	srand(current_ticks());
	test1(251, 5000);
}


/* relative memory usage:
 *
 * a hash table is 4+4*buckets bytes, plus 12*entries, plus 8*entries
 * (malloc overhead for entry), plus 8*entries (malloc overhead for
 * key).
 *
 * data is kept outside the node, so another 8*entries for that.
 *
 * a string treap is 16*entries bytes, plus 8*entries (malloc overhead
 * for nodes), plus 8*entries (malloc overhead for key).
 *
 * data is kept in the node, so no overhead for that.
 *
 * summary: hash table: 36 bytes/entry
 *        string treap: 32 bytes/entry
 *
 * by putting the key in the hash entry, we can get it down to 24
 * bytes/entry. treaps can never have the key in the entry. of course,
 * treaps don't need all those pointers for the buckets.
 *
 */

