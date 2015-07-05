
/* dist: public */

#ifndef __PACKETS_CHAT_H
#define __PACKETS_CHAT_H

#pragma pack(push,1)

/* chat.h - chat packet */

struct ChatPacket
{
	u8 pktype;
	u8 type;
	u8 sound;
	i16 pid;
	char text[0];
};

#pragma pack(pop)

#endif

