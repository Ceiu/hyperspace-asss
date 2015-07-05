
/* dist: public */

#ifndef __REDIRECT_H
#define __REDIRECT_H

#define I_REDIRECT "redirect-2"

typedef struct Iredirect
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	int (*AliasRedirect)(const Target *t, const char *dest);
	/* pyint: target, string -> int */
	int (*RawRedirect)(const Target *t, const char *ip, int port,
			int arenatype, const char *arenaname);
	/* pyint: target, string, int, int, string -> int */
	int (*ArenaRequest)(Player *p, const char *arenaname);
	/* pyint: player, string -> int */
} Iredirect;

#endif

