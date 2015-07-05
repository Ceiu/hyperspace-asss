
/* 2>/dev/null
gcc -I../src -I../src/include -o ripsettings ripsettings.c
exit # */

/* this program doesn't exist */

#include <stdio.h>
#include "asss.h"
typedef unsigned char byte;
#include "packets/clientset.h"
#include "core/clientset.def"

#define COUNT(x) (sizeof(x)/sizeof(x[0]))

char *ship_names[8] = 
{
	"Warbird",
	"Javelin",
	"Spider",
	"Leviathan",
	"Terrier",
	"Weasel",
	"Lancaster",
	"Shark"
};

int main()
{
	struct ClientSettings s, *cs = &s;
	int i, j;
	FILE *f = fopen("set", "r");
	fread(cs, 1, 1428, f);
	fclose(f);

	for (i = 0; i < 8; i++)
	{
		struct WeaponBits wb;
		struct ShipSettings *ss = cs->ships + i;
		char *shipname = ship_names[i];

		for (j = 0; j < COUNT(ss->long_set); j++)
			printf("%s:%s:%d\n", shipname, ship_long_names[j], ss->long_set[j]);
		for (j = 0; j < COUNT(ss->short_set); j++)
			printf("%s:%s:%d\n", shipname, ship_short_names[j], ss->short_set[j]);
		for (j = 0; j < COUNT(ss->byte_set); j++)
			printf("%s:%s:%d\n", shipname, ship_byte_names[j], ss->byte_set[j]);

#define DO(x) \
		printf("%s:%s:%d\n", shipname, #x, wb.x)
		DO(ShrapnelMax); DO(ShrapnelRate);  DO(AntiWarpStatus);
		DO(CloakStatus); DO(StealthStatus); DO(XRadarStatus);
		DO(InitialGuns); DO(MaxGuns);       DO(InitialBombs);
		DO(MaxBombs);    DO(DoubleBarrel);  DO(EmpBomb);
		DO(SeeMines);    DO(Unused1);
#undef DO
	}

	cs->long_set[1] /= 1000;
	cs->long_set[11] /= 1000;

	for (i = 0; i < COUNT(cs->long_set); i++)
		printf("%s:%d\n", long_names[i], cs->long_set[i]);
	for (i = 0; i < COUNT(cs->short_set); i++)
		printf("%s:%d\n", short_names[i], cs->short_set[i]);
	for (i = 0; i < COUNT(cs->byte_set); i++)
		printf("%s:%d\n", byte_names[i], cs->byte_set[i]);
	for (i = 0; i < COUNT(cs->prizeweight_set); i++)
		printf("%s:%d\n", prizeweight_names[i], cs->prizeweight_set[i]);
}

