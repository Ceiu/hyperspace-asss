
#include <netdb.h>

int main(int argc, char *argv[])
{
	int i;
	for (i = 1; i < argc; i++)
	{
		struct hostent *ent = gethostbyname(argv[i]);
		printf("Querying: %s\n", argv[i]);
		if (ent)
		{
			char **a;
			printf("  h_name: %s\n", ent->h_name);
			for (a = ent->h_aliases; *a; a++)
				printf("  h_aliases: %s\n", *a);
			printf("  h_length: %d\n", ent->h_length);
			for (a = ent->h_addr_list; *a; a++)
				printf("  h_addr_list: %s\n", inet_ntoa(*(struct in_addr*)*a));
		}
		else
			printf("  returned NULL\n");
	}
}

