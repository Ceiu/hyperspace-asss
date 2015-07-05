
/* dist: public */

#ifndef __PACKETS_OBJECTS_H
#define __PACKETS_OBJECTS_H

#pragma pack(push,1)

struct ToggledObject
{
	union
	{
		u16 id : 15;
		u16 off : 1;

		u16 n;
	};
};

struct ObjectToggling
{
	u8 type; /* S2C_TOGGLEOBJ */
	struct ToggledObject objs[1];
};


enum
{
	LAYER_BelowAll,
	LAYER_AfterBackground,
	LAYER_AfterTiles,
	LAYER_AfterWeapons,
	LAYER_AfterShips,
	LAYER_AfterGauges,
	LAYER_AfterChat,
	LAYER_TopMost,
	LAYER_Size,
};

enum
{
	MODE_ShowAlways,
	MODE_EnterZone,
	MODE_EnterArena,
	MODE_Kill,
	MODE_Death,
	MODE_ServerControlled,
	MODE_Size,
};

enum
{
	OFFSET_Normal,  /*  0 = Normal (no letters in front) */
	OFFSET_C,       /*  1 = C - Screen center */
	OFFSET_B,       /*  2 = B - Bottom right corner */
	OFFSET_S,       /*  3 = S - Stats box, lower right corner */
	OFFSET_G,       /*  4 = G - Top right corner of specials */
	OFFSET_F,       /*  5 = F - Bottom right corner of specials */
	OFFSET_E,       /*  6 = E - Below energy bar & spec data */
	OFFSET_T,       /*  7 = T - Top left corner of chat */
	OFFSET_R,       /*  8 = R - Top left corner of radar */
	OFFSET_O,       /*  9 = O - Top left corner of radar's text (clock/location) */
	OFFSET_W,       /* 10 = W - Top left corner of weapons */
	OFFSET_V,       /* 11 = V - Bottom left corner of weapons */
	OFFSET_Size,
};

struct ObjectMove   /* 11 bts */
{
	/* what properties are changed by this packet */
	u8 change_xy    :  1;
	u8 change_image :  1;
	u8 change_layer :  1;
	u8 change_time  :  1;
	u8 change_mode  :  1;
	u8 reserved     :  3;

	/* same as file format */
	struct ObjectData   /* 10 bts */
	{
		u16 mapobj  :  1;   /* screen/map object */
		u16 id      : 15;   /* object id */
		union
		{
			/* map objects */
			struct
			{
				i16 map_x, map_y;
			};

			/* screen objects */
			struct
			{
				u16 rela_x :  4;   /* OFFSET_ constant */
				i16 scrn_x : 12;
				u16 rela_y :  4;   /* OFFSET_ constant */
				i16 scrn_y : 12;
			};
		};
		u8 image;       /* image id */
		u8 layer;       /* LAYER_ constant */
		u16 time : 12;  /* display time in ticks */
		u16 mode :  4;  /* MODE_ constant */
	} data;
};

#pragma pack(pop)

#endif

