
/* dist: public */

#ifndef __NET_H
#define __NET_H

/*
 * Inet - network stuff
 *
 * need more docs here
 *
 * the player will be locked during the duration of the packet function.
 * so you don't have to do it yourself. you do have to do it if you're
 * modifying the player struct not in a packet handler, or if you're
 * modifying other players in another player's handler.
 *
 */


/* included for struct sockaddr_in */
#ifndef WIN32
#include <netinet/in.h>
#endif


/* important constants */
#define MAXPACKET 512
#define MAXBIGPACKET CFG_MAX_BIG_PACKET

/* bits in the flags parameter to the SendX functions */
#define NET_UNRELIABLE  0x00
#define NET_RELIABLE    0x01
#define NET_DROPPABLE   0x02
#define NET_URGENT      0x04

/* priority levels. 4 and 5 include urgent. */
#define NET_PRI_N1      0x10
#define NET_PRI_DEF     0x20
#define NET_PRI_P1      0x30
#define NET_PRI_P2      0x40
#define NET_PRI_P3      0x50
#define NET_PRI_P4      0x64
#define NET_PRI_P5      0x74


typedef void (*PacketFunc)(Player *p, byte *data, int length);
typedef void (*SizedPacketFunc)
	(Player *p, byte *data, int len, int offset, int totallen);

typedef void (*RelCallback)(Player *p, int success, void *clos);

#define CB_CONNINIT "conninit"
typedef void (*ConnectionInitFunc)(struct sockaddr_in *sin, byte *pkt, int len, void *v);


struct net_stats
{
	unsigned int pcountpings, pktsent, pktrecvd;
	unsigned int bytesent, byterecvd;
	unsigned int buffercount, buffersused;
#define NET_GROUPED_STATS_LEN 8
	unsigned int grouped_stats[NET_GROUPED_STATS_LEN];
#define NET_PRI_STATS_LEN 5 /* must match BW_PRIS in bwlimit.h */
	unsigned int pri_stats[NET_PRI_STATS_LEN];
	byte reserved[176];
};

struct net_client_stats
{
	/* sequence numbers */
	i32 s2cn, c2sn;
	/* counts of stuff sent and recvd */
	unsigned int pktsent, pktrecvd, bytesent, byterecvd;
	/* count of s2c packets dropped */
	unsigned int pktdropped;
	unsigned int unused1;
	/* encryption type */
	const char *encname;
	/* ip info */
	char ipaddr[16];
	unsigned short port;
	/* bandwidth limit info */
	char bwlimitinfo[32];
	byte reserved[170];
};


#include "encrypt.h"


#define I_NET "net-11"

typedef struct Inet
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/**
	 * Impl Note:
	 * The callback functionality provided by the *WithCallback functions only operates if the packet
	 * is sent reliably. If a callback is provided, but the NET_RELIABLE flag is absent, the callback
	 * will be silently ignored.
	 */

	void (*SendToOne)(Player *p, byte *data, int length, int flags);
	void (*SendToOneWithCallback)(Player *p, byte *data, int length, int flags, RelCallback callback, void *clos);

	void (*SendToArena)(Arena *a, Player *except, byte *data, int length, int flags);
	void (*SendToArenaWithCallback)(Arena *a, Player *except, byte *data, int length, int flags, RelCallback callback, void *clos);

	void (*SendToSet)(LinkedList *set, byte *data, int length, int flags);
	void (*SendToSetWithCallback)(LinkedList *set, byte *data, int length, int flags, RelCallback callback, void *clos);

	void (*SendToTarget)(const Target *target, byte *data, int length, int flags);
	void (*SendToTargetWithCallback)(const Target *target, byte *data, int length, int flags, RelCallback callback, void *clos);


	int (*SendSized)(Player *p, void *clos, int len,
			void (*request_data)(void *clos, int offset, byte *buf, int needed));

	void (*AddPacket)(int pktype, PacketFunc func);
	void (*RemovePacket)(int pktype, PacketFunc func);
	void (*AddSizedPacket)(int pktype, SizedPacketFunc func);
	void (*RemoveSizedPacket)(int pktype, SizedPacketFunc func);

	/* only to be used by encryption modules! */
	void (*ReallyRawSend)(struct sockaddr_in *sin, byte *pkt, int len, void *v);
	Player * (*NewConnection)(int type, struct sockaddr_in *sin, Iencrypt *enc, void *v);

	void (*GetStats)(struct net_stats *stats);
	void (*GetClientStats)(Player *p, struct net_client_stats *stats);
	int (*GetLastPacketTime)(Player *p);

	int (*GetListenData)(unsigned index, int *port, char *connectasbuf, int buflen);
	/* returns true if it returned stuff, false if bad index */
	/* pyint: int, int out, string out, int buflen -> int */
	int (*GetLDPopulation)(const char *connectas);
	/* pyint: string -> int */
} Inet;


#endif

