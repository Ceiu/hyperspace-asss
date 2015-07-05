
/* dist: public */

#ifndef __PACKETS_MAPFNAME_H
#define __PACKETS_MAPFNAME_H

#pragma pack(push,1)

/* mapfname.h - map filename packet */

#define MAX_LVZ_FILES 16

struct MapFilename
{
	u8 type;
	struct
	{
		char filename[16];
		u32 checksum;
		u32 size; /* cont only */
	} files[MAX_LVZ_FILES + 1];
};

#pragma pack(pop)

#endif

