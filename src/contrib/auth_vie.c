
/* dist: public */

/* this module authenticates bots based on username and IP range.
 * the global.conf file should contain a section like this:

[VIEnames]

SnrrrubSpace = any
pub0bot = 127.0.0.1
Probe1 = 65.72.

 * if the key has the value "any" then any IP address can be used,
 * otherwise the key is part or all of a dotted-quad IPv4 address.
 */

#include <string.h>
#include <stdio.h>

#include "asss.h"

local Imodman *mm;
local Iauth *oldauth;
local Iplayerdata *pd;
local Iconfig *cfg;
local Ilogman *lm;


local void authenticate(Player *p, struct LoginPacket *lp, int lplen,
		void (*done)(Player *p, AuthData *data))
{
	if (p->type == T_VIE)
	{
		const char *ip;
		char name[32];

		/* copy to local storage in case it's not null terminated */
		astrncpy(name, lp->name, sizeof(name));

		ip = cfg->GetStr(GLOBAL, "VIEnames", name);

		if (!ip || (strstr(p->ipaddr, ip) != p->ipaddr &&
		            strcasecmp(ip, "any") != 0))
		{
			AuthData ad;

			memset(&ad, 0, sizeof(ad));
			ad.authenticated = FALSE;
			ad.code = AUTH_NOPERMISSION2;
			astrncpy(ad.name, name, sizeof(ad.name));
			astrncpy(ad.sendname, name, sizeof(ad.sendname));

			done(p, &ad);

			if (lm) lm->Log(L_MALICIOUS,
					"<auth_vie> blocked player [%s] from %s",
					lp->name, p->ipaddr);

		}
		else
			oldauth->Authenticate(p, lp, lplen, done);
	}
	else
		oldauth->Authenticate(p, lp, lplen, done);
}


local Iauth myauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "auth-vie")
	authenticate
};


EXPORT int MM_auth_vie(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);

		if (!oldauth || !pd || !cfg) return MM_FAIL;

		mm->RegInterface(&myauth, ALLARENAS);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;
		mm->ReleaseInterface(oldauth);
		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		return MM_OK;
	}
	return MM_FAIL;
}

