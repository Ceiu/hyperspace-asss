
/* dist: public */

#include "asss.h"
#include "encrypt.h"

#define BAD_KEY (-1) /* valid keys must be positive */

/* structs */

typedef struct EncData
{
	int key;
	char table[520];
} EncData;


/* prototypes */

local void ConnInit(struct sockaddr_in *sin, byte *pkt, int len, void *v);

local void Init(Player *p, int k);

local int Encrypt(Player *p, byte *, int);
local int Decrypt(Player *p, byte *, int);
local void Void(Player *p);

local ClientEncryptData * ClientInit(void);
local int ClientEncrypt(ClientEncryptData *ced, byte *, int);
local int ClientDecrypt(ClientEncryptData *ced, byte *, int);
local void ClientVoid(ClientEncryptData *ced);


/* globals */

local int enckey;
local pthread_mutex_t mtx;

local Inet *net;
local Iplayerdata *pd;

local Iencrypt ienc =
{
	INTERFACE_HEAD_INIT("__unused__", "enc-vie")
	Encrypt, Decrypt, Void
};

local Iclientencrypt iclienc =
{
	INTERFACE_HEAD_INIT("__unused__", "enc-vie-client")
	ClientInit, ClientEncrypt, ClientDecrypt, ClientVoid
};

EXPORT const char info_enc_vie[] = CORE_MOD_INFO("enc_vie");

EXPORT int MM_enc_vie(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		net = mm->GetInterface(I_NET, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!net || !pd) return MM_FAIL;
		enckey = pd->AllocatePlayerData(sizeof(EncData*));
		if (enckey == -1) return MM_FAIL;
		mm->RegCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		pthread_mutex_init(&mtx, NULL);
		mm->RegInterface(&ienc, ALLARENAS);
		mm->RegInterface(&iclienc, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&ienc, ALLARENAS))
			return MM_FAIL;
		if (mm->UnregInterface(&iclienc, ALLARENAS))
			return MM_FAIL;
		mm->UnregCallback(CB_CONNINIT, ConnInit, ALLARENAS);
		pd->FreePlayerData(enckey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(pd);
		pthread_mutex_destroy(&mtx);
		return MM_OK;
	}
	return MM_FAIL;
}


local void ConnInit(struct sockaddr_in *sin, byte *pkt, int len, void *v)
{
	int key;
	Player *p;

	/* make sure the packet fits */
	if (len != 8 || pkt[0] != 0x00 || pkt[1] != 0x01 ||
			pkt[6] != 0x01 || pkt[7] != 0x00)
		return;

	/* ok, it fits. get connection. */
	++ienc.head.global_refcount;
	p = net->NewConnection(T_VIE, sin, &ienc, v);

	if (!p)
	{
		/* no slots left? */
		byte pkt[2] = { 0x00, 0x07 };
		net->ReallyRawSend(sin, (byte*)&pkt, 2, v);
		return;
	}

	key = *(int*)(pkt+2);
	key = -key;

	{
		/* respond */
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

	/* init encryption tables */
	Init(p, key);
}


local void do_init(EncData *ed, int k)
{
	int t, loop;
	short *mytable;

	ed->key = k;

	if (k == 0) return;

	mytable = (short*)(ed->table);

	for (loop = 0; loop < 0x104; loop++)
	{
		t = ((i64)k * 0x834E0B5F) >> 48;
		t += t >> 31;
		k = ((k % 127773) * 16807) - (t * 2836) + 123;
		if (!k || (k & 0x80000000)) k += 0x7FFFFFFF;
		mytable[loop] = (short)k;
	}
}

local void Init(Player *p, int k)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);

	pthread_mutex_lock(&mtx);
	if (!(ed = *p_ed)) ed = *p_ed = amalloc(sizeof(*ed));
	pthread_mutex_unlock(&mtx);

	do_init(ed, k);
}


local int do_enc(EncData *ed, byte *data, int len)
{
	int work = ed->key, *mytable = (int*)ed->table;
	int loop, until, *mydata;

	if (work == 0 || mytable == NULL) return len;

	if (data[0] == 0)
	{
		mydata = (int*)(data + 2);
		until = (len-2)/4 + 1;
	}
	else
	{
		mydata = (int*)(data + 1);
		until = (len-1)/4 + 1;
	}

	for (loop = 0; loop < until; loop++)
	{
		work = mydata[loop] ^ (mytable[loop] ^ work);
		mydata[loop] = work;
	}
	return len;
}

local int Encrypt(Player *p, byte *data, int len)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	pthread_mutex_lock(&mtx);
	ed = *p_ed;
	pthread_mutex_unlock(&mtx);
	return ed ? do_enc(ed, data, len) : len;
}


local int do_dec(EncData *ed, byte *data, int len)
{
	int work = ed->key, *mytable = (int*)ed->table;
	int *mydata, loop, until;

	if (work == 0 || mytable == NULL) return len;

	if (data[0] == 0)
	{
		mydata = (int*)(data + 2);
		until = (len-2)/4 + 1;
	}
	else
	{
		mydata = (int*)(data + 1);
		until = (len-1)/4 + 1;
	}

	for (loop = 0; loop < until; loop++)
	{
		int tmp = mydata[loop];
		mydata[loop] = mytable[loop] ^ work ^ tmp;
		work = tmp;
	}
	return len;
}

local int Decrypt(Player *p, byte *data, int len)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	pthread_mutex_lock(&mtx);
	ed = *p_ed;
	pthread_mutex_unlock(&mtx);
	return ed ? do_dec(ed, data, len) : len;
}


local void Void(Player *p)
{
	EncData *ed, **p_ed = PPDATA(p, enckey);
	pthread_mutex_lock(&mtx);
	ed = *p_ed;
	afree(ed);
	*p_ed = NULL;
	pthread_mutex_unlock(&mtx);
}


local ClientEncryptData * ClientInit(void)
{
	EncData *ed = amalloc(sizeof(*ed));
	ed->key = BAD_KEY;
	return (ClientEncryptData*)ed;
}

local int ClientEncrypt(ClientEncryptData *ced, byte *d, int n)
{
	EncData *ed = (EncData*)ced;
	if (d[1] == 0x01 && d[0] == 0x00)
	{
		/* sending key init */
		/* temporarily overload the key field to be what _we_ sent */
		ed->key = *(int*)(d+2);
		return n;
	}
	else if (ed->key != BAD_KEY)
		return do_enc(ed, d, n);
	else
		return n;
}

local int ClientDecrypt(ClientEncryptData *ced, byte *d, int n)
{
	EncData *ed = (EncData*)ced;
	if (d[1] == 0x02 && d[0] == 0x00)
	{
		/* got key response */
		int gotkey = *(int*)(d+2);
		if (gotkey == ed->key) /* signal for no encryption */
			ed->key = BAD_KEY;
		else
			do_init(ed, gotkey);
		return n;
	}
	else if (ed->key != BAD_KEY)
		return do_dec(ed, d, n);
	else
		return n;
}

local void ClientVoid(ClientEncryptData *ced)
{
	afree(ced);
}


