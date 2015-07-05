
/* dist: public */

#ifndef __ENCRYPT_H
#define __ENCRYPT_H

/*
 * Iencrypt - encryption methods
 *
 * the old version of the encryption interface wasn't flexible enough to
 * handle both vie encryption, cont encryption, and anti-spoofing all at
 * the same time, without basically hardcoding everything into net. the
 * result of a weekend of thought is this new version, which is a bit
 * strange, but much more flexible.
 *
 * encryption modules should register a handler for the callback
 * CB_CONNINIT. this will be called on every single packet recieved that
 * could possibly be related to connection initialization or encryption.
 * the handler should thoroughly check the packet to be sure it has the
 * correct structure, and if so, handle it. if not, just ignore it and
 * let another handler process it.
 *
 * "handle it" can mean one of several things. a handler might want to
 * call net->ReallyRawSend to send a packet back to the original sender.
 * ReallyRawSend should be used instead of calling socket functions
 * directly in case net wants to use other transport mechanisms besides
 * udp.
 *
 * another possibility for handling an connection init type packet is to
 * actually tell net about a new connection. you do this by calling
 * net->NewConnection, and passing it a type (see defs.h), the sockaddr
 * you were passed, and your encryption interface, to be used for all
 * further communication. in return, you'll get a pid. you should store
 * whatever parameters you've negociated about the connection with this
 * pid so it will be available when it comes time to encrypt/decrypt
 * packets.
 *
 * note that there is no I_ENCRYPT identifier. that's because encryption
 * interfaces are associated with players by calling NewConnection, not
 * with GetInterface.
 */


typedef struct Iencrypt
{
	INTERFACE_HEAD_DECL

	int (*Encrypt)(Player *p, byte *pkt, int len);
	int (*Decrypt)(Player *p, byte *pkt, int len);
	/* these are obvious. the return value is the length of the
	 * resulting data. it should be encrypted/decrypted in place. */

	void (*Void)(Player *p);
	/* this is called when the player disconnects */
} Iencrypt;


/* an opaque struct holding client encryption state information */
typedef struct ClientEncryptData ClientEncryptData;

typedef struct Iclientencrypt
{
	INTERFACE_HEAD_DECL

	/* this probably isn't flexible enough for other encryption types.
	 * sorry. */
	ClientEncryptData * (*Init)(void);

	int (*Encrypt)(ClientEncryptData *ced, byte *pkt, int len);
	int (*Decrypt)(ClientEncryptData *ced, byte *pkt, int len);
	void (*Void)(ClientEncryptData *ced);
	/* this frees ced! */
} Iclientencrypt;

#endif

