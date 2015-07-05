
/* dist: public */

#ifndef __CMDMAN_H
#define __CMDMAN_H

/** @file
 * the command manager; deals with registering and dispatching commands.
 *
 * command handlers come in two flavors, which differ only in whether
 * the handler gets to see the command name that was used. this can only
 * make a difference if the same handler is used for multiple commands,
 * of course. also, if you want to register per-arena commands, you need
 * to use the second flavor. all handlers may assume p and p->arena are
 * non-NULL.
 *
 * Target structs are used to describe how a command was issued.
 * commands typed as public chat messages get targets of type T_ARENA.
 * commands typed as local private messages or remove private messages
 * to another player on the same server get T_PLAYER targets, and
 * commands sent as team messages (to your own team or an enemy team)
 * get T_FREQ targets.
 *
 * there is no difference between ? commands and * commands. all
 * commands (except of course commands handled on the client) work
 * equally whether a ? or a * was used.
 *
 * help text should follow a standard format:
 * @code
 * local helptext_t help_foo =
 * "Module: ...\n"
 * "Targets: ...\n"
 * "Args: ...\n"
 * More text here...\n";
 * @endcode
 *
 * the server keeps track of a "default" command handler, which will get
 * called if no commands know to the server match a typed command. to
 * set or remove the default handler, pass NULL as cmdname to any of the
 * Add/RemoveCommand functions. this feature should only be used by
 * billing server modules.
 */


/** the type of command handlers.
 * @param command the name of the command that was issued
 * @param params the stuff that the player typed after the command name
 * @param p the player issuing the command
 * @param target describes how the command was issued (public, private,
 * etc.)
 */
typedef void (*CommandFunc)(const char *command, const char *params,
		Player *p, const Target *target);


/** a type representing the help text strings that get registered with
 ** the command manager. */
typedef const char *helptext_t;

/** the interface id for Icmdman */
#define I_CMDMAN "cmdman-10"

/** the interface struct for Icmdman */
typedef struct Icmdman
{
	INTERFACE_HEAD_DECL

	/** Registers a command handler.
	 * Be sure to use RemoveCommand to unregister this before unloading.
	 * @param cmdname the name of the command being registered
	 * @param func the handler function
	 * @param ht some help text for this command, or NULL for none
	 */
	void (*AddCommand)(const char *cmdname, CommandFunc func, Arena *arena, helptext_t ht);

	/** Unregisters a command handler.
	 * Use this to unregister handlers registered with AddCommand.
	 */
	void (*RemoveCommand)(const char *cmdname, CommandFunc func, Arena *arena);

	/** Dispatches an incoming command.
	 * This is generally only called by the chat module and billing
	 * server modules.
	 * If the first character of typedline is a backslash, command
	 * handlers in the server will be bypassed and the command will be
	 * passed directly to the default handler.
	 * @param typedline the thing that the player typed
	 * @param p the player who issued the command
	 * @param target how the command was issued
	 * @param sound the sound from the chat packet that this command
	 * came from
	 */
	void (*Command)(const char *typedline, Player *p,
			const Target *target, int sound);
	helptext_t (*GetHelpText)(const char *cmdname, Arena *arena);

	/** Prevents a command's parameters from being logged.
	 * The command should be removed with RemoveUnlogged when the
	 * command is unregistered.
	 * @param cmdname the name of the command to not log
	 */
	void (*AddUnlogged)(const char *cmdname);

	/** Removes a command added with AddDontLog
	 * @param cmdname the name of the unlogged command to remove
	 */
	void (*RemoveUnlogged)(const char *cmdname);
} Icmdman;

#endif

