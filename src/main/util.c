
/* dist: public */

#include <time.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef WIN32

#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#else

#include <sys/timeb.h>

#endif

#include "pthread.h"

/* make sure to get the prototypes for thread functions instead of macros */
#define USE_PROTOTYPES

#include "util.h"
#include "defs.h"

#ifdef USING_MSVC
#include "snprintf.c"
#endif


struct HashEntry
{
	struct HashEntry *next;
	void *p;
	char key[1];
};



ticks_t current_ticks(void)
{
#ifndef WIN32
	struct timespec ts;
	// not affected by leap seconds, user changing the clock, etc
	clock_gettime(CLOCK_MONOTONIC, &ts);

	// (ticks_t is unsigned)
	ticks_t ticks = TICK_MAKE(ts.tv_sec);
	ticks *= 100U; // (ticks * 100) % 2^32
	ticks += ts.tv_nsec / 10000000;

	return TICK_MAKE(ticks);
#else
	return TICK_MAKE(GetTickCount() / 10);
#endif
}


ticks_t current_millis(void)
{
#ifndef WIN32
	struct timespec ts;
	// not affected by leap seconds, user changing the clock, etc
	clock_gettime(CLOCK_MONOTONIC, &ts);

	// (ticks_t is unsigned)
	ticks_t ticks = TICK_MAKE(ts.tv_sec);
	ticks *= 1000U; // (ticks * 1000) % 2^32
	ticks += ts.tv_nsec / 1000000;

	return TICK_MAKE(ticks);
#else
	return TICK_MAKE(GetTickCount());
#endif
}


void fullsleep(long millis)
{
#ifndef WIN32
	{
		struct timespec ts = { millis / 1000L , (millis % 1000L) * 1000000L };
		while (nanosleep(&ts, &ts) == -1)
			/* retry if interrupted */;
	}
#else
	/* pthread cancel does not cancel the thread during Sleep (unless a special driver is installed).
	 * This implementation sleeps for 512ms at a time to work around this issue.
	 * (Without this fix deadlock blocks asss shutdown for a long time)
	 */
	int count = millis / 512;
	for (int i = 0; i < count; ++i)
	{
		Sleep(512);
	}

	int remain = millis % 512;
	Sleep(remain);
#endif
}

void alocaltime_r(time_t *t, struct tm *_tm)
{
#ifndef WIN32
	localtime_r(t, _tm);
#else
	struct tm *now;
	/* thread-safe in msvcrt (which mingw links with) */
	now = localtime(t);
	memcpy(_tm, now, sizeof(struct tm));
#endif
}

TimeoutSpec schedule_timeout(unsigned milliseconds)
{
	TimeoutSpec ret;

#ifndef WIN32
	clock_gettime(CLOCK_MONOTONIC, &ret.target);
	ret.target.tv_sec  += milliseconds / 1000u;
	ret.target.tv_nsec += (milliseconds % 1000u) * 1000000u;
	ret.target.tv_sec  += ret.target.tv_nsec / 1000000000;
	ret.target.tv_nsec  = ret.target.tv_nsec % 1000000000;
#else
	/* win32 pthread uses _ftime64, which unfortunately has an accuracy of only 1 second */
	ret.target = GetTickCount() + (u32) milliseconds;
#endif

	return ret;
}

char *RemoveCRLF(char *p)
{
	char *t;
	if ((t = strchr(p,0x0A))) *t = 0;
	if ((t = strchr(p,0x0D))) *t = 0;
	return p;
}

char *ToLowerStr(char *str)
{
	char *s = str;
	for (s = str; *s; s++)
		if (isupper(*s))
			*s = tolower(*s);
	return str;
}

void *amalloc(size_t s)
{
	void *ptr;
	ptr = malloc(s);
	if (!ptr)
		Error(EXIT_MEMORY,"malloc error: requested %i bytes\n",s);
	memset(ptr, 0, s);
	return ptr;
}

void *arealloc(void *p, size_t s)
{
	void *n;
	n = realloc(p, s);
	if (!n)
		Error(EXIT_MEMORY,"realloc error: requested %i bytes\n",s);
	return n;
}

char *astrdup(const char *s)
{
	char *r;
	if (!s)
		return NULL;
	r = strdup(s);
	if (!r)
		Error(EXIT_MEMORY,"strdup error\n");
	return r;
}

void afree(const void *ptr)
{
#ifdef FREE_DOESNT_CHECK_NULL
	if (ptr)
#endif
	free((void*)ptr);
}

void Error(int level, char *format, ...)
{
	va_list argptr;
	fprintf(stderr,"Unrecoverable error (%i): ", level);
	va_start(argptr, format);
	vfprintf(stderr, format, argptr);
	va_end(argptr);
	fprintf(stderr, "\n");
	exit(level);
}

#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(maj, min) 0
#endif

void set_thread_name(pthread_t thread, const char *format, ...)
{
#if !defined(WIN32) && __GLIBC_PREREQ(2, 12)
	va_list args;
	char name[255];

	va_start(args, format);

	if (vsnprintf(name, 255, format, args) < 0)
	{
		va_end(args);
		return;
	}

	pthread_setname_np(thread, name);
	va_end(args);
#endif
}

char *astrncpy(char *dest, const char *source, size_t n)
{
	strncpy(dest, source, n-1);
	dest[n-1] = 0;
	return dest;
}


const char *delimcpy(char *dest, const char *source, size_t destlen, char delim)
{
	for ( ; *source && *source != delim; source++)
		if (destlen > 1)
		{
			*dest++ = *source;
			destlen--;
		}
	*dest = '\0';
	return *source == delim ? source + 1 : NULL;
}


int strsplit(const char *big, const char *delims, char *buf, int buflen, const char **ptmp)
{
	const char *tmp = *ptmp;
	/* if the first time, init pointer to start of string */
	if (!tmp)
	{
		if (big)
			tmp = big;
		else
			return 0;
	}
	/* now pick up where we left off */
	/* move past delims */
	while (*tmp && strchr(delims, *tmp)) tmp++;
	/* check if we moved off end of string */
	if (!*tmp) return 0;
	/* copy into buf until max or delim or end of string */
	for ( ; *tmp && !strchr(delims, *tmp); tmp++)
		if (buflen > 1)
		{
			*buf++ = *tmp;
			buflen--;
		}
	/* terminate with nil */
	*buf = '\0';
	/* replace tmp pointer */
	*ptmp = tmp;
	return 1;
}


void wrap_text(const char *txt, int mlen, char delim,
		void (*cb)(const char *line, void *clos), void *clos)
{
	char line[256], buf[256], delimstr[2] = { delim, '\0' };
	const char *p = txt;
	int l;

	if (mlen > 250) mlen = 250;

	strcpy(line, "");

	while (*p)
	{
		p = delimcpy(buf, p, mlen, delim);
		if (!p) p = "";

		/* find eventual width */
		l = strlen(line) + strlen(buf);
		if (l > mlen)
		{
			/* kill last delim */
			int lst = strlen(line)-1;
			if (line[lst] == delim) line[lst] = 0;

			cb(line, clos);
			line[0] = 0;
		}

		strcat(line, buf);
		if (*p)
			strcat(line, delimstr);
	}

	if (line[0] && line[0] != delim)
		cb(line, clos);
}


/* LinkedList data type */

#ifdef CFG_USE_FREE_LINK_LIST

#define LINKSATONCE 510 /* enough to almost fill a page */

static Link *freelinks = NULL;

#ifdef _REENTRANT

local pthread_mutex_t freelinkmtx = PTHREAD_MUTEX_INITIALIZER;

#define LOCK_FREE() pthread_mutex_lock(&freelinkmtx)
#define UNLOCK_FREE() pthread_mutex_unlock(&freelinkmtx)

#else

#define LOCK_FREE()
#define UNLOCK_FREE()

#endif

local void GetSomeLinks(void)
{
	Link *mem, *start;
	int i;

	start = mem = amalloc(LINKSATONCE * sizeof(Link));
	for (i = 0; i < LINKSATONCE-1; i++, mem++)
		mem->next = mem + 1;
	mem->next = freelinks;
	freelinks = start;
}

local inline Link *GetALink(void)
{
	Link *ret;
	LOCK_FREE();
	if (!freelinks) GetSomeLinks();
	ret = freelinks;
	freelinks = freelinks->next;
	UNLOCK_FREE();
	return ret;
}

local inline void FreeALink(Link *l)
{
	LOCK_FREE();
	l->next = freelinks;
	freelinks = l;
	UNLOCK_FREE();
}

#else

local inline Link *GetALink(void)
{
	return amalloc(sizeof(Link));
}

local inline void FreeALink(Link *l)
{
	afree(l);
}

#endif


void LLInit(LinkedList *lst)
{
	lst->start = lst->end = NULL;
}

LinkedList * LLAlloc(void)
{
	LinkedList *ret;
	/* HACK: depends on LinkedList and Link being the same size */
	ret = (LinkedList*)GetALink();
	LLInit(ret);
	return ret;
}

void LLEmpty(LinkedList *lst)
{
	Link *n = lst->start, *t;

#ifdef CFG_USE_FREE_LINK_LIST
	if (n)
	{
		LOCK_FREE();
		t = freelinks;
		freelinks = n;
		while (n->next)
			n = n->next;
		n->next = t;
		UNLOCK_FREE();
	}
#else
	while (n)
	{
		t = n->next;
		afree(n);
		n = t;
	}
#endif
	lst->start = lst->end = NULL;
}

void LLFree(LinkedList *lst)
{
	/* HACK: see above */
	LLEmpty(lst);
	FreeALink((Link*)lst);
}

void LLAdd(LinkedList *lst, const void *p)
{
	Link *n = GetALink();

	n->next = NULL;
	n->data = (void*)p;

	if (lst->end)
	{
		lst->end->next = n;
		lst->end = n;
	}
	else
	{
		lst->start = lst->end = n;
	}
}

void LLAddFirst(LinkedList *lst, const void *data)
{
	Link *n = GetALink();

	n->next = lst->start;
	n->data = (void*)data;

	lst->start = n;
	if (lst->end == NULL)
		lst->end = n;
}

void LLInsertAfter(LinkedList *lst, Link *link, const void *data)
{
	Link *n;

	if (link)
	{
		n = GetALink();
		n->next = link->next;
		n->data = (void*)data;
		link->next = n;
		if (lst->end == link)
			lst->end = n;
	}
	else
		LLAddFirst(lst, data);
}

int LLRemove(LinkedList *lst, const void *p)
{
	Link *n = lst->start, *prev = NULL;
	while (n)
	{
		if (n->data == p)
		{
			if (lst->start == n)
			{
				lst->start = n->next;
				if (lst->start == NULL) lst->end = NULL;
			}
			else
			{
				prev->next = n->next;
				if (n == lst->end) lst->end = prev;
			}
			FreeALink(n);
			return 1;
		}
		prev = n;
		n = n->next;
	}
	return 0;
}

int LLRemoveAll(LinkedList *lst, const void *p)
{
	Link *n = lst->start, *prev = NULL, *next;
	int removed = 0;

	while (n)
	{
		next = n->next;
		if (n->data == p)
		{
			if (prev == NULL) /* first link */
				lst->start = next;
			else
				prev->next = next;
			if (next == NULL) lst->end = prev;
			FreeALink(n);
			removed++;
		}
		else
			prev = n;
		n = next;
	}

	return removed;
}

void *LLRemoveFirst(LinkedList *lst)
{
	Link *lnk;
	void *ret;

	if (lst->start == NULL)
		return NULL;

	ret = lst->start->data;

	lnk = lst->start;
	lst->start = lst->start->next;

	FreeALink(lnk);

	if (lst->start == NULL)
		lst->end = NULL;

	return ret;
}

Link * LLGetHead(LinkedList *lst)
{
	return lst->start;
}

int LLIsEmpty(LinkedList *lst)
{
	return lst->start == NULL;
}

int LLCount(LinkedList *lst)
{
	Link *l;
	int c = 0;
	for (l = lst->start; l; l = l->next) c++;
	return c;
}

int LLMember(LinkedList *lst, const void *p)
{
	Link *l;
	for (l = lst->start; l; l = l->next)
		if (l->data == p)
			return TRUE;
	return FALSE;
}

void LLEnum(LinkedList *lst, void (*func)(const void *ptr))
{
	Link *l;
	for (l = lst->start; l; l = l->next)
		func(l->data);
}

void LLEnumNC(LinkedList *lst, void (*func)(void *ptr))
{
	Link *l;
	for (l = lst->start; l; l = l->next)
		func(l->data);
}


static void sort_work_split(Link *src, Link **a, Link **b)
{
	Link *s1 = src, *s2 = src;
	/* s2 moves at double speed, so s1 will be half-way through the list
	 * when s2 hits the end. */
	for (;;)
	{
		s2 = s2->next ? s2->next->next : NULL;
		if (!s2) break;
		s1 = s1->next;
	}

	*a = src;
	if (s1)
	{
		*b = s1->next;
		s1->next = NULL;
	}
	else
		*b = NULL;
}

static Link * sort_work_merge(Link *a, Link *b, int (*lt)(const void *a, const void *b))
{
	Link *list = NULL, **tail = &list;

	while (a && b)
	{
		if (lt(a->data, b->data))
		{
			*tail = a;
			tail = &a->next;
			a = a->next;
		}
		else
		{
			*tail = b;
			tail = &b->next;
			b = b->next;
		}
	}

	if (a)
		*tail = a;
	else if (b)
		*tail = b;

	return list;
}

static Link * sort_work(Link *l, int (*lt)(const void *a, const void *b))
{
	Link *a = NULL, *b = NULL;

	if (!l || !l->next)
		return l;

	sort_work_split(l, &a, &b);
	return sort_work_merge(sort_work(a, lt), sort_work(b, lt), lt);
}

static int generic_lt(const void *a, const void *b)
{
	return a < b;
}

void LLSort(LinkedList *lst, int (*lt)(const void *a, const void *b))
{
	/* this disregards the end pointer */
	lst->start = sort_work(lst->start, lt ? lt : generic_lt);
	if (lst->start)
	{
		/* restore the end pointer */
		Link *l = lst->start;
		while (l->next)
			l = l->next;
		lst->end = l;
	}
}

int LLSort_StringCompare(const void *a, const void *b)
{
	const char *as = a;
	const char *bs = b;
	return strcmp(as, bs) < 0;
}


/* HashTable data type */


static inline HashEntry *alloc_entry(const char *key)
{
	HashEntry *ret = amalloc(sizeof(HashEntry) + strlen(key));
	ret->p = ret->next = NULL;
	strcpy(ret->key, key);
	return ret;
}

/* note: this is a case-insensitive hash! */
static inline unsigned long hash_string(const char *s)
{
	unsigned long h = 0xf0e1d2;
	while (*s)
		h ^= (h << 7) + (h >> 2) + ((*s++) | 32);
	return h;
}

static void check_rehash(HashTable *h)
{
	int newbucketsm1, i, load = h->ents * 100 / (h->bucketsm1+1);
	HashEntry **newlists;

	/* figure out if we need to rehash, and how big the resulting table
	 * will be */
	if (load > h->maxload)
		newbucketsm1 = h->bucketsm1 * 2 + 1;
	else if (load < (h->maxload / 4) && h->bucketsm1 > 15)
		newbucketsm1 = (h->bucketsm1 - 1) / 2;
	else
		return;

	/* allocate the new bucket array */
	newlists = amalloc((newbucketsm1+1) * sizeof(HashEntry *));
	for (i = 0; i <= newbucketsm1; i++)
		newlists[i] = NULL;

	/* rehash entries. this will end up with the chains in backwards
	 * order */
	for (i = 0; i <= h->bucketsm1; i++)
	{
		HashEntry *next, *e = h->lists[i];
		while (e)
		{
			int newbucket = hash_string(e->key) & newbucketsm1;
			next = e->next;
			e->next = newlists[newbucket];
			newlists[newbucket] = e;
			e = next;
		}
	}

	/* so we reverse them */
	for (i = 0; i <= newbucketsm1; i++)
	{
		HashEntry *prev = NULL, *next, *e = newlists[i];
		while (e)
		{
			next = e->next;
			e->next = prev;
			prev = e;
			e = next;
		}
		newlists[i] = prev;
	}

	/* and finally clean up the old array */
	afree(h->lists);
	h->lists = newlists;
	h->bucketsm1 = newbucketsm1;
}

HashTable * HashAlloc(void)
{
	HashTable *h = amalloc(sizeof(*h));
	HashInit(h);
	return h;
}

void HashInit(HashTable *h)
{
	int i;
	h->bucketsm1 = 15;
	h->ents = 0;
	h->lists = amalloc((h->bucketsm1 + 1) * sizeof(HashEntry *));
	for (i = 0; i <= h->bucketsm1; i++)
		h->lists[i] = NULL;
	h->maxload = 75;
}

void HashDeinit(HashTable *h)
{
	HashEntry *e, *n;
	int i;
	for (i = 0; i <= h->bucketsm1; i++)
	{
		e = h->lists[i];
		while (e)
		{
			n = e->next;
			afree(e);
			e = n;
		}
	}
	afree(h->lists);
}

void HashFree(HashTable *h)
{
	HashDeinit(h);
	afree(h);
}

void HashEnum(HashTable *h,
		int (*func)(const char *key, void *val, void *data),
		void *data)
{
	int i, rem;
	for (i = 0; i <= h->bucketsm1; i++)
	{
		HashEntry *prev = NULL, *e = h->lists[i];
		while (e)
		{
			HashEntry *next = e->next;
			rem = func(e->key, e->p, data);
			if (rem)
			{
				if (prev)
					prev->next = next;
				else
					h->lists[i] = next;
				afree(e);
				h->ents--;
			}
			else
				prev = e;
			e = next;
		}
	}
	check_rehash(h);
}

const char * HashAdd(HashTable *h, const char *s, const void *p)
{
	int slot;
	HashEntry *e, *l;

	slot = hash_string(s) & h->bucketsm1;

	e = alloc_entry(s);
	e->p = (void*)p;
	e->next = NULL;

	l = h->lists[slot];
	if (l)
	{
		/* find end of list and insert it */
		while (l->next) l = l->next;
		l->next = e;
	}
	else
	{
		/* this is first hash entry for this key */
		h->lists[slot] = e;
	}

	h->ents++;
	check_rehash(h);
	return e->key;
}

void HashAddFront(HashTable *h, const char *s, const void *p)
{
	int slot;
	HashEntry *e;

	slot = hash_string(s) & h->bucketsm1;

	e = alloc_entry(s);
	e->p = (void*)p;
	e->next = h->lists[slot];
	h->lists[slot] = e;
	h->ents++;
	check_rehash(h);
}

void HashReplace(HashTable *h, const char *s, const void *p)
{
	int slot;
	HashEntry *l;

	slot = hash_string(s) & h->bucketsm1;
	l = h->lists[slot];

	/* try to find it */
	while (l)
	{
		if (!strcasecmp(s, l->key))
		{
			/* found it, replace data and return */
			l->p = (void*)p;
			/* no need to modify h->ents */
			return;
		}
		l = l->next;
	}

	/* it's not in the table, add normally */
	HashAdd(h, s, p);
}

void HashRemove(HashTable *h, const char *s, const void *p)
{
	int slot;
	HashEntry *l, *prev = NULL;

	slot = hash_string(s) & h->bucketsm1;
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key) && l->p == p)
		{
			if (prev)
				prev->next = l->next;
			else /* removing first item */
				h->lists[slot] = l->next;
			afree(l);
			h->ents--;
			check_rehash(h);
			return;
		}
		prev = l;
		l = l->next;
	}
}

void HashRemoveAny(HashTable *h, const char *s)
{
	int slot;
	HashEntry *l, *prev = NULL;

	slot = hash_string(s) & h->bucketsm1;
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key))
		{
			if (prev)
				prev->next = l->next;
			else /* removing first item */
				h->lists[slot] = l->next;
			afree(l);
			h->ents--;
			check_rehash(h);
			return;
		}
		prev = l;
		l = l->next;
	}
}

LinkedList * HashGet(HashTable *h, const char *s)
{
	LinkedList *res = LLAlloc();
	HashGetAppend(h, s, res);
	return res;
}

void HashGetAppend(HashTable *h, const char *s, LinkedList *res)
{
	int slot;
	HashEntry *l;

	slot = hash_string(s) & h->bucketsm1;
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key))
			LLAdd(res, l->p);
		l = l->next;
	}
}

void *HashGetOne(HashTable *h, const char *s)
{
	int slot;
	HashEntry *l;

	slot = hash_string(s) & h->bucketsm1;
	l = h->lists[slot];

	while (l)
	{
		if (!strcasecmp(s, l->key))
			return l->p;
		l = l->next;
	}
	return NULL;
}

int hash_enum_afree(const char *key, void *val, void *d)
{
	afree(val);
	return FALSE;
}

LinkedList *HashGetKeys(HashTable *h)
{
	int i;
	LinkedList *keys = LLAlloc();
	Link *link;
	const char *stored;

	for (i = 0; i <= h->bucketsm1; i++)
	{
		HashEntry *e = h->lists[i];
		while(e)
		{
			int add = TRUE;
			FOR_EACH(keys, stored, link)
			{
				int hash1 = hash_string(stored);
				int hash2 = hash_string(e->key);

				if(hash1 == hash2)
				{
					add = FALSE;
					break;
				}
			}

			if(add)
			{
				LLAdd(keys, e->key);
			}
			e = e->next;
		}
	}

	return keys;
}


#ifndef NODQ

void DQInit(DQNode *node)
{
	node->prev = node->next = node;
}

void DQAdd(DQNode *base, DQNode *node)
{
	base->prev->next = node;
	node->prev = base->prev;
	base->prev = node;
	node->next = base;
}

void DQRemove(DQNode *node)
{
	node->prev->next = node->next;
	node->next->prev = node->prev;
	node->next = node->prev = node;
}

int DQCount(DQNode *node)
{
	DQNode *n;
	int c = 0;
	for (n = node->next; n != node; n = n->next)
		c++;
	return c;
}

#endif



#ifndef NOTREAP

local TreapHead **tr_find(TreapHead **root, int key)
{
	for (;;)
		if ((*root) == NULL)
			return NULL;
		else if ((*root)->key == key)
			return root;
		else if ((*root)->key < key)
			root = &(*root)->right;
		else
			root = &(*root)->left;
}

TreapHead *TrGet(TreapHead *root, int key)
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
	else if ((*root)->key < node->key)
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

TreapHead *TrRemove(TreapHead **root, int key)
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

void TrEnum(TreapHead *root, void (*func)(TreapHead *node, void *clos), void *clos)
{
	if (root)
	{
		TreapHead *t;
		TrEnum(root->left, func, clos);
		/* save right child now because func might free root */
		t = root->right;
		func(root, clos);
		TrEnum(t, func, clos);
	}
}

void tr_enum_afree(TreapHead *node, void *dummy)
{
	afree(node);
}

#endif



#ifndef NOSTRINGCHUNK

#define SCSIZE 4000

struct StringChunk
{
	struct StringChunk *next;
	int room;
	char data[SCSIZE];
};

StringChunk *SCAlloc(void)
{
	StringChunk *c;

	c = amalloc(sizeof(StringChunk));
	c->next = NULL;
	c->room = SCSIZE;
	return c;
}

const char *SCAdd(StringChunk *chunk, const char *str)
{
	int len;
	StringChunk *prev = NULL;

	len = strlen(str)+1;

	if (len > SCSIZE)
	{
		/* too big for normal chunk. get specially sized chunk. */
		while (chunk)
		{
			prev = chunk;
			chunk = chunk->next;
		}
		if (prev)
		{
			StringChunk *new = amalloc(sizeof(StringChunk) - SCSIZE + len);
			new->next = NULL;
			new->room = 0;
			memcpy(new->data, str, len);
			prev->next = new;
			return new->data;
		}
		else
			return NULL;
	}

	while (chunk)
	{
		if (chunk->room >= len)
		{
			char *spot;
			spot = chunk->data + (SCSIZE - chunk->room);
			memcpy(spot, str, len);
			chunk->room -= len;
			return spot;
		}
		prev = chunk;
		chunk = chunk->next;
	}

	/* no room in any present chunk */
	if (prev)
	{
		chunk = SCAlloc();
		prev->next = chunk;
		/* recursive call to add it */
		return SCAdd(chunk, str);
	}
	else /* got a null chunk */
		return NULL;
}

void SCFree(StringChunk *chunk)
{
	StringChunk *old;
	while (chunk)
	{
		old = chunk->next;
		afree(chunk);
		chunk = old;
	}
}

#endif


#ifndef NOSTRINGBUFFER

void SBInit(StringBuffer *sb)
{
	sb->start = sb->end = sb->initial;
	sb->alloc = sizeof(sb->initial);
	sb->start[0] = '\0';
}

void SBPrintf(StringBuffer *sb, const char *fmt, ...)
{
	va_list args;
	int len, used;
	size_t needed;

	/* figure out how long the result is */
	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	/* figure out if we need to reallocate */
	used = sb->end - sb->start;
	needed = used + len + 1;
	if (sb->alloc < needed)
	{
		while (sb->alloc < needed)
			sb->alloc *= 2;
		if (sb->start == sb->initial)
		{
			/* need to allocate and copy */
			sb->start = amalloc(sb->alloc);
			memcpy(sb->start, sb->initial, sizeof(sb->initial));
		}
		else
		{
			/* buffer already heap-allocated, realloc */
			sb->start = arealloc(sb->start, sb->alloc);
		}
		sb->end = sb->start + used;
	}

	/* now print */
	va_start(args, fmt);
	vsnprintf(sb->end, len+1, fmt, args);
	va_end(args);

	sb->end += len;
}

const char * SBText(StringBuffer *sb, int offset)
{
	int len = sb->end - sb->start;
	return (offset < len) ? sb->start + offset : sb->end;
}

void SBDestroy(StringBuffer *sb)
{
	if (sb->start != sb->initial)
		afree(sb->start);
	sb->start = sb->end = NULL;
	sb->alloc = -1;
}

#endif


#ifndef NOMPQUEUE

void MPInit(MPQueue *q)
{
	LLInit(&q->list);
	pthread_mutex_init(&q->mtx, NULL);
#ifndef WIN32
	pthread_condattr_init(&q->condattr);
	pthread_condattr_setclock(&q->condattr, CLOCK_MONOTONIC);
	pthread_cond_init(&q->cond, &q->condattr);
#else
	pthread_cond_init(&q->cond, NULL);
#endif

}

void MPDestroy(MPQueue *q)
{
	LLEmpty(&q->list);
	pthread_mutex_destroy(&q->mtx);
	pthread_cond_destroy(&q->cond);
#ifndef WIN32
	pthread_condattr_destroy(&q->condattr);
#endif
}

void MPAdd(MPQueue *q, void *data)
{
	pthread_mutex_lock(&q->mtx);
	LLAdd(&q->list, data);
	pthread_cond_signal(&q->cond);
	pthread_mutex_unlock(&q->mtx);
}

void * MPTryRemove(MPQueue *q)
{
	void *data;
	pthread_mutex_lock(&q->mtx);
	data = LLRemoveFirst(&q->list);
	pthread_mutex_unlock(&q->mtx);
	return data;
}

void * MPRemove(MPQueue *q)
{
	void *data;
	/* this is a cancellation point, so we have to be careful about
	 * cleanup. this casting is a bit hacky, but it's in the man page,
	 * so it can't be that bad. */
	pthread_cleanup_push((void(*)(void*)) pthread_mutex_unlock, (void*) &q->mtx);
		pthread_mutex_lock(&q->mtx);
		while (LLIsEmpty(&q->list))
			pthread_cond_wait(&q->cond, &q->mtx);
		data = LLRemoveFirst(&q->list);
	pthread_cleanup_pop(1);
	return data;
}

#ifndef WIN32

void * MPTimeoutRemove(MPQueue *q, TimeoutSpec timeout)
{
	void *data;
	int rc = 0;

	/* this is a cancellation point, so we have to be careful about
	 * cleanup. this casting is a bit hacky, but it's in the man page,
	 * so it can't be that bad. */
	pthread_cleanup_push((void(*)(void*)) pthread_mutex_unlock, (void*) &q->mtx);

	pthread_mutex_lock(&q->mtx);

	/* ETIMEDOUT (!= 0) is returned if the timeout elapsed */
	while (LLIsEmpty(&q->list) && rc == 0)
	{
		 rc = pthread_cond_timedwait(&q->cond, &q->mtx, &timeout.target);
	}

	data = LLRemoveFirst(&q->list);

	pthread_cleanup_pop(1);
	return data;
}

#else

/* win32 pthread uses _ftime64, which unfortunately has an accuracy of only 1 second */
void * MPTimeoutRemove(MPQueue *q, TimeoutSpec timeout)
{
	void *data;

	while(1)
	{
		pthread_mutex_lock(&q->mtx);
		data = LLRemoveFirst(&q->list);
		pthread_mutex_unlock(&q->mtx);

		if (data || GetTickCount() >= timeout.target)
		{
			return data;
		}

		Sleep(1);
	}
}

#endif

void MPClear(MPQueue *q)
{
	pthread_mutex_lock(&q->mtx);
	LLEmpty(&q->list);
	pthread_mutex_unlock(&q->mtx);
}

void MPClearOne(MPQueue *q, void *data)
{
	pthread_mutex_lock(&q->mtx);
	LLRemoveAll(&q->list, data);
	pthread_mutex_unlock(&q->mtx);
}

#endif /* MPQUEUE */


#ifndef NOMMAP

typedef struct PrivMMD
{
#ifdef WIN32
	void *hFile, *hMapping;
#endif
} PrivMMD;


MMapData * MapFile(const char *filename, int writable)
{
	MMapData *mmd = amalloc(sizeof(MMapData) + sizeof(PrivMMD));

#ifdef WIN32
	PrivMMD *pmmd = (PrivMMD*)(mmd+1);
	FILETIME mtime;

	pmmd->hFile = CreateFile(
			filename,
			GENERIC_READ | (writable ? GENERIC_WRITE : 0),
			FILE_SHARE_READ | (writable ? FILE_SHARE_WRITE : 0),
			NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
			0);
	if (pmmd->hFile == INVALID_HANDLE_VALUE)
		goto fail1;

	mmd->len = GetFileSize(pmmd->hFile, NULL);
	if (mmd->len == -1)
		goto fail1;

	if (GetFileTime(pmmd->hFile, NULL, NULL, &mtime) == 0)
		goto fail1;
	mmd->lastmod = (time_t)mtime.dwLowDateTime;

	pmmd->hMapping = CreateFileMapping(
			pmmd->hFile,
			NULL,
			writable ? PAGE_READWRITE : PAGE_READONLY,
			0, 0, 0);
	if (!pmmd->hMapping)
		goto fail2;

	mmd->data = MapViewOfFile(
			pmmd->hMapping,
			writable ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ,
			0, 0, 0);
	if (!mmd->data)
		goto fail3;

	return mmd;

fail3:
	CloseHandle(pmmd->hMapping);
fail2:
	CloseHandle(pmmd->hFile);
#else

	int fd;
	struct stat st;

	fd = open(filename, writable ? O_RDWR : O_RDONLY);
	if (fd == -1)
		goto fail1;

	if (fstat(fd, &st) < 0)
		goto fail1;

	mmd->len = st.st_size;
	mmd->lastmod = st.st_mtime;

	mmd->data = mmap(NULL, mmd->len, PROT_READ | (writable ? PROT_WRITE : 0), MAP_SHARED, fd, 0);
	close(fd);
	if (mmd->data == MAP_FAILED)
		goto fail1;

	return mmd;
#endif

fail1:
	afree(mmd);
	return NULL;
}

int UnmapFile(MMapData *mmd)
{
#ifdef WIN32
	PrivMMD *pmmd = (PrivMMD*)(mmd+1);
	UnmapViewOfFile(mmd->data);
	CloseHandle(pmmd->hMapping);
	CloseHandle(pmmd->hFile);
#else
	munmap(mmd->data, mmd->len);
#endif

	afree(mmd);

	return 0;
}

void MapFlush(MMapData *mmd)
{
#ifdef WIN32
	FlushViewOfFile(mmd->data, 0);
#else
	msync(mmd->data, mmd->len, MS_ASYNC);
#endif
}

#endif /* NOMMAP */

