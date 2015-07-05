
/* this file holds various compile-time parameters to control the
 * server's behavior. */

/* pyconst: config, "CFG_*" */

/* the search path for config files */
#define CFG_CONFIG_SEARCH_PATH "arenas/%b/%n:conf/%n:%n:arenas/(default)/%n"


/* the search path for map files */
#define CFG_LVL_SEARCH_PATH "arenas/%b/%m:maps/%m:%m:arenas/%b/%b.lvl:maps/%b.lvl:arenas/(default)/%m"
#define CFG_LVZ_SEARCH_PATH "arenas/%b/%m:maps/%m:%m:arenas/(default)/%m"


/* the import path used for C modules */
#define CFG_CMOD_SEARCH_PATH "localbin:bin:corebin"


/* the import path used for python modules */
#define CFG_PYTHON_IMPORT_PATH "python:" CFG_CMOD_SEARCH_PATH


/* whether to log private and chat messages */
/* #define CFG_LOG_PRIVATE */


/* whether to disallow allow loading modules from anywhere other than ./bin/ */
/* #define CFG_RESTRICT_MODULE_PATH */


/* whether to disallow module loading after the server has been initalized */
/* #define CFG_NO_RUNTIME_LOAD */


/* the format for printing time in log files, in strftime format. this
 * one looks like "Mar 26 13:33:16". */
#define CFG_TIMEFORMAT "%b %d %H:%M:%S"


/* whether to include uname info in the ?version output */
#define CFG_EXTRA_VERSION_INFO


/* whether to scan the arenas directory for ?arena all */
#define CFG_DO_EXTRAARENAS


/* if this is defined and the capability mananger isn't loaded, all
 * commands will be allowed. if it's not defined, _no_ commands will be
 * allowed. that's probably not what you want, unless you're very
 * paranoid. */
#define CFG_ALLOW_ALL_IF_CAPMAN_IS_MISSING


/* the maximum value for Team:DesiredTeams */
#define CFG_MAX_DESIRED 10


/* maximum length of a line in a config file */
#define CFG_MAX_LINE 4096


/* maximum size of a "big packet" */
#define CFG_MAX_BIG_PACKET 65536


/* maximum length of module-defined persistent data */
#define CFG_MAX_PERSIST_LENGTH 4096


/* number of lines to hold in memory for ?lastlog, and the number of
 * characters of each line to store. */
#define CFG_LAST_LINES 640
#define CFG_LAST_LENGTH 192


/* relax checks on certain packet lengths. only useful for debugging. */
/* #define CFG_RELAX_LENGTH_CHECKS */


/* maximum paramaters for the soccer game */
#define CFG_SOCCER_MAXFREQ 8
#define CFG_SOCCER_MAXGOALS 16


/* number of buckets, and size of each bucket, for lag measurement. note
 * that the bucket width is in milliseconds, not ticks. */
#define CFG_LAG_BUCKETS 25
#define CFG_LAG_BUCKET_WIDTH 20


/* whether to set the SO_REUSEADDR option on tcp sockets (specifically
 * the chatnet server socket). this makes restarting the server always
 * work. */
#define CFG_SET_REUSEADDR


/* whether to keep a list of free links. this is an optimization that
 * will have different effects on different systems. enabling it will
 * probably decrease memory use a bit, and might make things faster or
 * slower, depending on your system and malloc implementation. */
/* #define CFG_USE_FREE_LINK_LIST */


/* whether to enable a few locks for unimportant data that may decrease
 * performance (and increase lag), but are relatively safe to leave off.
 * you might want to consider enabling this on a multiprocessor box. */
/* #define CFG_PEDANTIC_LOCKING */


/* the default spec freq, if no settings exist. don't change this. */
#define CFG_DEF_SPEC_FREQ 8025


/* the file to write asss' pid to when it starts up. set to NULL to
 * disable. */
#define CFG_PID_FILE "asss.pid"


/* whether the unixsignal module should trap segfaults and attempt to
 * generate a backtrace. set this to 1 to use glibc's internal backtrace
 * functionality, to 2 to invoke gdb. */
#define CFG_HANDLE_SEGV 2


/* how many threads there are in the general-purpose thread pool. */
#define CFG_THREAD_POOL_WORKER_THREADS 3


/* pyconst: config end */
/* dist: public */

