
/* dist: public */

#ifndef __CONFIG_H
#define __CONFIG_H

/** @file
 * configuration manager
 *
 * modules get all of their settings from here and nowhere else. there
 * are two types of configuration files: global and arena. global ones
 * apply to the whole zone, and are stored in the conf directory of the
 * zone directory. arena ones are usually stored in arenas/arenaname,
 * but this can be customized with the search path.
 *
 * the main global configuration file is maintained internally to this
 * moudule and you don't have to open or close it. just use GLOBAL as
 * your ConfigHandle. arena configuration files are also maintained for
 * you as arena->cfg. so typically you will only need to call GetStr and
 * GetInt.
 *
 * there can also be secondary global or arena config files, specified
 * with the second parameter of OpenConfigFile. these are used for staff
 * lists and a few other special things. in general, you should use a
 * new section in the global or arena config files rather than using a
 * different file.
 *
 * setting configuration values is relatively straightforward. the info
 * parameter to SetStr and SetInt should describe who initiated the
 * change and when. this information may be written back to the
 * configuration files.
 *
 * FlushDirtyValues and CheckModifiedFiles do what they say. there's no
 * need to call them in general; the config module performs those
 * actions internally based on timers also.
 */


/* some defines for maximums */

/** the maximum length of a section name */
#define MAXSECTIONLEN 64
/** the maximum length of a key name */
#define MAXKEYLEN 64


/** other modules should manipulate config files through ConfigHandles. */
typedef struct ConfigHandle_ *ConfigHandle;

/** use this special ConfigHandle to refer to the global config file. */
#define GLOBAL ((ConfigHandle)(-3))

/** pass functions of this type to LoadConfigFile to be notified when
 ** the given file is changed. */
typedef void (*ConfigChangedFunc)(void *clos);


/** this callback is called when the global config file has been changed. */
#define CB_GLOBALCONFIGCHANGED "gconfchanged"
/** the type of the CB_GLOBALCONFIGCHANGED callback. */
typedef void (*GlobalConfigChangedFunc)(void);
/* pycb: void */


/** the config interface id */
#define I_CONFIG "config-4"

/** the config interface struct */
typedef struct Iconfig
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/** Gets a string value from a config file.
	 * @param ch the config file to use (GLOBAL for global.conf)
	 * @param section which section of the file the key is in
	 * @param key the name of the key to read
	 * @return the value of the key as a string, or NULL if the key
	 * isn't present.
	 * The return value points into memory owned by the config module.
	 * if you're planning to use it for longer than the duration of the
	 * calling function, you should make a copy yourself.
	 */
	const char * (*GetStr)(ConfigHandle ch, const char *section, const char *key);
	/* pyint: config, string, string -> string */

	/** Gets an integer value from a config file.
	 * @param ch the config file to use
	 * @param section which section of the file the key is in
	 * @param key the name of the key to read
	 * @param defvalue the value to be returned if the key isn't found
	 * @return the value of the key converted to an integer, or defvalue
	 * if it wasn't found. one special conversion is done: if the key
	 * has a string value that starts with a letter "y", then 1 is
	 * returned instead of 0.
	 */
	int (*GetInt)(ConfigHandle ch, const char *section, const char *key, int defvalue);
	/* pyint: config, string, string, int -> int */

	/** Changes a config file value.
	 * The change will show up immediately in following calls to GetStr
	 * or GetInt, and if permanent is true, it will eventually be
	 * written back to the source file. The writing back might not
	 * happen immediately, though.
	 * @param ch the config file to use
	 * @param section which section of the file the key is in
	 * @param key the name of the key to change
	 * @param value the new value of the key
	 * @param info a string describing the circumstances, for logging
	 * and auditing purposes. for example, "changed by ... with
	 * ?quickfix on may 2 2004"
	 * @param permanent whether this change should be written back to
	 * the config file.
	 */
	void (*SetStr)(ConfigHandle ch, const char *section, const char *key,
			const char *value, const char *info, int permanent);
	/* pyint: config, string, string, string, zstring, int -> void */

	/** Changes a config file value.
	 * Same as SetStr, but the new value is specified as an integer.
	 * \see Iconfig::SetStr
	 */
	void (*SetInt)(ConfigHandle ch, const char *section, const char *key,
			int value, const char *info, int permanent);
	/* pyint: config, string, string, int, zstring, int -> void */

	/** Open a new config file.
	 * This opens a new config file to be managed by the config module.
	 * You should close each file you open with CloseConfigFile. The
	 * filename to open isn't specified directly, but indirectly by
	 * providing an optional arena the file is associated with, and a
	 * filename. These elements are plugged into the config file search
	 * path to find the actual file. For example, if you want to open
	 * the file groupdef.conf in the global conf directory, you'd pass
	 * in NULL for arena and "groupdef.conf" for name. If you wanted the
	 * file "staff.conf" in arenas/foo, you'd pass in "foo" for arena
	 * and "staff.conf" for name. If name is NULL, it looks for
	 * "arena.conf" in the arena directory. If name and arena are both
	 * NULL, it looks for "global.conf" in the global conf directory.
	 *
	 * The optional callback function can call GetStr or GetInt on this
	 * (or other) config files, but it should not call any other
	 * functions in the config interface.
	 *
	 * @param arena the name of the arena to use when searching for the
	 * file
	 * @param name the name of the desired config file
	 * @param func a callback function that will get called whenever
	 * some values in the config file have changed (due to either
	 * something in the server calling SetStr or SetInt, or the file
	 * changing on disk and the server noticing)
	 * @param clos a closure argument for the callback function
	 * @return a ConfigHandle for the new file, or NULL if an error
	 * occured
	 */
	ConfigHandle (*OpenConfigFile)(const char *arena, const char *name,
			ConfigChangedFunc func, void *clos);
	/* pyint: zstring, zstring, null, null -> config */

	/** Closes a previously opened file.
	 * Don't forget to call this when you're done with a config file.
	 * @param ch the config file to close
	 */
	void (*CloseConfigFile)(ConfigHandle ch);

	/** Forces the config module to reload the file from disk.
	 * You shouldn't have to use this, as the server automatically
	 * checks config files for modifications periodically.
	 * @param ch the config file to reload
	 */
	void (*ReloadConfigFile)(ConfigHandle ch);
	/* pyint: config -> void */

	/** Adds a reference to an existing open config file.
	 * Config files are internally reference counted to support sharing.
	 * This allows you to increment the reference count without going
	 * through the trouble of opening the file again. In general you
	 * shouldn't have to use this at all, unless you're writing code to
	 * allow access to config files from another language.
	 */
	ConfigHandle (*AddRef)(ConfigHandle ch);

	/** Forces the server to write changes back to config files on disk.
	 * You shouldn't have to call this, as it's done automatically
	 * periocally.
	 */
	void (*FlushDirtyValues)(void);
	/* pyint: void -> void */

	/** Forces the server to check all open config files for
	 ** modifications since they were last loaded.
	 * You shouldn't have to call this, as it's done automatically
	 * periocally.
	 */
	void (*CheckModifiedFiles)(void);
	/* pyint: void -> void */

	/** Forces a reload of one or more config files.
	 * Pass in NULL to force a reload of all open files. Pass in a
	 * string to limit reloading to files containing that string.
	 */
	void (*ForceReload)(const char *pathname,
			void (*callback)(const char *pathname, void *clos), void *clos);
	/* pyint: string, (string, clos -> void), clos -> void */

	/** The names of the 8 ships, for use with config sections */
	const char * SHIP_NAMES[8];
} Iconfig;


#endif

