
#include <stdlib.h>
#include <stdio.h>

typedef unsigned char byte;

typedef struct EncData
{
	int key;
	byte table[520];
} EncData;


static void do_init(EncData *ed, int k)
{
	int t, loop;
	short *mytable;

	ed->key = k;

	if (k == 0) return;

	mytable = (short*)(ed->table);

	for (loop = 0; loop < 0x104; loop++)
	{
		//printf("key: %x\n", k);
#ifndef WIN32
		asm ( "imul %%ecx" : "=d" (t) : "a" (k), "c" (0x834E0B5F) );
#else
		_asm
		{
			mov eax,k
			mov ecx,0x834E0B5F
			imul ecx
			mov t,edx
		};
#endif
		t = (t + k) >> 16;
		t += t >> 31;
		t = ((((((t * 9) << 3) - t) * 5) << 1) - t) << 2;
		k = (((k % 0x1F31D) * 0x41A7) - t) + 0x7B;
		if (!k || (k & 0x80000000)) k += 0x7FFFFFFF;
		mytable[loop] = (short)k;
	}
}


static int do_enc(EncData *ed, byte *data, int len)
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


static int do_dec(EncData *ed, byte *data, int len)
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


static void dump_pk(byte *d, int len)
{
	char str[80];
	int c;
	while (len > 0)
	{
		for (c = 0; c < 16 && len > 0; c++, len--)
			sprintf(str + 3*c, "%02x ", *d++);
		str[strlen(str)-1] = 0;
		puts(str);
	}
}


int main(int argc, char *argv[])
{
	int k;
	EncData ed;
	byte foo[512];

	if (argc < 2)
		return;

	k = strtoul(argv[1], NULL, 0);

	do_init(&ed, k);

	//dump_pk(ed.table, sizeof(ed.table));
	//return;

	for (k = 0; k < 32; k++) foo[k] = k;
	do_enc(&ed, foo, 32);
	dump_pk(foo, 32);
	for (k = 0; k < 32; k++) foo[k] = k;
	do_dec(&ed, foo, 32);
	dump_pk(foo, 32);
}

