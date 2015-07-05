
/* dist: public */

#ifndef __PACKETS_VOICE_H
#define __PACKETS_VOICE_H

#pragma pack(push,1)

struct S2CVoice
{
	u8 type;
	u16 source_id;
	/* voice data goes here */
};

struct C2SVoice
{
	u8 type;
	u8 voice_index;
	u16 target_id;
	/* voice data goes here */
};

#pragma pack(pop)

#endif

