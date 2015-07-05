
/* dist: public */

#ifndef WIN32
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#include <string.h>
#include <stdlib.h>

#include "asss.h"
#include "redirect.h"
#include "packets/redirect.h"

local Iconfig *cfg;
local Inet *net;

local int RawRedirect(const Target *t, const char *ip, int port,
		int arenatype, const char *arenaname)
{
	struct S2CRedirect pkt;
	struct in_addr addr;

	if (inet_aton(ip, &addr) == 0)
		return FALSE;

	pkt.type = S2C_REDIRECT;
	pkt.ip = ntohl(addr.s_addr);
	pkt.port = port;
	pkt.arenatype = (i16)arenatype;
	strncpy((char*)pkt.arenaname, arenaname ? arenaname : "", sizeof(pkt.arenaname));
	pkt.loginid = 0;

	net->SendToTarget(t, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);
	return TRUE;
}

local int AliasRedirect(const Target *target, const char *alias)
{
	/* cfghelp: Redirects:<name>, global, string
	 * Settings in the Redirects section correspond to arena names. If a
	 * player tries to ?go to an arena name listed in this section, they
	 * will be redirected to the zone specified as the value of the
	 * setting. The format of values is 'ip:port[:arena]'.
	 */
	char ipstr[16], portstr[16];
	const char *t = cfg->GetStr(GLOBAL, "Redirects", alias);

	/* if it's not an alias, maybe it's a literal address */
	if (!t && alias[0] >= '1' && alias[0] <= '9' &&
	    strchr(alias, '.') && strchr(alias, ':'))
		t = alias;

	if (!t) return FALSE;
	t = delimcpy(ipstr, t, sizeof(ipstr), ':');
	if (!t) return FALSE;
	t = delimcpy(portstr, t, sizeof(portstr), ':');

	return RawRedirect(target, ipstr, atoi(portstr), t ? -3 : -1, t);
}

local int ArenaRequest(Player *p, const char *arenaname)
{
	Target t;
	t.type = T_PLAYER;
	t.u.p = p;
	return AliasRedirect(&t, arenaname);
}


local Iredirect reint =
{
	INTERFACE_HEAD_INIT(I_REDIRECT, "redirect")
	AliasRedirect, RawRedirect, ArenaRequest
};

EXPORT const char info_redirect[] = CORE_MOD_INFO("redirect");

EXPORT int MM_redirect(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		if (!cfg || !net) return MM_FAIL;

		mm->RegInterface(&reint, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&reint, ALLARENAS))
			return MM_FAIL;
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	return MM_FAIL;
}

