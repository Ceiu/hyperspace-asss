
/* dist: public */

#include <string.h>

#include "asss.h"


local Imodman *mm;
local Iauth *oldauth;
local Icapman *capman;
local Iplayerdata *pd;
local int prefixkey;
#define PREFIX(p) (*(char*)PPDATA(p, prefixkey))
local void (*CachedDone)(Player *p, AuthData *data);


local void MyDone(Player *p, AuthData *data)
{
	AuthData mydata;

	memcpy(&mydata, data, sizeof(mydata));
	if (PREFIX(p))
	{
		/* we have a prefix. check for the appropriate capability */
		char cap[] = "prefix_@";
		strchr(cap, '@')[0] = PREFIX(p);
		if (capman->HasCapabilityByName(mydata.name, cap))
		{
			/* only add back the letter if he has the capability, and
			 * only add it to the sendname. */
			memmove(mydata.sendname+1, mydata.sendname, 19);
			mydata.sendname[0] = PREFIX(p);
		}
	}

	CachedDone(p, &mydata);
}


local void Authenticate(Player *p, struct LoginPacket *lp, int lplen,
		void (*Done)(Player *p, AuthData *data))
{
	struct LoginPacket mylp;

	/* save Done to call later */
	CachedDone = Done;

	/* construct new login packet with prefix removed, if any */
	memcpy(&mylp, lp, lplen);
	if (strchr("+->@#$", mylp.name[0]))
	{
		PREFIX(p) = mylp.name[0];
		memmove(mylp.name, mylp.name + 1, 31);
	}
	else
		PREFIX(p) = 0;

	/* call it */
	oldauth->Authenticate(p, &mylp, lplen, MyDone);
}


local Iauth myauth =
{
	INTERFACE_HEAD_INIT(I_AUTH, "auth-prefix")
	Authenticate
};

EXPORT const char info_auth_prefix[] = CORE_MOD_INFO("auth_prefix");

EXPORT int MM_auth_prefix(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		oldauth = mm->GetInterface(I_AUTH, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!oldauth || !capman) /* need another auth to layer on top of */
			return MM_FAIL;
		prefixkey = pd->AllocatePlayerData(sizeof(char));
		if (prefixkey == -1) return MM_FAIL;
		mm->RegInterface(&myauth, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&myauth, ALLARENAS))
			return MM_FAIL;
		pd->FreePlayerData(prefixkey);
		mm->ReleaseInterface(oldauth);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}

