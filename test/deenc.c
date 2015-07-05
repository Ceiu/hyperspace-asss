
#include <stdio.h>
#include <stdlib.h>


typedef unsigned char byte;


/* structs */

typedef struct EncData
{
	int key, status;
	char enctable[520];
} EncData;

EncData enc;


void Init(int k)
{
	int t, loop;
	short *mytable = (short *) enc.enctable;

	if (k == 0) return;

	enc.key = k;

	for (loop = 0; loop < 0x104; loop++)
	{
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


int Decrypt(byte *data, int len)
{
	int *mytable = (int *) enc.enctable, *mydata;
	int work, loop, until, esi, edx;

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

	work = enc.key;

	if (work == 0) return len;

	for (loop = 0; loop < until; loop++)
	{
		esi = mytable[loop];
		edx = mydata[loop];
		esi ^= work;
		esi ^= edx;
		mydata[loop] = esi;
		work = edx;
	}
	return len;
}


inline int val(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	else if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	else
		return -1;
}

inline char digit(int v)
{
	if (v >= 0 && v <= 9)
		return v + '0';
	else if (v >= 10)
		return 'a' + v - 10;
}

int unhex(byte *buf, int len, const char *line)
{
	char b1, b2;
	byte *b = buf;

	while (*line)
	{
		b1 = line[0];
		b2 = line[1];

		*b++ = val(b1) << 4 | val(b2);

		line += 2;
		while (*line && val(*line) == -1)
			line++;
	}

	return b - buf;
}

void hex(byte *buf, int len)
{
	while (len--)
	{
		char b1 = digit((*buf >> 4) & 0x0f), b2 = digit(*buf & 0x0f);
		printf("%c%c ", b1, b2);
		buf++;
	}
}


int main(int argc, char *argv[])
{
	FILE *f;
	char line[4096];
	byte data[512];

	if (argc < 2)
	{
		printf("usage: deenc <filename>\n");
		exit(1);
	}

	f = fopen(argv[1], "r");

	while (fgets(line, 4096, f))
	{
		int (*func)(byte *data, int len);
		int len;

		len = unhex(data, 512, line + 2);

		if (data[0] == 0x00 && data[1] == 0x02)
			Init(*(int*)(data+2));

		Decrypt(data, len);

		printf("%c\n\n", line[0]);
		hex(data, len);
		printf("\n\n\n");
	}
}


