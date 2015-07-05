#ifndef TEAMNAMES_H
#define TEAMNAMES_H

#define I_TEAMNAMES "teamnames-1"

typedef struct Iteamnames
{
	INTERFACE_HEAD_DECL

	const char * (*getFreqTeamName)(int freq, Arena *arena);
	const char * (*getPlayerTeamName)(Player *p);
} Iteamnames;

#endif //TEAMNAMES_H
