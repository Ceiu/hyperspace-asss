
/* dist: public */

#ifndef __NET_CLIENT_H
#define __NET_CLIENT_H

#include "net.h"

/* client connection stuff */
typedef struct ClientConnection ClientConnection;

typedef struct Iclientconn
{
	void (*Connected)(void);
	void (*HandlePacket)(byte *pkt, int len);
	void (*Disconnected)(void);
	/* after you get this your cc pointer is invalid */
} Iclientconn;


#define I_NET_CLIENT "net-client-2"

typedef struct Inet_client
{
	INTERFACE_HEAD_DECL

	ClientConnection *(*MakeClientConnection)(const char *addr, int port,
			Iclientconn *icc, Iclientencrypt *ice);
	/* returns null on failure. */

	void (*SendPacket)(ClientConnection *cc, byte *pkt, int len, int flags);
	/* flags are the same as for net. */

	void (*DropConnection)(ClientConnection *cc);
	/* this will _not_ call your Disconnected function. after you call
	 * this your cc pointer is invalid. */

} Inet_client;

#endif

