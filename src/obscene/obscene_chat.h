
/* dist: public */

#ifndef __OBSCENE_CHAT_H
#define __OBSCENE_CHAT_H

/** this callback is called when most types of chat messages pass
 ** through the server. */
#define CB_FILTEREDMSG "filteredmsg"
/** the type of CB_CHATMSG callbacks.
 * @param p the player initiating the chat message
 * @param type the type of message (MSG_FOO)
 * @param sound an optional associated sound. zero means none.
 * @param target the target of a private message. only valid when type
 * is MSG_PRIV or MSG_REMOTEPRIV.
 * @param freq the target frequency. only valid when type is MSG_FREQ or
 * MSG_NMEFREQ.
 * @param text the original text before filtering.
 */
typedef void (*FilteredMsgFunc)(Player *p, int type, int sound, Player *target,
		int freq, const char *text);


#endif

