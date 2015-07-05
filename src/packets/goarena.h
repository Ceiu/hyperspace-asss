
/* dist: public */

#ifndef __PACKETS_GOARENA_H
#define __PACKETS_GOARENA_H

#pragma pack(push,1)

/* goarena.h - the ?go arena change request packet */

struct GoArenaPacket
{
	u8 type;
	u8 shiptype;
	i8 obscenity_filter;
	i8 wavmsg;
	i16 xres;
	i16 yres;
	i16 arenatype;
	char arenaname[16];
	u8 optionalgraphics; /* cont */
};

#define LEN_GOARENAPACKET_VIE (sizeof(struct GoArenaPacket) - 1)
#define LEN_GOARENAPACKET_CONT sizeof(struct GoArenaPacket)

#pragma pack(pop)

#endif

