
#include <stdio.h>

typedef char i8;
typedef short i16;
typedef int i32;
typedef unsigned char byte;

#include "../../bot/work/realsettings.txt"

int main()
{
	printf("sizeof(struct ClientSettings) = %i\n", sizeof(struct ClientSettings));
};

