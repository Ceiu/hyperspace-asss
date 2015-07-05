
#include <stdio.h>

#include "../src/packets/types.h"

#define STR(x) #x
#define PKT_CALLBACK(x) STR(x)"packet"

int main()
{
	puts(PKT_CALLBACK(S2C_LOGONRESPONSE));
}


