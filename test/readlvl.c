
/* 2>/dev/null
gcc -O3 -I../src -o readlvl readlvl.c
exit # */

#include <stdio.h>

#define TILE_FLAG 0xAA
#define TILE_SAFE 0xAB
#define TILE_GOAL 0xAC
#define TILE_BRICK 0xFA /* internal only */

struct TileData
{
	unsigned x : 12;
	unsigned y : 12;
	unsigned type : 8;
};

static unsigned int hist[256];
static unsigned char map[128][128];

const char *tilemap =
" ......:::ooooooxxxxxxOOOOOOXXXXXX******&&&&&&%%%%%%######@@@@@@@@@";

static void insert_sparse(int dummy, int x, int y, int tile)
{
	if (tile == TILE_FLAG)
		printf("  tile: (%4d,%4d): flag\n", x, y);
	else if (tile == TILE_SAFE)
		printf("  tile: (%4d,%4d): safe\n", x, y);
	else if (tile == TILE_GOAL)
		printf("  tile: (%4d,%4d): goal\n", x, y);
	else
		printf("  tile: (%4d,%4d): %x\n", x, y, tile);
	hist[tile]++;
	map[x>>3][y>>3]++;
}

static int read_lvl(char *name)
{
	unsigned short bm;
	struct TileData td;
	int flags = 0, errors = 0;
	int arr = 0, i, j;

	FILE *f = fopen(name, "r");
	if (!f) return 1;

	/* first try to skip over bmp header */
	fread(&bm, sizeof(bm), 1, f);
	if (bm == 0x4D42)
	{
		unsigned int len;
		fread(&len, sizeof(len), 1, f);
		fseek(f, len, SEEK_SET);
		printf("skipping bmp header to %d\n", len);
	}
	else
	{
		rewind(f);
		printf("no bmp header found\n");
	}

	/* now read the map */
	while (fread(&td, sizeof(td), 1, f))
	{
		if (td.x < 1024 && td.y < 1024)
		{
			if (td.type == TILE_FLAG)
				flags++;
			if (td.type < 0xD0)
				insert_sparse(arr, td.x, td.y, td.type);
			else
			{
				int size = 1, x, y;
				if (td.type == 0xD9)
					size = 2;
				else if (td.type == 0xDB)
					size = 6;
				else if (td.type == 0xDC)
					size = 5;
				for (x = 0; x < size; x++)
					for (y = 0; y < size; y++)
						insert_sparse(arr, td.x+x, td.y+y, td.type);
			}
		}
		else
			errors++;
	}

	fclose(f);
	printf("%d flags, %d errors\n", flags, errors);

	puts("hist:");
	for (i = 0; i < 256; i++)
		printf("  %02x: %d\n", i, hist[i]);

	puts("picture:");
	for (i = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
			putchar(tilemap[map[j][i]]);
		putchar('\n');
	}

	return 0;
}


int main(int argc, char *argv[])
{
	if (argv[1])
		read_lvl(argv[1]);
}

