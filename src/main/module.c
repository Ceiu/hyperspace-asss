
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <assert.h>

#include "asss.h"



typedef struct ModuleData
{
	mod_args_t args;
	ModuleLoaderFunc loader;
	char loadername[16];
} ModuleData;

typedef struct AttachData
{
	ModuleData *mod;
	Arena *arena;
} AttachData;


local int LoadModule_(const char *);
local int UnloadModule(const char *);
local void EnumModules(void (*)(const char *, const char *, void *), void *, Arena *);
local int AttachModule(const char *, Arena *);
local int DetachModule(const char *, Arena *);
local int DetachAllFromArena(Arena *);
local const char *GetModuleInfo(const char *);
local const char *GetModuleLoader(const char *);

local void RegInterface(void *iface, Arena *arena);
local int UnregInterface(void *iface, Arena *arena);
local void *GetInterface(const char *id, Arena *arena);
local void *GetInterfaceByName(const char *name);
local void ReleaseInterface(void *iface);
local void *GetArenaInterface(const char *id, Arena *arena);
local void ReleaseArenaInterface(void *iface, Arena *arena);

local void RegCallback(const char *, void *, Arena *);
local void UnregCallback(const char *, void *, Arena *);
local void LookupCallback(const char *, Arena *, LinkedList *);
local void FreeLookupResult(LinkedList *);

local void RegAdviser(void *adv, Arena *arena);
local void UnregAdviser(void *adv, Arena *arena);
local void GetAdviserList(const char *id, Arena *arena, LinkedList *list);
local void ReleaseAdviserList(LinkedList *list);

local void RegModuleLoader(const char *sig, ModuleLoaderFunc func);
local void UnregModuleLoader(const char *sig, ModuleLoaderFunc func);

local void DoStage(int);
local void UnloadAllModules(void);
local void NoMoreModules(void);


local HashTable *arenacallbacks, *globalcallbacks;
local HashTable *arenaints, *globalints, *intsbyname;
local HashTable *arenaadvs, *globaladvs;
local HashTable *loaders;
local LinkedList mods;
local LinkedList attachments;
local int nomoremods;

local pthread_mutex_t modmtx;
local pthread_mutex_t intmtx = PTHREAD_MUTEX_INITIALIZER;
local pthread_mutex_t cbmtx = PTHREAD_MUTEX_INITIALIZER;
local pthread_mutex_t advmtx = PTHREAD_MUTEX_INITIALIZER;


local Imodman mmint =
{
	INTERFACE_HEAD_INIT(I_MODMAN, "modman")
	LoadModule_, UnloadModule, EnumModules,
	AttachModule, DetachModule,
	RegInterface, UnregInterface, GetInterface, GetInterfaceByName, ReleaseInterface,
	GetArenaInterface, ReleaseArenaInterface,
	RegCallback, UnregCallback, LookupCallback, FreeLookupResult,
	RegAdviser, UnregAdviser, GetAdviserList, ReleaseAdviserList,
	RegModuleLoader, UnregModuleLoader,
	GetModuleInfo, GetModuleLoader,
	DetachAllFromArena,
	{ DoStage, UnloadAllModules, NoMoreModules },
	{ NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
};




/* return an entry point */

Imodman * InitModuleManager(void)
{
	pthread_mutexattr_t attr;

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&modmtx, &attr);
	pthread_mutexattr_destroy(&attr);

	LLInit(&mods);
	LLInit(&attachments);
	arenacallbacks = HashAlloc();
	globalcallbacks = HashAlloc();
	arenaints = HashAlloc();
	globalints = HashAlloc();
	intsbyname = HashAlloc();
	arenaadvs = HashAlloc();
	globaladvs = HashAlloc();
	loaders = HashAlloc();
	mmint.head.global_refcount = 1;
	nomoremods = 0;
	/* for the benefit of python: */
	RegInterface(&mmint, ALLARENAS);
	return &mmint;
}

void DeInitModuleManager(Imodman *mm)
{
	if (LLGetHead(&mods))
		fprintf(stderr, "All modules not unloaded!!!\n");
	if (mm && mm->head.global_refcount > 1)
		fprintf(stderr, "There are remaining references to the module manager!!!\n");
	LLEmpty(&mods);
	HashFree(arenacallbacks);
	HashFree(globalcallbacks);
	HashFree(arenaints);
	HashFree(globalints);
	HashFree(intsbyname);
	HashFree(arenaadvs);
	HashFree(globaladvs);
	HashFree(loaders);
	pthread_mutex_destroy(&modmtx);
}


/* module management stuff */

local void RegModuleLoader(const char *sig, ModuleLoaderFunc func)
{
	pthread_mutex_lock(&modmtx);
	HashAdd(loaders, sig, func);
	pthread_mutex_unlock(&modmtx);
}

local void UnregModuleLoader(const char *sig, ModuleLoaderFunc func)
{
	pthread_mutex_lock(&modmtx);
	HashRemove(loaders, sig, func);
	pthread_mutex_unlock(&modmtx);
}


/* call with modmtx held */
local ModuleData *get_module_by_name(const char *name)
{
	Link *l;
	ModuleData *mod;
	FOR_EACH(&mods, mod, l)
	{
		if (!strcasecmp(mod->args.name, name))
			return mod;
	}
	return NULL;
}


local int LoadModule_(const char *spec)
{
	int ret = MM_FAIL;
	ModuleData *mod;
	char loadername[32] = "c";
	ModuleLoaderFunc loader;
	const char *t = spec;

	while (*t && isspace(*t)) t++;

	if (*t == '<')
	{
		t = delimcpy(loadername, t+1, sizeof(loadername), '>');
		if (!t)
		{
			fprintf(stderr, "E <module> bad module specifier: '%s'\n", spec);
			return MM_FAIL;
		}
		while (*t && isspace(*t)) t++;
	}

	pthread_mutex_lock(&modmtx);

	/* check if already loaded */
	if (get_module_by_name(t))
	{
		pthread_mutex_unlock(&modmtx);
		fprintf(stderr, "E <module> tried to load '%s' twice\n", t);
		return MM_FAIL;
	}

	loader = HashGetOne(loaders, loadername);
	if (loader)
	{
		mod = amalloc(sizeof(*mod));
		ret = loader(MM_LOAD, &mod->args, t, NULL);
		if (ret == MM_OK)
		{
			astrncpy(mod->loadername, loadername, sizeof(mod->loadername));
			mod->loader = loader;
			LLAdd(&mods, mod);
		}
		else
			afree(mod);
	}
	else
		fprintf(stderr, "E <module> can't find module loader for signature <%s>\n",
				loadername);

	pthread_mutex_unlock(&modmtx);

	return ret;
}


/* call with modmtx held */
local int unload_by_ptr(ModuleData *mod)
{
	Link *l;
	AttachData *ad;
	int ret;

	/* detach this module from all arenas it's attached to.
	 * TODO: this is inefficient.
	 * TODO: if a module unload fails, it will be detached from all
	 * arenas, but still loaded. */
	FOR_EACH(&attachments, ad, l)
	{
		if (ad->mod == mod)
		{
			mod->loader(MM_DETACH, &mod->args, NULL, ad->arena);
			LLRemove(&attachments, ad);
			afree(ad);
		}
	}

	ret = mod->loader(MM_UNLOAD, &mod->args, NULL, NULL);
	if (ret == MM_OK)
	{
		/* now unload it */
		LLRemove(&mods, mod);
		afree(mod);
	}

	return ret;
}


int UnloadModule(const char *name)
{
	int ret = MM_FAIL;
	ModuleData *mod;

	pthread_mutex_lock(&modmtx);
	mod = get_module_by_name(name);
	if (mod)
		ret = unload_by_ptr(mod);
	pthread_mutex_unlock(&modmtx);

	return ret;
}


/* call with modmtx held */
local void recursive_unload(Link *l)
{
	if (l)
	{
		ModuleData *mod = l->data;
		recursive_unload(l->next);
		if (unload_by_ptr(mod) == MM_FAIL)
			fprintf(stderr, "E <module> error unloading module %s\n",
					mod->args.name);
	}
}


void DoStage(int stage)
{
	Link *l;
	ModuleData *mod;
	pthread_mutex_lock(&modmtx);
	FOR_EACH(&mods, mod, l)
	{
		mod->loader(stage, &mod->args, NULL, NULL);
	}
	pthread_mutex_unlock(&modmtx);
}


void UnloadAllModules(void)
{
	pthread_mutex_lock(&modmtx);
	recursive_unload(LLGetHead(&mods));
	LLEmpty(&mods);
	pthread_mutex_unlock(&modmtx);
}


void NoMoreModules(void)
{
	nomoremods = 1;
}


void EnumModules(void (*func)(const char *, const char *, void *),
		void *clos, Arena *filter)
{
	Link *l;
	ModuleData *mod;
	AttachData *ad;
	pthread_mutex_lock(&modmtx);
	/* TODO: this returns modules in load order for no filter, and
	 * reverse-attach order for a filter. possibly change to use load
	 * order for both cases. */
	if (filter == NULL)
	{
		FOR_EACH(&mods, mod, l)
		{
			func(mod->args.name, mod->args.info, clos);
		}
	}
	else
	{
		FOR_EACH(&attachments, ad, l)
		{
			if (ad->arena == filter)
				func(ad->mod->args.name, ad->mod->args.info, clos);
		}
	}
	pthread_mutex_unlock(&modmtx);
}

local int is_attached(ModuleData *mod, Arena *arena, int remove)
{
	Link *l;
	AttachData *ad;
	FOR_EACH(&attachments, ad, l)
	{
		if (ad->mod == mod && ad->arena == arena)
		{
			if (remove)
			{
				LLRemove(&attachments, ad);
				afree(ad);
			}
			return TRUE;
		}
	}
	return FALSE;
}

int AttachModule(const char *name, Arena *arena)
{
	ModuleData *mod;
	int ret = MM_FAIL;
	pthread_mutex_lock(&modmtx);
	mod = get_module_by_name(name);
	if (mod &&
	    !is_attached(mod, arena, FALSE) &&
	    mod->loader(MM_ATTACH, &mod->args, NULL, arena) == MM_OK)
	{
		AttachData *ad = amalloc(sizeof(*ad));
		ad->mod = mod;
		ad->arena = arena;
		/* use LLAddFront so that detaching things happens in the
		 * reverse order of attaching. */
		LLAddFirst(&attachments, ad);
		ret = MM_OK;
	}
	pthread_mutex_unlock(&modmtx);
	return ret;
}

int DetachModule(const char *name, Arena *arena)
{
	ModuleData *mod;
	int ret = MM_FAIL;
	pthread_mutex_lock(&modmtx);
	mod = get_module_by_name(name);
	if (mod &&
	    is_attached(mod, arena, FALSE) &&
	    mod->loader(MM_DETACH, &mod->args, NULL, arena) == MM_OK)
	{
		is_attached(mod, arena, TRUE);
	}
	pthread_mutex_unlock(&modmtx);
	return ret;
}

int DetachAllFromArena(Arena *arena)
{
	int result = MM_OK;
	Link *l;
	AttachData *ad;
	pthread_mutex_lock(&modmtx);
	/* TODO: this is inefficient */
	FOR_EACH(&attachments, ad, l)
	{
		if (ad->arena == arena)
		{
			if (ad->mod->loader(MM_DETACH, &ad->mod->args, NULL, arena) == MM_FAIL)
			{
				Ilogman *lm = GetInterface(I_LOGMAN, ALLARENAS);
				if (lm)
				{
					lm->LogA(L_ERROR, "module", arena, "failed to detach '%s' while detaching modules", ad->mod->args.name);
					ReleaseInterface(lm);
				}
				result = MM_FAIL;
			}
			else
			{
				LLRemove(&attachments, ad);
				afree(ad);
			}
		}
	}
	pthread_mutex_unlock(&modmtx);
	return result;
}

const char * GetModuleInfo(const char *name)
{
	ModuleData *mod;
	const char *ret = NULL;
	pthread_mutex_lock(&modmtx);
	mod = get_module_by_name(name);
	if (mod)
		ret = mod->args.info;
	pthread_mutex_unlock(&modmtx);
	return ret;
}

const char * GetModuleLoader(const char *name)
{
	ModuleData *mod;
	const char *ret = NULL;
	pthread_mutex_lock(&modmtx);
	mod = get_module_by_name(name);
	if (mod)
		ret = mod->loadername;
	pthread_mutex_unlock(&modmtx);
	return ret;
}


/* interface management stuff */

void RegInterface(void *iface, Arena *arena)
{
	const char *id;
	InterfaceHead *head = (InterfaceHead*)iface;

	assert(iface);
	assert(head->magic == MODMAN_MAGIC);

	id = head->iid;

	pthread_mutex_lock(&intmtx);

	/* use HashAddFront so that newer interfaces override older ones.
	 * slightly evil, relying on implementation details of hash tables. */
	HashAddFront(intsbyname, head->name, iface);

	if (arena == ALLARENAS)
		HashAddFront(globalints, id, iface);
	else
	{
		char key[MAX_ID_LEN];

		if (!head->arena_refcounts)
			head->arena_refcounts = HashAlloc();

		/* increment this so we know to free head->arena_refcounts only when
		 * this interface is not registered anywhere anymore on an arena scope.
		 */
		++head->arena_registrations;

		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashAddFront(arenaints, key, iface);
	}

	pthread_mutex_unlock(&intmtx);
}

int UnregInterface(void *iface, Arena *arena)
{
	char key[MAX_ID_LEN];
	const char *id;
	InterfaceHead *head = (InterfaceHead*)iface;

	assert(head->magic == MODMAN_MAGIC);

	id = head->iid;

	/* this is a little messy: this function is overloaded with has the
	 * responsibility of checking that nobody is using this interface
	 * anymore, on the assumption that modules will call this as they
	 * unload, and abort the unload if someone still needs them. we do
	 * this with a simple refcount.
	 */

	/* this doesn't make a lot of sense with arena interfaces unless you use the
	 * new interface functions GetArenaInterface and ReleaseArenaInterface,
	 * which will also modify a reference count for the arena.
	 * if you don't, the old behavior will still be applied and the programmer
	 * accepts responsibility of that behavior. */
	if (arena == ALLARENAS && head->global_refcount > 0)
		return head->global_refcount;

	pthread_mutex_lock(&intmtx);

	if (arena != ALLARENAS)
	{
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, arena->name);
		long arena_refcount = (long)HashGetOne(head->arena_refcounts, key);
		if (arena_refcount > 0)
		{
			pthread_mutex_unlock(&intmtx);
			return arena_refcount;
		}
	}

	HashRemove(intsbyname, head->name, iface);
	if (arena == ALLARENAS)
	{
		HashRemove(globalints, id, iface);
	}
	else
	{
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashRemove(arenaints, key, iface);
		/* we incremented this when registering, now decrement it */
		--head->arena_registrations;
	}

	/* check to see if we should free up the arena_refcounts HashTable. */
	if (head->arena_refcounts != NULL && head->arena_registrations <= 0)
	{
		HashFree(head->arena_refcounts);
		head->arena_refcounts = NULL;
	}

	pthread_mutex_unlock(&intmtx);

	return 0;
}


void * GetInterface(const char *id, Arena *arena)
{
	InterfaceHead *head;

	pthread_mutex_lock(&intmtx);
	if (arena == ALLARENAS)
		head = HashGetOne(globalints, id);
	else
	{
		char key[MAX_ID_LEN];
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		head = HashGetOne(arenaints, key);
		/* if the arena doesn't have it, fall back to a global one */
		if (!head)
			head = HashGetOne(globalints, id);
	}
	pthread_mutex_unlock(&intmtx);
	if (head)
		++head->global_refcount;
	return head;

}


void * GetInterfaceByName(const char *name)
{
	InterfaceHead *head;

	pthread_mutex_lock(&intmtx);
	head = HashGetOne(intsbyname, name);

	if (head)
		++head->global_refcount;

	pthread_mutex_unlock(&intmtx);

	return head;
}


void ReleaseInterface(void *iface)
{
	/* this function should no longer be used with arena interfaces, however
	 * it is kept for backwards compatibility. */
	InterfaceHead *head = (InterfaceHead*)iface;
	if (!iface) return;
	assert(head->magic == MODMAN_MAGIC);
	--head->global_refcount;
}

void * GetArenaInterface(const char *id, Arena *arena)
{
	char key[MAX_ID_LEN];
	InterfaceHead *head;

	if (!arena)
		return NULL;

	snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);

	pthread_mutex_lock(&intmtx);

	head = HashGetOne(arenaints, key);
	/* if the arena doesn't have it, fall back to a global one */
	if (!head)
		head = HashGetOne(globalints, id);

	if (head)
	{
		long arena_refcount;
		snprintf(key, sizeof(key), "%p-%s", (void *)arena, arena->name);

		if (head->arena_refcounts != NULL)
		{
			/* the hash table for arena_refcounts will be undefined until an
			 * arena interface is registered or the interface is obtained once
			 * from this function, so it cannot be checked blindly */
			arena_refcount = (long)HashGetOne(head->arena_refcounts, key);
		}
		else
		{
			arena_refcount = 0;
			head->arena_refcounts = HashAlloc();
		}
		++arena_refcount;

		++head->global_refcount;
		HashReplace(head->arena_refcounts, key, (void *)arena_refcount);
	}

	pthread_mutex_unlock(&intmtx);

	return head;
}

void ReleaseArenaInterface(void *iface, Arena *arena)
{
	long arena_refcount;
	char key[MAX_ID_LEN];
	InterfaceHead *head = (InterfaceHead*)iface;
	if (!iface) return;
	assert(head->magic == MODMAN_MAGIC);

	snprintf(key, sizeof(key), "%p-%s", (void *)arena, arena->name);

	pthread_mutex_lock(&intmtx);

	arena_refcount = (long)HashGetOne(head->arena_refcounts, key);
	--arena_refcount;

	if (arena_refcount > 0)
		HashReplace(head->arena_refcounts, key, (void *)arena_refcount);
	else
		HashRemoveAny(head->arena_refcounts, key);

	--head->global_refcount;

	pthread_mutex_unlock(&intmtx);
}

/* callback stuff */

void RegCallback(const char *id, void *f, Arena *arena)
{
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashAdd(globalcallbacks, id, f);
	}
	else
	{
		char key[MAX_ID_LEN];
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashAdd(arenacallbacks, key, f);
	}
	pthread_mutex_unlock(&cbmtx);
}

void UnregCallback(const char *id, void *f, Arena *arena)
{
	pthread_mutex_lock(&cbmtx);
	if (arena == ALLARENAS)
	{
		HashRemove(globalcallbacks, id, f);
	}
	else
	{
		char key[MAX_ID_LEN];
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashRemove(arenacallbacks, key, f);
	}
	pthread_mutex_unlock(&cbmtx);
}

void LookupCallback(const char *id, Arena *arena, LinkedList *ll)
{
	LLInit(ll);
	pthread_mutex_lock(&cbmtx);
	/* first get global ones */
	HashGetAppend(globalcallbacks, id, ll);
	if (arena != ALLARENAS)
	{
		char key[MAX_ID_LEN];
		/* then append local ones */
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashGetAppend(arenacallbacks, key, ll);
	}
	pthread_mutex_unlock(&cbmtx);
}

void FreeLookupResult(LinkedList *lst)
{
	LLEmpty(lst);
}

void RegAdviser(void *adv, Arena *arena)
{
	const char *id;
	AdviserHead *head = (AdviserHead*)adv;

	assert(adv);
	assert(head->magic == ADVISER_MAGIC);

	id = head->aid;

	pthread_mutex_lock(&advmtx);

	if (arena == ALLARENAS)
		HashAdd(globaladvs, id, adv);
	else
	{
		char key[MAX_ID_LEN];
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashAdd(arenaadvs, key, adv);
	}

	pthread_mutex_unlock(&advmtx);
}

void UnregAdviser(void *adv, Arena *arena)
{
	const char *id;
	AdviserHead *head = (AdviserHead*)adv;

	assert(head->magic == ADVISER_MAGIC);

	id = head->aid;

	pthread_mutex_lock(&advmtx);

	if (arena == ALLARENAS)
		HashRemove(globaladvs, id, adv);
	else
	{
		char key[MAX_ID_LEN];
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashRemove(arenaadvs, key, adv);
	}

	pthread_mutex_unlock(&advmtx);
}

void GetAdviserList(const char *id, Arena *arena, LinkedList *list)
{
	LLInit(list);
	pthread_mutex_lock(&advmtx);
	/* first get global ones */
	HashGetAppend(globaladvs, id, list);
	if (arena != ALLARENAS)
	{
		char key[MAX_ID_LEN];
		/* then append local ones */
		snprintf(key, sizeof(key), "%p-%s", (void*)arena, id);
		HashGetAppend(arenaadvs, key, list);
	}
	pthread_mutex_unlock(&advmtx);
}

void ReleaseAdviserList(LinkedList *list)
{
	LLEmpty(list);
}

