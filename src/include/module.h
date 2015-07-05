
/* dist: public */

#ifndef __MODMAN_H
#define __MODMAN_H

/** @file
 * describes the module manager interface, which handles modules,
 * interfaces, and callbacks.
 */

struct Imodman;

/** module load/unload operations.
 * these codes are passed to module entry points to describe which
 * operation is being requested. */
enum
{
	/* pyconst: enum, "MM_*" */

	/** the module is being loaded.
	 * do all global initialization here.
	 */
	MM_LOAD,

	/** a second initialization phase that allows modules to obtain
	 ** references to interfaces exported by modules loaded after them.
	 * interfaces obtained in MM_POSTLOAD should be released in
	 * MM_PREUNLOAD, so that module unloading can proceed cleanly.
	 */
	MM_POSTLOAD,

	/** this stage is for cleaning up any activity done in MM_POSTLOAD.
	 */
	MM_PREUNLOAD,

	/** the module is being unloaded.
	 * clean up everything you did in MM_LOAD.
	 */
	MM_UNLOAD,

	/** the module is being attached to an arena.
	 * if you have any arena-specific functionality, now would be a good
	 * time to turn it on for this arena.
	 */
	MM_ATTACH,

	/** the reverse of MM_ATTACH.
	 * disable any special functionality for this arena.
	 */
	MM_DETACH
};


/* return values for module functions, and also some other things. */
/* pyconst: define int, "MM_*" */
#define MM_OK     0  /**< success */
#define MM_FAIL   1  /**< failure */

/** the maximum length of a callback, interface or adviser id. */
#define MAX_ID_LEN 128

/** all interfaces declarations MUST start with this macro */
#define INTERFACE_HEAD_DECL struct InterfaceHead head;

/** and all interface initializers must start with this or the next macro.
 * @param iid the interface id of this interface
 * @param name a unique name for the implementation of the interface */
#define INTERFACE_HEAD_INIT(iid, name) { MODMAN_MAGIC, iid, name, NULL, 0, 0, {0, 0, 0, 0} },

/** this struct appears at the head of each interface implementation declaration.
 * you shouldn't ever use it directly, but it's necessary for the above
 * macros (INTERFACE_HEAD_DECL, INTERFACE_HEAD_INIT). */
typedef struct InterfaceHead
{
	unsigned long magic;
	const char *iid, *name;
	HashTable *arena_refcounts;
	int global_refcount;
	int arena_registrations;
	long _reserved[4];
} InterfaceHead;


/** all interfaces declarations MUST start with this macro */
#define ADVISER_HEAD_DECL struct AdviserHead head;

/** and all interface initializers must start with this or the next macro.
 * @param aid the adviser id of this adviser */
#define ADVISER_HEAD_INIT(aid) { ADVISER_MAGIC, aid },

/** this struct appears at the head of each adviser implementation declaration.
 * you shouldn't ever use it directly, but it's necessary for the above
 * macros (INTERFACE_HEAD_DECL, INTERFACE_HEAD_INIT). */
typedef struct AdviserHead
{
	unsigned long magic;
	const char *aid;
} AdviserHead;

/** a magic value to distinguish adviser pointers */
#define ADVISER_MAGIC 0xADB15345

/** a magic value to distinguish interface pointers */
#define MODMAN_MAGIC 0x46692018


typedef struct mod_args_t
{
	char name[32];
	const char *info;
	void *privdata;
} mod_args_t;

/** the type of a module loader handler.
 * different types of module loaders can exist in the server. the C
 * module loader is required, and a Python module loader is also
 * included. you can register a module loader with RegModuleLoader.
 *
 * this will be called when loading a module. action is:
 * MM_LOAD - requesting to load a module. line will be set. fill in
 * args. ignore arena. return MM_OK/FAIL
 * MM_UNLOAD - requesting to unload. ignore line. args will be set.
 * return MM_OK/FAIL.
 * MM_ATTACH - requesting to attach. args and arena will be set. ignore
 * line.
 * MM_DETACH - requesting to detach. args and arena will be set. ignore
 * line.
 * MM_POSTLOAD, MM_PREUNLOAD - two more phases of initialization. don't
 * worry about these.
 *
 * line, when it is set, is a module specifier (from modules.conf or
 * ?insmod).
 * all of the stuff in args is for the module loader's use, although
 * name and info will be used by the module manager.
 */
typedef int (*ModuleLoaderFunc)(int action, mod_args_t *args, const char *line, Arena *arena);


/** Use this in some of the following functions to refer to make things
 ** global instead of specific to an arena. */
#define ALLARENAS NULL


/** this is only used from python */
#define I_MODMAN "modman-3"

/** the module manager interface struct */
typedef struct Imodman
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/* module stuff */

	/** Load a module.
	 * The specifier is of the form "<loader>file:modname". loader
	 * describes the module loader to use, and is optional. if omitted,
	 * the C loader is used. file is the filename (without the
	 * .so/.dll/.py), and is also optional. if omitted (omit the colon
	 * too), the module is assumed to be linked into the asss binary.
	 */
	int (*LoadModule)(const char *specifier);
	/* pyint: string -> int */

	/** Unloads a module.
	 * Only the name should be given (not the loader or filename).
	 */
	int (*UnloadModule)(const char *name);
	/* pyint: string -> int */

	/** Calls the supplied callback function for each loaded module,
	 ** passing it the module name and extra info, and a closure pointer
	 ** for it to use.
	 * If attachedfilter is not NULL, only modules attached to the
	 * specified arena are returned.
	 */
	void (*EnumModules)(void (*func)(const char *name, const char *info,
				void *clos), void *clos, Arena *attachedfilter);
	/* pyint: (string, string, clos -> void), clos, arena -> void */

	/** Attaches a module to an arena.
	 * This is called by the arena manager at the proper stage of arena
	 * loading, and occasionally while the arena is running also.
	 */
	int (*AttachModule)(const char *modname, Arena *arena);
	/* pyint: string, arena -> int */
	/** Detaches a module from an arena.
	 * This is called by the arena manager at the proper stage of arena
	 * loading, and occasionally while the arena is running also.
	 */
	int (*DetachModule)(const char *modname, Arena *arena);
	/* pyint: string, arena -> int */


	/* interface stuff */

	/** Registers an implementation of an interface to expose to other
	 ** modules.
	 * @param iface a pointer to the interface structure to register.
	 * because of the macros like INTERFACE_HEAD_INIT, the interface id
	 * and other relevent information will be included in the interface
	 * struct itself.
	 * @param arena the arena to register the interface for, or
	 * ALLARENAS to make it globally visible.
	 */
	void (*RegInterface)(void *iface, Arena *arena);
	/** Unregisters an implementation of an interface.
	 * This should be called once for each call to RegInterface, with
	 * the same parameters. It will refuse to register an interface if
	 * its reference count indicates that it's still being used by other
	 * modules.
	 * @param iface a pointer to the interface structure
	 * @param arena the arena to unregister the interface for, or
	 * ALLARENAS to unregister it globally. note that if arena was
	 * specified to RegInterface, it must be specified to UnregInterface
	 * too; using ALLARENAS doesn't unregister arena-specific
	 * interfaces.
	 * @return the reference count of the interface struct, so zero
	 * means success.
	 */
	int (*UnregInterface)(void *iface, Arena *arena);

	/** Retrieves an interface pointer.
	 * @param id the interface id for the desired interface
	 * @param arena the arena to use when looking for registered
	 * implementations, or ALLARENAS to use only globally registered
	 * implementations.
	 */
	void * (*GetInterface)(const char *id, Arena *arena);
	/** Retrieves an interface pointer, by implementation name.
	 * Rather than getting any implementation that's registered for some
	 * interface id, sometimes you need to get a specific implementation
	 * by name. the name is the second parameter to INTERFACE_HEAD_INIT.
	 * @param name the name of the implementation of the interface
	 * pointer to get
	 */
	void * (*GetInterfaceByName)(const char *name);

	/** Release an interface pointer.
	 * This decrements the reference count in the interface struct. You
	 * need to do this once for each GetInterface/ByName you do.
	 */
	void (*ReleaseInterface)(void *iface);

	/** Retrieves an interface pointer for an arena. Increments the reference
	 * count for the interface on the arena, unlike GetInterface, which will
	 * only increment the global reference count. */
	void * (*GetArenaInterface)(const char *id, Arena *arena);

	/** Releases an arena interface. Properly releases the arena reference
	 * count, unlike ReleaseInterface. */
	void (*ReleaseArenaInterface)(void *iface, Arena *arena);

	/* callback stuff.
	 * these manage callback functions. putting this functionality in
	 * here keeps every other module that wants to call callbacks from
	 * implementing it themselves. */

	/** Registers a function as a callback handler.
	 * Be careful with the type of the function. Because this is a
	 * generic function, no type checking can be done.
	 * @param id the callback id to register it for (CB_BLAH)
	 * @param func the handler to register
	 * @param arena the arena to register it for, or ALLARENAS to
	 * register it globally
	 */
	void (*RegCallback)(const char *id, void *func, Arena *arena);
	/** Unregisters a function as a callback handler.
	 * You should call this with the same arguments as RegCallback when
	 * your module is detaching or unloading.
	 */
	void (*UnregCallback)(const char *id, void *func, Arena *arena);

	/** Returns a list of currently registered handlers for a type of
	 ** callback and arena.
	 * If arena is set, returns all callbacks registered for that arena,
	 * and also all registered globally. If arena is ALLARENAS, returns
	 * only callbacks registered globally.
	 * Don't use this directly, use the DO_CBS macro.
	 * @see DO_CBS
	 */
	void (*LookupCallback)(const char *id, Arena *arena, LinkedList *res);
	/** Frees a callback list result from LookupCallback.
	 * @see DO_CBS
	 */
	void (*FreeLookupResult)(LinkedList *res);

	/* adviser stuff */

	/** Registers an adviser for other modules to discover.
	 * Advisers are a cross between interfaces and callbacks, since they
	 * are declared in a similar fashion to interfaces, but multiple 
	 * advisers can be registered by different modules (like callbacks).
	 * @param adv a pointer to the adviser to register
	 * @param arena the arena to register the adviser in, or ALLARENAS
	 */
	void (*RegAdviser)(void *adv, Arena *arena);
	/** Unregisters an adviser registered with RegAdviser.
	 * @param adv the pointer passed to RegAdviser
	 * @param arena the arena that the adviser was registered in
	 */
	void (*UnregAdviser)(void *adv, Arena *arena);
	/** Populates a LinkedList with currently registered advisers.
	 * If arena is set, then this will return all advisers registered
	 * for that arena, along with globally registered advisers. If the
	 * arena is ALLARENAS, then this will return only globally 
	 * registered advisers. The list must be passed to ReleaseAdviserList 
	 * when finished. 
	 * NOTE: unlike interfaces, advisers may have NULL functions pointers.
	 * @param id the id of the desired advisers
	 * @param arena the arena of the desired advisers, or ALLARENAS for
	 * only globally registered advisers
	 * @param list the LinkedList to fill with advisers
	 */
	void (*GetAdviserList)(const char *id, Arena *arena, LinkedList *list);
	/** Frees the contents of the list used with GetAdviserList.
	 * @param list the LinkedList passed to GetAdviserList
	 */
	void (*ReleaseAdviserList)(LinkedList *list);

	/* module loaders */

	/** Registers a new module loader. */
	void (*RegModuleLoader)(const char *signature, ModuleLoaderFunc func);
	/** Unregisters a module loader. */
	void (*UnregModuleLoader)(const char *signature, ModuleLoaderFunc func);

	/* more module stuff */

	/** Gets the extra info for a module.
	 * This belongs up there with the other module functions, but it was
	 * added later and goes down here to avoid breaking binary
	 * compatibility.
	 */
	const char *(*GetModuleInfo)(const char *modname);
	/* pyint: string -> string */

	/** Gets the name of the loader for a module. */
	const char *(*GetModuleLoader)(const char *modname);

	/** Detaches modules in reverse order, until one fails.
	 * Returns MM_OK if all modules detached okay, returns MM_FAIL if any
	 * module failed to detach. */
	int (*DetachAllFromArena)(Arena *arena);
	
	/* these functions should be called only from main.c */
	struct
	{
		void (*DoStage)(int stage);
		void (*UnloadAllModules)(void);
		void (*NoMoreModules)(void);
	} frommain;
	
	void *_reserved[8];
} Imodman;


/** The entry point to the module manager.
 * Only main should call this. */
Imodman * InitModuleManager(void);

/** Deinitializes the module manager.
 * Only main should call this. */
void DeInitModuleManager(Imodman *mm);



/** Calls all registered handlers of some type.
 * You should use this macro to invoke callback functions, instead of
 * using LookupCallback yourself. Here's an example from flags.c:
 * @code
 *   DO_CBS(CB_FLAGWIN, arena, FlagWinFunc, (arena, freq));
 * @endcode
 * @param cb the callback id to call (CB_BLAH)
 * @param arena the arena to lookup handlers in, or ALLARENAS to call
 * only globally registered handlers. if arena is not ALLARENAS,
 * handlers registered for that arena, and also ones registered
 * globally, will be called. if arena is ALLARENAS, only handlers
 * registered globally will be called.
 * @param type the type representing the callback handler signature
 * (BlahFunc)
 * @param args the arguments to the callback handler, enclosed in an
 * extra set of parenthesis
 */
#define DO_CBS(cb, arena, type, args)        \
do {                                         \
	LinkedList _a_lst;                       \
	Link *_a_l;                              \
	mm->LookupCallback(cb, arena, &_a_lst);  \
	for (_a_l = LLGetHead(&_a_lst); _a_l; _a_l = _a_l->next)  \
		((type)_a_l->data) args ;            \
	mm->FreeLookupResult(&_a_lst);           \
} while (0)

#endif

