
/* dist: public */

#ifndef __MAPNEWSDL_H
#define __MAPNEWSDL_H

#define I_MAPNEWSDL "mapnewsdl-2"

typedef struct Imapnewsdl
{
	INTERFACE_HEAD_DECL

	void (*SendMapFilename)(Player *p);
	u32 (*GetNewsChecksum)();
} Imapnewsdl;

#endif

