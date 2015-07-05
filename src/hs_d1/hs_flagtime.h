#ifndef HS_FLAGTIME_H
#define HS_FLAGTIME_H

#define I_HS_FLAGTIME "hs_flagtime-2"

typedef struct FlagTeam
{
	Arena *Arena;
	int Freq;
	int DroppedFlags;
	int FlagSeconds;
	HashTable *Breakdown;
}FlagTeam;

typedef struct FlagOwner
{
	Arena *Arena;
	char Name[24];
	int Freq;
	int FlagID;
}FlagOwner;

typedef struct Ihs_flagtime
{
	INTERFACE_HEAD_DECL
	FlagTeam *(*GetFlagTeam)(Arena *a, int freq);
	FlagOwner *(*GetFlagOwner)(Arena *a, int flagID);
	int (*GetFlagSeconds)(Player *p);
	int (*GetStatus)(Arena *a);
	void (*SetStatus)(Arena *a, int status);
	void (*Reset)(Arena *a);
}Ihs_flagtime;

#endif
