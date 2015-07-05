
/* dist: public */

#include "asss.h"

/* prototypes */

local void ConnInit(struct sockaddr_in *sin, byte *pkt, int len, void *v);


/* globals */

local Inet *net;

EXPORT const char info_enc_null[] = CORE_MOD_INFO("enc_null");

EXPORT int MM_enc_null(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		net = mm->GetInterface(I_NET, ALLARENAS);
		mm->RegCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->UnregCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		mm->ReleaseInterface(net);
		return MM_OK;
	}
	return MM_FAIL;
}


void ConnInit(struct sockaddr_in *sin, byte *pkt, int len, void *v)
{
	int key, type;
	Player *p;

	/* make sure the packet fits */
	if (len != 8 || pkt[0] != 0x00 || pkt[1] != 0x01 || pkt[7] != 0x00)
		return;

	/* figure out type */
	if (pkt[6] == 0x01)
		type = T_VIE;
	else if (pkt[6] == 0x11)
		type = T_CONT;
	else
		/* unknown type */
		return;

	/* get connection. NULL encryption means none. */
	p = net->NewConnection(type, sin, NULL, v);

	if (!p)
	{
		/* no slots left? */
		byte pkt[2] = { 0x00, 0x07 };
		net->ReallyRawSend(sin, (byte*)&pkt, 2, v);
		return;
	}

	key = *(int*)(pkt+2);

	{
		/* respond. sending back the key without change means no
		 * encryption, both to 1.34 and cont */
#pragma pack(push, 1)
		struct
		{
			u8 t1, t2;
			int key;
		}
		pkt = { 0x00, 0x02, key };
#pragma pack(pop)
		net->ReallyRawSend(sin, (byte*)&pkt, sizeof(pkt), v);
	}
}

