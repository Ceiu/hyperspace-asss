
/* dist: public */

#ifndef __CHATNET_H
#define __CHATNET_H


typedef void (*MessageFunc)(Player *p, const char *line);
/* a func of this type is responsible for one message type. the line
 * passed in will be the line sent by the client, minus the message type
 * field. */


struct chat_client_stats
{
	/* ip info */
	char ipaddr[16];
	unsigned short port;
};


#define I_CHATNET "chatnet-3"

typedef struct Ichatnet
{
	INTERFACE_HEAD_DECL

	void (*AddHandler)(const char *type, MessageFunc func);
	void (*RemoveHandler)(const char *type, MessageFunc func);

	void (*SendToOne)(Player *p, const char *line, ...)
		ATTR_FORMAT(printf, 2, 3);
	void (*SendToArena)(Arena *a, Player *except, const char *line, ...)
		ATTR_FORMAT(printf, 3, 4);
	void (*SendToSet)(LinkedList *set, const char *line, ...)
		ATTR_FORMAT(printf, 2, 3);

	void (*GetClientStats)(Player *p, struct chat_client_stats *stats);
} Ichatnet;

#endif

