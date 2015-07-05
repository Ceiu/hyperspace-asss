
/* dist: public */

#ifndef __CHAT_H
#define __CHAT_H

/** @file
 * Ichat and related declaraions. stuff to do with chat messages.
 */

/* types of chat messages */
/* pyconst: define int, "MSG_*" */
#define MSG_ARENA        0   /**< arena messages (in green) */
#define MSG_PUBMACRO     1   /**< macros as public arena chat */
#define MSG_PUB          2   /**< public arena chat */
#define MSG_FREQ         3   /**< team message */
#define MSG_TEAM         3   /**< an alias for MSG_FREQ */
#define MSG_NMEFREQ      4   /**< enemy team messages */
#define MSG_PRIV         5   /**< within-arena private messages */
#define MSG_REMOTEPRIV   7   /**< cross-arena or cross-zone private messages */
#define MSG_SYSOPWARNING 8   /**< red sysop warning text */
#define MSG_CHAT         9   /**< chat channel messages */
#define MSG_FUSCHIA      79  /**< special chat code that displays pink/purple (continuum only) */
/* the following are for internal use only. they never appear in packets
 * sent over the network. */
#define MSG_MODCHAT      10  /**< moderator chat messages (internal only) */
#define MSG_COMMAND      11  /**< msgs that function as commands (internal only) */
#define MSG_BCOMMAND     12  /**< commands that go to the biller (internal only) */


/** this callback is called when most types of chat messages pass
 ** through the server. */
#define CB_CHATMSG "chatmsg"
/** the type of CB_CHATMSG callbacks.
 * @param p the player initiating the chat message
 * @param type the type of message (MSG_FOO)
 * @param sound an optional associated sound. zero means none.
 * @param target the target of a private message. only valid when type
 * is MSG_PRIV or MSG_REMOTEPRIV.
 * @param freq the target frequency. only valid when type is MSG_FREQ or
 * MSG_NMEFREQ.
 * @param text the text of the message
 */
typedef void (*ChatMsgFunc)(Player *p, int type, int sound, Player *target,
		int freq, const char *text);
/* pycb: player, int, int, player, int, string */


/* this isn't for general use */
#define CB_REWRITECOMMAND "rewritecommand"
typedef void (*CommandRewriterFunc)(int initial, char *buf, int len);


/** this type is used to represent chat masks, which can be used to
 ** restrict certain types of chat per-player or per-arena. */
typedef unsigned short chat_mask_t;
/** checks if the given chat type is allowed by the given mask */
#define IS_RESTRICTED(mask, type) ((mask) & (1<<(type)))
/** checks if the given chat type is restricted by the given mask */
#define IS_ALLOWED(mask, type) (!IS_RESTRICTED(mask, type))
/** sets the bit to restrict the given chat type in the given mask */
#define SET_RESTRICTED(mask, type) (mask) |= (1<<(type))
/** sets the bit to allow the given chat type in the given mask */
#define SET_ALLOWED(mask, type) (mask) &= ~(1<<(type))


/** the interface id for Ichat */
#define I_CHAT "chat-7"

/** the interface struct for Ichat.
 * most of these functions take a printf-style format string plus
 * variable arguments.
 */
typedef struct Ichat
{
	INTERFACE_HEAD_DECL
	/* pyint: use */
	/* note that things involving player sets (lists) aren't supported yet. */

	/** Send a green arena message to a player. */
	void (*SendMessage)(Player *p, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: player, formatted -> void */

	/** Sends a command response to a player.
	 * For Continuum clients, this is the same as an arena message, but
	 * other clients might interpret them differently.
	 */
	void (*SendCmdMessage)(Player *p, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: player, formatted -> void */

	/** Sends a green arena message to a set of players. */
	void (*SendSetMessage)(LinkedList *set, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: playerlist, formatted -> void */

	/** Sends a green arena message plus sound code to a player. */
	void (*SendSoundMessage)(Player *p, char sound, const char *format, ...)
		ATTR_FORMAT(printf, 3, 4);
	/* pyint: player, int, formatted -> void */

	/** Sends a green arena message plus sound code to a set of players. */
	void (*SendSetSoundMessage)(LinkedList *set, char sound, const char *format, ...)
		ATTR_FORMAT(printf, 3, 4);
	/* pyint: playerlist, int, formatted -> void */

	/** Sends an arbitrary chat message to a set of players. */
	void (*SendAnyMessage)(LinkedList *set, char type, char sound,
			Player *from, const char *format, ...)
		ATTR_FORMAT(printf, 5, 6);
	/* pyint: playerlist, int, int, player, formatted -> void */

	/** Sends a green arena message to all players in an arena.
	 * Use ALLARENAS for areana to send to all players in all arenas. */
	void (*SendArenaMessage)(Arena *arena, const char *format, ...)
		ATTR_FORMAT(printf, 2, 3);
	/* pyint: arena, formatted -> void */

	/** Sends a green arena message plus sound code to all players in an
	 ** arena. */
	void (*SendArenaSoundMessage)(Arena *arena, char sound, const char *format, ...)
		ATTR_FORMAT(printf, 3, 4);
	/* pyint: arena, int, formatted -> void */

	/** Sends a moderator chat message to all connected staff. */
	void (*SendModMessage)(const char *format, ...)
		ATTR_FORMAT(printf, 1, 2);
	/* pyint: formatted -> void */

	/** Sends a remove private message to a set of players.
	 * This should only be used from billing server modules. */
	void (*SendRemotePrivMessage)(LinkedList *set, int sound, const char
			*squad, const char *sender, const char *msg);


	/** Retrives the chat mask for an arena. */
	chat_mask_t (*GetArenaChatMask)(Arena *arena);
	/* pyint: arena -> int */

	/** Sets the chat mask for an arena. */
	void (*SetArenaChatMask)(Arena *arena, chat_mask_t mask);
	/* pyint: arena, int -> void */

	/** Retrives the chat mask for a player. */
	chat_mask_t (*GetPlayerChatMask)(Player *p);
	/* pyint: player -> int */

	/** Retrievs the remaining time (in seconds) on the chat mask */
	int (*GetPlayerChatMaskTime)(Player *p);
	/* pyint: player -> int */

	/** Sets the chat mask for a player.
	 * @param p the player whose mask to modify
	 * @param mask the new chat mask
	 * @param timeout zero to set a session mask (valid until the next
	 * arena change), or a number of seconds for the mask to be valid
	 */
	void (*SetPlayerChatMask)(Player *p, chat_mask_t mask, int timeout);
	/* pyint: player, int, int -> void */

	/** A utility function for sending lists of items in a chat message. */
	void (*SendWrappedText)(Player *p, const char *text);
} Ichat;


#endif

