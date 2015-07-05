
#include <zlib.h>
#include <stdlib.h>
#include <stdio.h>


#define BLOCKSIZE 8192


int main(int argc, char *argv[])
{
	FILE *f;
	char buf[BLOCKSIZE];
	int b;
	uLong crc = crc32(0, Z_NULL, 0);

	f = fopen(argv[1],"r");

	while (!feof(f))
	{
		b = fread(buf, 1, BLOCKSIZE, f);
		crc = crc32(crc, buf, b);
	}

	fclose(f);

	printf("%s: %X\n", argv[1], crc);

}


