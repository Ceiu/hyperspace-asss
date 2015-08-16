
/* dist: public */

#ifndef __UTIL_H
#define __UTIL_H

/** @file
 * various utility functions and data structures that you can (and
 * should) use.
 */

/* include for size_t */
#include <stddef.h>
#include <time.h>
#include "sizes.h"
#include "pthread.h"

#ifndef ATTR_FORMAT
#define ATTR_FORMAT(a,b,c)
#endif
#ifndef ATTR_MALLOC
#define ATTR_MALLOC()
#endif

#ifdef USING_MSVC
#include <stdarg.h>
#include <stdio.h>
#define vsnprintf rpl_vsnprintf
#define snprintf rpl_snprintf
#define vasprintf rpl_vasprintf
#define asprintf rpl_asprintf
int rpl_vsnprintf(char *, size_t, const char *, va_list);
int rpl_snprintf(char *, size_t, const char *, ...);
int rpl_vasprintf(char **, const char *, va_list);
int rpl_asprintf(char **, const char *, ...);
#endif

/** represents a time, either absolute or relative.
 * ticks are 31 bits in size. the value is stored in the lower 31 bits
 * of an unsigned int. don't do arithmetic on these directly, use the
 * TICK_* macros, to handle wraparound. */
typedef u32 ticks_t;

/** the difference between two ticks_t values */
#define TICK_DIFF(a,b) ((signed int)(((a)<<1)-((b)<<1))>>1)
/** whether a is later than b */
#define TICK_GT(a,b) (TICK_DIFF(a,b) > 0)
/** convert the result of some expression involving ticks into a legal
 ** ticks value. use like: TICK_MAKE(current_ticks() + 1000). */
#define TICK_MAKE(a) ((a) & 0x7fffffff)


typedef struct TimeoutSpec
{
#ifndef WIN32
	struct timespec target;
#else
	u32 target; /* milliseconds */
#endif
} TimeoutSpec;

/* miscellaneous stuff */

/** gets the current server time in ticks (1/100ths of a second). ticks
 ** are used for all game-related purposes. */
ticks_t current_ticks(void);
/** gets the current server time in milliseconds. these are used instead
 ** of ticks for things that need better resolution. */
ticks_t current_millis(void);

/** sleep for this many milliseconds, accurately. */
void fullsleep(long millis);

/** portable localtime_r wrapper */
void alocaltime_r(time_t *t, struct tm *_tm);

/** Some functions let you specific a timeout using a absolute time value.
 * This method creates such an absolute time value from a relative value.
 * @param milliseconds How many milliseconds from now that this timeout expires
 */
TimeoutSpec schedule_timeout(unsigned milliseconds);

/** strips a trailing CR or LF off the end of a string. this modifies
 ** the string, and returns it. */
char *RemoveCRLF(char *str);
/** changes all uppercase characters in a string to their lowercase
 ** equivalents, and returns the string. */
char *ToLowerStr(char *str);

/**  a wrapper around malloc that never returns NULL.
 * use this to do most memory allocation. */
void *amalloc(size_t bytes) ATTR_MALLOC();
/** a wrapper around realloc that never returns NULL. */
void *arealloc(void *p, size_t bytes);
/** a wrapper around strdup that never returns NULL. */
char *astrdup(const char *str);
/** a wrapper around free, to be paired with amalloc.
 * you may pass NULL values to afree. */
void afree(const void *ptr);

/** a safe version of strncpy. this will always leave a nul-terminated
 ** string in dest. */
char *astrncpy(char *dest, const char *source, size_t destlength);

/** a modified version of astrncpy for copying out of a field-delimited
 ** string.
 * it copies from source to dest, stopping at either the end of source,
 * after destlen chars, or when it hits the delimiter. this is best for
 * splitting fields out of a string with a fixed number of fields.
 * @param dest where to put the result
 * @param source where to copy from
 * @param destlen how much space is available at dest
 * @param delim which character is the delimiter.
 * @return if it hit a delimiter, returns the character following the
 * delimiter. if it hit the end of the string, returns NULL.
 */
const char *delimcpy(char *dest, const char *source, size_t destlen, char delim);

/** sort of like strtok, but better.
 * this should be used in a loop condition for separating items from a
 * list with an indefinite number of items. it accepts a set of
 * delimiters. it returns the next "token" from the original string.
 * it doesn't modify the source string. use like:
 * @code
 * const char *tmp = NULL;
 * char word[64];
 * while (strsplit(line, " ,:;", word, sizeof(word), &tmp))
 *     do_something_with_word;
 * @endcode
 * @param big the source string to be split
 * @param delims the set of delimiters to use
 * @param buf where to put the result
 * @param buflen how much space is in buf
 * @param ptmp a pointer to some space strsplit can use to keep state
 * across calls. you must initialize a const char * to NULL and provide
 * a pointer to it.
 * @return true if it put something in buf, false if there are no more
 * tokens.
 */
int strsplit(const char *big, const char *delims, char *buf, int buflen, const char **ptmp);

/** performs not-so-optimal word-wrapping of a really long line of text.
 * rather than returning the results in a buffer, this calls a callback
 * function for each split line that it produces.
 * @param txt the long line of text to split
 * @param mlen how many columns it should be split at. 80 is a good
 * choice.
 * @param delim the character to break at. usually a space.
 * @param cb the function to call on each line.
 * @param clos a closure argument.
 */
void wrap_text(const char *txt, int mlen, char delim,
		void (*cb)(const char *line, void *clos), void *clos);

/** immediately terminates the server with an error code.
 * @param code the code to exit with (EXIT_*)
 * @param message the message to print to stderr (with printf
 * formatting)
 * @see EXIT_NONE, etc.
 */
void Error(int code, char *message, ...) ATTR_FORMAT(printf, 2, 3);

/** Set the name of the given thread (or do nothing is this is not supported on your platform).
 * To see this name try `ps H -o 'pid tid cmd comm'` or `info thread` when you are debugging in GDB
 * @param thread posix thread
 * @name format printf format string
 */
void set_thread_name(pthread_t thread, const char *format, ...) ATTR_FORMAT(printf, 2, 3);

/* list manipulation functions */

/** the type of one linked list link.
 * it's ok to access its fields directly. */
typedef struct Link
{
	/** a pointer to the next link */
	struct Link *next;
	/** a pointer to some data */
	void *data;
} Link;

/** the type of a linked list.
 * don't access its fields directly; use LLGetHead. */
typedef struct LinkedList
{
	Link *start, *end;
} LinkedList;

/** use this to statically initialize LinkedList structs to avoid
 ** calling LLInit, if you so choose. */
#define LL_INITIALIZER { NULL, NULL }

/** allocates a new linked list. */
LinkedList * LLAlloc(void);
/** initializes a linked list allocated through some other means. */
void LLInit(LinkedList *lst);
/** frees memory occupied by the links of a list (not the items
 ** contained in it, nor the list header itself. */
void LLEmpty(LinkedList *lst);
/** frees the linked list struct (allocated with LLAlloc), and also the
 ** memory used by the links. */
void LLFree(LinkedList *lst);
/** adds a new pointer to the end of a linked list. */
void LLAdd(LinkedList *lst, const void *data);
/** adds a new pointer to the front of a linked list. */
void LLAddFirst(LinkedList *lst, const void *data);
/** adds a pointer to somewhere in the middle of a linked list.
 * the link had better belong to the list you say it does. */
void LLInsertAfter(LinkedList *lst, Link *link, const void *data);
/** remove the first occurance of a specific pointer from a list. */
int LLRemove(LinkedList *lst, const void *data);
/** remove all occurances of a specific pointer from a list. */
int LLRemoveAll(LinkedList *lst, const void *data);
/** removes the first item from a list, and returns it. */
void *LLRemoveFirst(LinkedList *lst);
/** returns true if the given list is empty. */
int LLIsEmpty(LinkedList *lst);
/** returns the number of items in a list. */
int LLCount(LinkedList *lst);
/** returns true if the given item is in the list. */
int LLMember(LinkedList *lst, const void *data);
/** calls a function on each item in a list. */
void LLEnum(LinkedList *lst, void (*func)(const void *ptr));
/** calls a function on each item in a list. the item is passed as non-const.*/
void LLEnumNC(LinkedList *lst, void (*func)(void *ptr));
/** returns the first link in the list. */
Link *LLGetHead(LinkedList *lst);
/** sorts the list. the comparator must return true if a is less than b.
 * the default comparator uses pointer value. */
void LLSort(LinkedList *lst, int (*lt)(const void *a, const void *b));
/** a comparator that uses string values. */
int LLSort_StringCompare(const void *a, const void *b);

#ifndef USE_PROTOTYPES
#define LLGetHead(lst) ((lst)->start)
#define LLIsEmpty(lst) ((lst)->start == NULL)
#endif

#define FOR_EACH(list, var, link) \
	for (link = LLGetHead(list); \
	link && ((var = link->data, link = link->next) || 1); )


/* hashing stuff */

typedef struct HashEntry HashEntry;

/** these hash tables support case-insensitive strings as keys. they
 * support multiple values per key. they're reasonably memory efficient,
 * and auto-resize the table when the number of items passes various
 * thresholds.
 */
typedef struct HashTable
{
	int bucketsm1, ents;
	int maxload; /* percent out of 100 */
	HashEntry **lists;
} HashTable;

/** allocate a new hash table and initializes it. */
HashTable * HashAlloc(void);
/** initializes a hash table. */
void HashInit(HashTable *table);
/** frees memory used inside the hashtable and removes all entries.
 * the table is unusable after this unless you HashInit it again! */
void HashDeinit(HashTable *table);
/** frees a hash table and all associated memory. */
void HashFree(HashTable *table);
/** inserts a new key, value pair into the table.
 * this value will show up at the end of the list when this key is
 * queried for. a pointer to the hash table's internal copy of the key
 * is returned. */
const char * HashAdd(HashTable *table, const char *key, const void *value);
/** inserts a new key, value pair into the table.
 * this value will show up at the front of the list when this key is
 * queried for. */
void HashAddFront(HashTable *table, const char *key, const void *value);
/** inserts a new key, value pair into the table, replacing one previous
 ** value associated with this key.
 * if you use only HashReplace on a table, it effectively behaves like a
 * standard map that supports one value for each key.
 */
void HashReplace(HashTable *table, const char *key, const void *value);
/** removes one instance of the key, value pair from the table. */
void HashRemove(HashTable *table, const char *key, const void *value);
/** removes the first instance of the key from the table. */
void HashRemoveAny(HashTable *table, const char *key);
/** returns a list with all values that the key maps to in the table. */
LinkedList *HashGet(HashTable *table, const char *key);
/** appends all values that the key maps to in the table, to the given list. */
void HashGetAppend(HashTable *table, const char *key, LinkedList *ll);
/** returns the first value that the key maps to in the table, or NULL
 ** if it's not present. */
void *HashGetOne(HashTable *table, const char *key);
/** calls a function for each key, value pair in the table.
 * the function should return true if that item should be removed. */
/** returns a list of all unique keys contained in the table. */
LinkedList *HashGetKeys(HashTable *h);
void HashEnum(HashTable *table,
		int (*func)(const char *key, void *val, void *clos),
		void *clos);
/** a function to use with HashEnum that calls afree on each key. */
int hash_enum_afree(const char *key, void *val, void *clos);

#ifndef NODQ

/** a header for a double-queue node.
 * you should declare structs with this as the first field.
 * the (singly-)linked list functions are a little easier to use and
 * should be used unless you need constant-time removal.
 */
typedef struct DQNode
{
	struct DQNode *prev, *next;
} DQNode;

/** initializes a node of a double-queue */
void DQInit(DQNode *node);
/** inserts a new double-queue node before the given base node */
void DQAdd(DQNode *base, DQNode *node);
/** removes a double-queue node from whatever queue it's on. */
void DQRemove(DQNode *node);
/** returns the number of nodes on this queue. */
int DQCount(DQNode *node);

#endif


#ifndef NOTREAP

/** a header for a treap node.
 * the only field you should read or write in this struct is key. */
typedef struct TreapHead
{
	struct TreapHead *left, *right;
	int pri, key;
} TreapHead;

/** retrieves the treap node with the key from the treap. */
TreapHead *TrGet(TreapHead *root, int key);
/** adds the given node to the treap.
 * note that you need to pass a pointer to the root node, because it
 * might change. node->head.key should be set to the key of the node
 * before calling this. */
void TrPut(TreapHead **root, TreapHead *node);
/** removes a node with the given key from the treap.
 * note that you need to pass a pointer to the root node, because it
 * might change. returns the removed node, or NULL if the key wasn't
 * found. */
TreapHead *TrRemove(TreapHead **root, int key);
/** calls a function on each node in the treap. */
void TrEnum(TreapHead *root, void (*func)(TreapHead *node, void *clos), void *clos);
/** a callback function intended to be used with TrEnum, which simply
 ** calls afree on each node it gets passed. */
void tr_enum_afree(TreapHead *node, void *dummy);

#endif


#ifndef NOSTRINGCHUNK

/* string chunks: idea stolen from glib */

/** the type for a string chunk.
 * a string chunk is a way of storing a bunch of small strings
 * efficiently. you can add strings to it to make copies of them, and
 * the free the whole chunk at once.
 */
typedef struct StringChunk StringChunk;

/** allocates a new string chunk. */
StringChunk *SCAlloc(void);
/** adds a string to a chunk and returns a pointer to the copy, or NULL
 ** if no memory. */
const char *SCAdd(StringChunk *chunk, const char *str);
/** frees a string chunk. */
void SCFree(StringChunk *chunk);

#endif


#ifndef NOSTRINGBUFFER

/* stringbuffer: an append-only string buffer thingy. */

typedef struct StringBuffer StringBuffer;

void SBInit(StringBuffer *sb);
void SBPrintf(StringBuffer *sb, const char *fmt, ...) ATTR_FORMAT(printf, 2, 3);
const char * SBText(StringBuffer *sb, int offset);
void SBDestroy(StringBuffer *sb);

/* don't look, implementation details */
struct StringBuffer
{
	char *start, *end;
	size_t alloc;
	char initial[128];
};

#endif


#ifndef NOMPQUEUE

/* message passing queue stuff */

/** a simple thread-safe queue built on top of pthreads.
 * don't access its members directly. */
typedef struct MPQueue
{
	LinkedList list;
	pthread_mutex_t mtx;
	pthread_cond_t cond;
	pthread_condattr_t condattr;
} MPQueue;

/** initalizes an mpqueue. you need to allocate memory for it yourself. */
void MPInit(MPQueue *mpq);
/** destroys an mpqueue and any associated memory. */
void MPDestroy(MPQueue *mpq);
/** adds an item to the end of the queue. doesn't block. */
void MPAdd(MPQueue *mpq, void *data);
/** tries to remove an item from the front of the queue.
 * if there's an item, it returns it. if the queue is empty it returns
 * NULL. this will not block. */
void * MPTryRemove(MPQueue *mpq);
/** removes an item from the front of the queue.
 * if the queue is empty, this will block until there's something to
 * remove, and then return it. */
void * MPRemove(MPQueue *mpq);
/** Removes an item from the front of the queue.
 * this function will wait up to the specified milliseconds (see schedule_timeout()) if necessary
 * for an item to become available. If an item becomes available
 * before that timeout this function will return early with that item.
 * This function only returns NULL if the given timeout has expired.
 */
void * MPTimeoutRemove(MPQueue *q, TimeoutSpec timeout);
/** clear the current contents of the queue. */
void MPClear(MPQueue *mpq);
/** clear all instances of this item from the queue. */
void MPClearOne(MPQueue *mpq, void *data);

#endif /* MPQUEUE */


#ifndef NOMMAP

/* memory mapped files stuff */

/** an object representing a memory-mapped file.
 * this is a thin and relatively few-featured wrapper over the memory
 * mapping provided by the OS. */
typedef struct MMapData
{
	/** a pointer to the data in the file */
	void *data;
	/** how long the file is. don't read/write past this offset from data. */
	int len;
	/** some indication of the last time this file was modified.
	 * note that this might not be a real time_t value, but at least it
	 * will change when the file is modified. */
	time_t lastmod;
} MMapData;

/** memory-map a file.
 * @param filename the file to map
 * @param writable true if you want to be able to write to the memory
 * (and see the changes reflected in the file)
 * @return a MMapData structure, or NULL on error
 */
MMapData * MapFile(const char *filename, int writable);
/** unmaps a file and frees memory associated with a MMapData structure. */
int UnmapFile(MMapData *mmd);
/** flushes the current state of memory for a writable memory-mapped
 ** file to disk. */
void MapFlush(MMapData *mmd);

#endif


/* some macro-based stuff for making enum types in config files easier
 * to deal with. */

#define DEFINE_ENUM(MAP) \
enum { MAP(DEFINE_ENUM_HELPER) };

#define DEFINE_ENUM_HELPER(X) X,

#define DEFINE_FROM_STRING(NAME, MAP) \
static int NAME(const char *s, int def) \
{ if (s == NULL) return def; \
  else if (*s >= '0' && *s <= '9') return atoi(s); \
  else if (*s == '$') s++; \
  MAP(DEFINE_FROM_STRING_HELPER) return def; }

#define DEFINE_FROM_STRING_HELPER(X) if (strcasecmp(#X, s) == 0) return X; else

#define DEFINE_TO_STRING(NAME, MAP) \
static const char * NAME(int v) \
{ switch (v) { MAP(DEFINE_TO_STRING_HELPER) } return NULL; }

#define DEFINE_TO_STRING_HELPER(X) case X: return #X;


/* another helpful macro */

#define CLIP(x, low, high) \
	do { if ((x) > (high)) (x) = (high); else if ((x) < (low)) (x) = (low); } while (0)

/* type definitions */
typedef enum CType
{
	CT_INTEGER,
	CT_FLOAT,
	CT_STRING,
	CT_VOID,
	CT_ARENA,
	CT_PLAYER,
	CT_PLAYERLIST,
	CT_TARGET
} CType;

#endif

