
/* dist: public */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "asss.h"
#include "objects.h"
#include "packets/objects.h"

/* interface funcs */
local void SendState(Player *p);
local void Toggle(const Target *t, int id, int on);
local void ToggleSet(const Target *t, short *id, char *ons, int size);
local void Move(const Target *t, int id, int x, int y, int rx, int ry);
local void Image(const Target *t, int id, int image);
local void Layer(const Target *t, int id, int layer);
local void Timer(const Target *t, int id, int time);
local void Mode(const Target *t, int id, int mode);

enum { BROADCAST_NONE, BROADCAST_BOT, BROADCAST_ANY };

#pragma pack(push, 1)

typedef struct podata
{
	int brallow;  /* allowed broadcast modes */
} podata;

local int pokey;

typedef struct lvzdata
{
	u8 off : 1;
	u8 reserved : 7;

	struct ObjectData defaults, current; /* extended data */
} lvzdata;

typedef struct aodata
{
	LinkedList list; /* of lvzdata */
	u32 tog_diffs, ext_diffs; /* # of modified objects */

	pthread_mutex_t obj_mtx;
} aodata;

local int aokey;

#define MUTEX_LOCK(ad) pthread_mutex_lock(&(ad)->obj_mtx)
#define MUTEX_UNLOCK(ad) pthread_mutex_unlock(&(ad)->obj_mtx)

/* utility functions */
local lvzdata *getDataFromId(LinkedList *list, u16 object_id);
local void ReadLVZFile(Arena *a, byte *file, u32 flen, int opt);

/* callbacks */
local void PBroadcast(Player *p, byte *pkt, int len);
local void PlayerAction(Player *p, int action, Arena *arena);
local void ArenaAction(Arena *arena, int action);

/* local data */
local Imodman *mm;
local Imainloop *mainloop;
local Inet *net;
local Icapman *capman;
local Ilogman *lm;
local Iarenaman *aman;
local Iconfig *cfg;
local Imapdata *mapdata;
local Icmdman *cmd;
local Iplayerdata *pd;
local Ichat *chat;

local Iobjects _myint =
{
	INTERFACE_HEAD_INIT(I_OBJECTS, "objects")

	SendState,
	Toggle,
	ToggleSet,
	Move,
	Image,
	Layer,
	Timer,
	Mode
};


/* command functions */
local helptext_t objon_help =
"Targets: any\n"
"Args: object id\n"
"Toggles the specified object on.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjon(const char *cmd, const char *params, Player *p, const Target *target)
{
	Toggle(target, atoi(params), 1);
}

local helptext_t objoff_help =
"Targets: any\n"
"Args: object id\n"
"Toggles the specified object off.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjoff(const char *cmd, const char *params, Player *p, const Target *target)
{
	Toggle(target, atoi(params), 0);
}

local helptext_t objset_help =
"Targets: any\n"
"Args: [+/-]object id [+/-]id ...\n"
"Toggles the specified objects on/off.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjset(const char *cmd, const char *params, Player *p, const Target *target)
{
	int l = strlen(params) + 1;
	const char *c = params;
	short *objs = alloca(l * sizeof(short));
	char *ons = alloca(l * sizeof(char));

	l = 0;
	for (;;)
	{
		/* move to next + or - */
		while (*c != 0 && *c != '-' && *c != '+' && c[1] != '-' && c[1] != '+')
			c++;
		if (*c == 0)
			break;

		/* change it */
		if (*c == '+')
			ons[l] = 1;
		else
			ons[l] = 0;

		c++;
		objs[l++] = atoi(c);
	}

	if (l) ToggleSet(target, objs, ons, l);
}

local helptext_t objmove_help =
"Targets: any\n"
"Args: <id> <x> <y> (for map obj) or <id> [CBSGFETROWV]<0/1> [CBSGFETROWV]<0/1> (screen obj)\n"
"Moves an LVZ map or screen object. Coordinates are in pixels.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjmove(const char *cmd, const char *params, Player *p, const Target *target)
{
	int offset, i = 0, j = 0, v[5] = {0,0,0,0,0};
	char ch, field[256];

	do
	{
		ch = *params++;

		if (ch == ' ' || ch == '\0')
		{
			if (!i) continue;

			offset = OFFSET_Normal;
			field[i] = 0;
			i = 0;

			switch (tolower(*field))
			{
				case 'c': offset = OFFSET_C; break;
				case 'b': offset = OFFSET_B; break;
				case 's': offset = OFFSET_S; break;
				case 'g': offset = OFFSET_G; break;
				case 'f': offset = OFFSET_F; break;
				case 'e': offset = OFFSET_E; break;
				case 't': offset = OFFSET_T; break;
				case 'r': offset = OFFSET_R; break;
				case 'o': offset = OFFSET_O; break;
				case 'w': offset = OFFSET_W; break;
				case 'v': offset = OFFSET_V; break;
			}

			if (offset == OFFSET_Normal)
				v[j++] = atoi(field);
			else
			{
				v[j + 2] = offset;
				v[j++] = atoi(field+1);
			}
			if (j >= 3) break;
		}
		else if (i < sizeof(field)-1)
			field[i++] = ch;
	} while (ch);

	Move(target, v[0], v[1], v[2], v[3], v[4]);
}

local helptext_t objimage_help =
"Targets: any\n"
"Args: <id> <image>\n"
"Change the image associated with an object id.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjimage(const char *cmd, const char *params, Player *p, const Target *target)
{
	char id[256];
	const char *image = delimcpy(id, params, sizeof(id), ' ');
	if (image) Image(target, atoi(id), atoi(image));
	else chat->SendMessage(p, "Invalid syntax. Please read help for ?objimage");
}

local helptext_t objlayer_help =
"Targets: any\n"
"Args: <id> <layer code>\n"
"Change the image associated with an object id. Layer codes:\n"
"BelowAll  AfterBackground  AfterTiles  AfterWeapons\n"
"AfterShips  AfterGauges  AfterChat  TopMost\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjlayer(const char *cmd, const char *params, Player *p, const Target *target)
{
	int code;
	char id[256];
	const char *layer = delimcpy(id, params, sizeof(id), ' ');
	if (!layer) return;

	if (!strcasecmp(layer, "AfterBackground"))     code = LAYER_AfterBackground;
	else if (!strcasecmp(layer, "BelowAll"))       code = LAYER_BelowAll;
	else if (!strcasecmp(layer, "AfterTiles"))     code = LAYER_AfterTiles;
	else if (!strcasecmp(layer, "AfterWeapons"))   code = LAYER_AfterWeapons;
	else if (!strcasecmp(layer, "AfterShips"))     code = LAYER_AfterShips;
	else if (!strcasecmp(layer, "AfterGauges"))    code = LAYER_AfterGauges;
	else if (!strcasecmp(layer, "AfterChat"))      code = LAYER_AfterChat;
	else if (!strcasecmp(layer, "TopMost"))        code = LAYER_TopMost;
	else return;

	Layer(target, atoi(id), code);
}

local helptext_t objtimer_help =
"Targets: any\n"
"Args: <id> <timer>\n"
"Change the timer associated with an object id.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjtimer(const char *cmd, const char *params, Player *p, const Target *target)
{
	char id[256];
	const char *timer = delimcpy(id, params, sizeof(id), ' ');
	if (timer) Timer(target, atoi(id), atoi(timer));
	else chat->SendMessage(p, "Invalid syntax. Please read help for ?objtimer");
}

local helptext_t objmode_help =
"Targets: any\n"
"Args: <id> <mode code>\n"
"Change the mode associated with an object id. Mode codes:\n"
"ShowAlways  EnterZone  EnterArena  Kill  Death  ServerControlled\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjmode(const char *cmd, const char *params, Player *p, const Target *target)
{
	int code;
	char id[256];
	const char *mode = delimcpy(id, params, sizeof(id), ' ');
	if (!mode) return;

	if (!strcasecmp(mode, "ServerControlled"))    code = MODE_ServerControlled;
	else if (!strcasecmp(mode, "EnterZone"))      code = MODE_EnterZone;
	else if (!strcasecmp(mode, "ShowAlways"))     code = MODE_ShowAlways;
	else if (!strcasecmp(mode, "EnterArena"))     code = MODE_EnterArena;
	else if (!strcasecmp(mode, "Kill"))           code = MODE_Kill;
	else if (!strcasecmp(mode, "Death"))          code = MODE_Death;
	else return;

	Mode(target, atoi(id), code);
}

local helptext_t objinfo_help =
"Targets: none\n"
"Args: <id>\n"
"Reports all known information about the object.\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local const char *LAYER_String[] = {
	"BelowAll", "AfterBackground", "AfterTiles", "AfterWeapons",
	"AfterShips", "AfterGauges", "AfterChat", "TopMost",
};

local const char *MODE_String[] = {
	"ShowAlways", "EnterZone", "EnterArena",
	"Kill", "Death", "ServerControlled",
};

local const char *OFFSET_String[] = {
	"Normal - from upper left",
	"C - Screen center",
	"B - Bottom right corner",
	"S - Stats box, lower right corner",
	"G - Top right corner of specials",
	"F - Bottom right corner of specials",
	"E - Below energy bar & spec data",
	"T - Top left corner of chat",
	"R - Top left corner of radar",
	"O - Top left corner of radar's text (clock/location)",
	"W - Top left corner of weapons",
	"V - Bottom left corner of weapons",
};

local void Cobjinfo(const char *cmd, const char *params, Player *p, const Target *target)
{
	int id = atoi(params);
	if (p->status == S_PLAYING)
	{
		aodata *ad = P_ARENA_DATA(p->arena, aokey);
		lvzdata *node = getDataFromId(&ad->list, id);
		if (!node)
			chat->SendMessage(p, "Object %i does not exist in any of the loaded LVZ files.", id);
		else
		{
			chat->SendMessage(p, "lvz: Id:%i Off:%i Image:%i Layer:%s Mode:%s",
				id, node->off, node->current.image,
				(node->current.layer < LAYER_Size) ? LAYER_String[node->current.layer] : "?",
				(node->current.mode < MODE_Size) ? MODE_String[node->current.mode] : "?");
			if (node->current.mapobj)
				chat->SendMessage(p, "lvz: map object coords (%i, %i)",
					node->current.map_x, node->current.map_y);
			else
				chat->SendMessage(p, "lvz: screen object coords (%i, %i). X-offset: %s. Y-offset: %s",
					node->current.scrn_x, node->current.scrn_y,
					(node->current.rela_x < OFFSET_Size) ? OFFSET_String[node->current.rela_x] : "?",
					(node->current.rela_y < OFFSET_Size) ? OFFSET_String[node->current.rela_y] : "?");
		}
	}
}

local helptext_t objlist_help =
"Targets: none\n"
"Args: none\n"
"List all ServerControlled object id's. Use ?objinfo <id> for attributes\n"
"Object commands: ?objon ?objoff ?objset ?objmove ?objimage ?objlayer ?objtimer ?objmode ?objinfo ?objlist\n";

local void Cobjlist(const char *cmd, const char *params, Player *p, const Target *target)
{
	StringBuffer sb;
	aodata *ad = P_ARENA_DATA(p->arena, aokey);
	int scnt = 0;
	Link *l;

	SBInit(&sb);
	MUTEX_LOCK(ad);

	for (l = LLGetHead(&ad->list); l; l = l->next)
	{
		lvzdata *node = l->data;

		if (node->current.mode == MODE_ServerControlled)
		{
			SBPrintf(&sb, ", %d", node->current.id);
			scnt++;
		}
	}

	MUTEX_UNLOCK(ad);

	if (scnt)
	{
		chat->SendMessage(p, "%d ServerControlled object%s:",
				scnt, (scnt == 1) ? "" : "s");
		chat->SendWrappedText(p, SBText(&sb, 2));
	}
	else
		chat->SendMessage(p, "0 ServerControlled objects.");
	SBDestroy(&sb);
}

EXPORT const char info_objects[] = CORE_MOD_INFO("objects");

EXPORT int MM_objects(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		mainloop = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		chat = mm->GetInterface(I_CHAT, ALLARENAS);

		if (!mainloop || !aman || !net || !capman || !pd || !lm || !cfg ||
		    !mapdata || !cmd || !chat)
			return MM_FAIL;

		pokey = pd->AllocatePlayerData(sizeof(podata));
		if (pokey == -1) return MM_FAIL;

		net->AddPacket(C2S_REBROADCAST, PBroadcast);

		aokey = aman->AllocateArenaData(sizeof(aodata));
		if (aokey == -1) return MM_FAIL;

		mm->RegInterface(&_myint, ALLARENAS);

		mm->RegCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		cmd->AddCommand("objon", Cobjon, ALLARENAS, objon_help);
		cmd->AddCommand("objoff", Cobjoff, ALLARENAS, objoff_help);
		cmd->AddCommand("objset", Cobjset, ALLARENAS, objset_help);
		cmd->AddCommand("objmove", Cobjmove, ALLARENAS, objmove_help);
		cmd->AddCommand("objimage", Cobjimage, ALLARENAS, objimage_help);
		cmd->AddCommand("objlayer", Cobjlayer, ALLARENAS, objlayer_help);
		cmd->AddCommand("objtimer", Cobjtimer, ALLARENAS, objtimer_help);
		cmd->AddCommand("objmode", Cobjmode, ALLARENAS, objmode_help);
		cmd->AddCommand("objinfo", Cobjinfo, ALLARENAS, objinfo_help);
		cmd->AddCommand("objlist", Cobjlist, ALLARENAS, objlist_help);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_myint, ALLARENAS))
			return MM_FAIL;

		mm->UnregCallback(CB_PLAYERACTION, PlayerAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		net->RemovePacket(C2S_REBROADCAST, PBroadcast);

		pd->FreePlayerData(pokey);
		aman->FreeArenaData(aokey);

		cmd->RemoveCommand("objon", Cobjon, ALLARENAS);
		cmd->RemoveCommand("objoff", Cobjoff, ALLARENAS);
		cmd->RemoveCommand("objset", Cobjset, ALLARENAS);
		cmd->RemoveCommand("objmove", Cobjmove, ALLARENAS);
		cmd->RemoveCommand("objimage", Cobjimage, ALLARENAS);
		cmd->RemoveCommand("objlayer", Cobjlayer, ALLARENAS);
		cmd->RemoveCommand("objtimer", Cobjtimer, ALLARENAS);
		cmd->RemoveCommand("objmode", Cobjmode, ALLARENAS);
		cmd->RemoveCommand("objinfo", Cobjinfo, ALLARENAS);
		cmd->RemoveCommand("objlist", Cobjlist, ALLARENAS);

		mm->ReleaseInterface(mainloop);
		mm->ReleaseInterface(cmd);
		mm->ReleaseInterface(mapdata);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(chat);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}


void PBroadcast(Player *p, byte *pkt, int len)
{
	struct SimplePacket *reb = (struct SimplePacket *)pkt;
	int pid, type;
	podata *ppd = PPDATA(p, pokey);

	if (len < 4)
	{
		lm->LogP(L_MALICIOUS, "objects", p, "bad rebroadcast packet len=%i", len);
		return;
	}

	pid = reb->d1;
	type = pkt[3];

	if (ppd->brallow == BROADCAST_NONE || p->status != S_PLAYING)
	{
		lm->LogP(L_MALICIOUS, "objects", p, "attempted rebroadcast without permission");
		return;
	}

	switch (type)
	{
		case S2C_ZERO:
			return;
		case S2C_MOVEOBJECT:
			if (len < 8 || (len - 4) % sizeof(struct ObjectMove))
			{
				lm->LogP(L_INFO, "objects", p, "invalid move object rebroadcast");
				return;
			}
			else if (pid == -1) /* to whole arena */
			{
				int i = (len - 4) / sizeof(struct ObjectMove);
				struct ObjectMove *objm = (struct ObjectMove *)(pkt + 4);
				aodata *ad = P_ARENA_DATA(p->arena, aokey);
				lvzdata *node;

				MUTEX_LOCK(ad);

				while (i--)
					if ((node = getDataFromId(&ad->list, objm[i].data.id)))
					{
						/* note that bots only need to send what they changed */
						int changed = memcmp(&node->current, &node->defaults, sizeof(struct ObjectData));
						int changing = 0;

						/* hack: map_x = scrn_x|rela_x */
						if (objm[i].change_xy &&
						    (objm[i].data.map_x != node->defaults.map_x ||
						     objm[i].data.map_y != node->defaults.map_y))
						{
							changing = 1;
							node->current.map_x = objm[i].data.map_x;
							node->current.map_y = objm[i].data.map_y;
						}
						if (objm[i].change_image && (objm[i].data.image != node->defaults.image))
						{
							changing = 1;
							node->current.image = objm[i].data.image;
						}
						if (objm[i].change_layer && (objm[i].data.layer != node->defaults.layer))
						{
							changing = 1;
							node->current.layer = objm[i].data.layer;
						}
						if (objm[i].change_mode && (objm[i].data.mode != node->defaults.mode))
						{
							changing = 1;
							node->current.mode = objm[i].data.mode;
						}
						if (objm[i].change_time && (objm[i].data.time != node->defaults.time))
						{
							changing = 1;
							node->current.time = objm[i].data.time;
						}

						if (changing && !changed)
							ad->ext_diffs++;
						else if (!changing && changed)
							ad->ext_diffs--;
						lm->LogA(L_DRIVEL, "objects", p->arena,
								"morphed object %i. saving %i objects",
								objm[i].data.id, ad->ext_diffs);
					}

				MUTEX_UNLOCK(ad);
			}
			break;

		case S2C_TOGGLEOBJ:
			if (len < 6 || (len - 4) % sizeof(struct ToggledObject))
			{
				lm->LogP(L_INFO, "objects", p, "invalid toggle object rebroadcast");
				return;
			}
			else if (pid == -1)
			{
				int i = (len - 4) / sizeof(struct ToggledObject);
				struct ToggledObject *objt = (struct ToggledObject *)(pkt + 4);
				aodata *ad = P_ARENA_DATA(p->arena, aokey);
				lvzdata *node;

				MUTEX_LOCK(ad);

				while (i--)
					if ((node = getDataFromId(&ad->list, objt[i].id)) &&
					    node->current.time == 0)
					{
						if (objt[i].off == 1 && node->off == 0)
							ad->tog_diffs--;
						else if (objt[i].off == 0 && node->off == 1)
							ad->tog_diffs++;
						node->off = (u8)objt[i].off;
						lm->LogA(L_DRIVEL, "objects", p->arena,
								"toggled object %i. saving %i objects", objt[i].id, ad->tog_diffs);
					}

				MUTEX_UNLOCK(ad);

			}
			break;

		default:
			if (ppd->brallow != BROADCAST_ANY)
			{
				lm->LogP(L_INFO, "objects", p, "bot is not authorized to rebroadcast packet type %d", type);
				return;
			}
	}

	if (pid == -1)
		net->SendToArena(p->arena, 0, pkt + 3, len - 3, NET_RELIABLE);
	else
	{
		Player *t = pd->PidToPlayer(reb->d1);
		if (t && t->status == S_PLAYING && p->arena == t->arena)
			net->SendToOne(t, pkt + 3, len - 3, NET_RELIABLE);
	}
}


/* player/arena actions */

void PlayerAction(Player *p, int action, Arena *arena)
{
	if (action == PA_ENTERARENA)
	{
		podata *ppd = PPDATA(p, pokey);
		ppd->brallow = BROADCAST_NONE;

		if (capman->HasCapability(p, CAP_BROADCAST_ANY))
			ppd->brallow = BROADCAST_ANY;
		else if (capman->HasCapability(p, CAP_BROADCAST_BOT))
			ppd->brallow = BROADCAST_BOT;
	}
	else if (action == PA_ENTERGAME)
	{
		SendState(p);
	}
}


local void one_lvz_file(const char *fn, int optional, void *clos)
{
	Arena *arena = clos;
	MMapData *mmd = MapFile(fn, FALSE);
	if (mmd)
	{
		lm->LogA(L_DRIVEL, "objects", arena, "reading object: %s", fn);
		ReadLVZFile(arena, mmd->data, mmd->len, optional);
		UnmapFile(mmd);
	}
}

local void aaction_work(void *clos)
{
	Arena *arena = clos;
	mapdata->EnumLVZFiles(arena, one_lvz_file, arena);
	aman->Unhold(arena);
}

void ArenaAction(Arena *arena, int action)
{
	aodata *ad = P_ARENA_DATA(arena, aokey);

	if (action == AA_PRECREATE)
	{
		pthread_mutex_init(&ad->obj_mtx, NULL);
	}
	else if (action == AA_CREATE)
	{
		mainloop->RunInThread(aaction_work, arena);
		aman->Hold(arena);
	}
	else if (action == AA_DESTROY)
	{
		Link *l;
		for (l = LLGetHead(&ad->list); l; l = l->next)
			afree(l->data);
		LLEmpty(&ad->list);
	}
	else if (action == AA_POSTDESTROY)
	{
		pthread_mutex_destroy(&ad->obj_mtx);
	}
}

/* call with mutex locked */
lvzdata *getDataFromId(LinkedList *list, u16 object_id)
{
	Link *l;

	for (l = LLGetHead(list); l; l = l->next)
		if (((lvzdata*)l->data)->defaults.id == object_id)
			return l->data;

	return NULL;
}


/* server's LVZ object file loader */

#include "zlib.h"

#define HEADER_CONT 0x544e4f43
#define HEADER_CLV1 0x31564c43
#define HEADER_CLV2 0x32564c43

struct LVZ_HEADER
{
	u32 CONT;
	u32 num_sections;
};

struct SECTION_HEADER
{
	u32 CONT;
	u32 decompress_size;
	u32 file_time;
	u32 compressed_size;
	char fname[1];	/* null-terminated */
	/* compressed data */
};

struct OBJECT_SECTION
{
	u32 version;
	u32 object_count;
	u32 image_count;
	/* object data */
	/* image data */
};

void ReadLVZFile(Arena *a, byte *file, u32 flen, int opt)
{
	aodata *ad = P_ARENA_DATA(a, aokey);
	struct LVZ_HEADER *lvzh = (struct LVZ_HEADER *)file;
	int num_sections;

	if (flen <= sizeof(struct LVZ_HEADER)) return;

	num_sections = lvzh->num_sections;
	if (lvzh->CONT != HEADER_CONT || num_sections <= 0) return;

	file += sizeof(struct LVZ_HEADER);
	flen -= sizeof(struct LVZ_HEADER);

	while (num_sections--)
	{
		struct SECTION_HEADER *secth = (struct SECTION_HEADER *)file;
		u32 namelen;

		if (flen <= sizeof(struct SECTION_HEADER)) return;
		file += sizeof(struct SECTION_HEADER);
		flen -= sizeof(struct SECTION_HEADER);

		if (secth->CONT != HEADER_CONT) return;
		/* if EOF without a nul-term, we're sunk */
		namelen = strlen(secth->fname);

		if (flen <= namelen) return;
		file += namelen;
		flen -= namelen;

		if (namelen == 0 && secth->file_time == 0)
		{
			uLongf decompress_size = secth->decompress_size;
			u32 dlen = decompress_size;
			byte *dcmp = amalloc(dlen);

			if (uncompress(dcmp, &decompress_size, file, secth->compressed_size) == Z_OK)
			{
				struct OBJECT_SECTION *objs = (struct OBJECT_SECTION *)dcmp;
				struct ObjectData *objd = (struct ObjectData *)(objs + 1);

				/* we're looking for object sections */
				if ((dlen > sizeof(struct OBJECT_SECTION)) &&
				    (objs->version == HEADER_CLV1 ||
				     objs->version == HEADER_CLV2))
				{
					MUTEX_LOCK(ad);

					while (objs->object_count-- > 0)
					{
						lvzdata *data = amalloc(sizeof(lvzdata));

						data->off = 1;
						memcpy(&data->defaults, objd, sizeof(*objd));
						memcpy(&data->current, objd, sizeof(*objd));

						LLAdd(&ad->list, data);

						objd++;
					}

					MUTEX_UNLOCK(ad);
				}
			}

			afree(dcmp);
		}

		if (flen <= secth->compressed_size) return;
		file += secth->compressed_size;
		flen -= secth->compressed_size;
	}
}


/* interface functions */

void SendState(Player *p)
{
	aodata *ad = P_ARENA_DATA(p->arena, aokey);
	u32 t = 0, e = 0;
	byte *toggle = alloca(1 + 2 * ad->tog_diffs);
	byte *extra = alloca(1 + 11 * ad->ext_diffs);
	Link *l;

	toggle[0] = S2C_TOGGLEOBJ;
	extra[0] = S2C_MOVEOBJECT;

	MUTEX_LOCK(ad);

	for (l = LLGetHead(&ad->list); l; l = l->next)
	{
		lvzdata *data = l->data;

		/* check for changes in data */
		if (memcmp(&data->defaults, &data->current, 10))
		{
			extra[1 + 11*e] = 255; /* hack: everything "changed" */
			memcpy(extra + 2 + 11*e++, &data->current, 10);
		}

		if (data->off == 0)
			*(u16*)(toggle+1+2*t++) = data->defaults.id;

		if (t >= ad->tog_diffs && e >= ad->ext_diffs)
			break;
	}

	MUTEX_UNLOCK(ad);

	if (t) net->SendToOne(p, toggle, 1 + 2 * t, NET_RELIABLE);
	if (e) net->SendToOne(p, extra, 1 + 11 * e, NET_RELIABLE);
}

void Toggle(const Target *t, int id, int on)
{
	byte toggle[3] = { S2C_TOGGLEOBJ, id & 0xff, (id>>8) | (on ? 0 : 0x80) };

	net->SendToTarget(t, toggle, sizeof(toggle), NET_RELIABLE);

	if (t->type == T_ARENA)
	{
		aodata *ad = P_ARENA_DATA(t->u.arena, aokey);
		lvzdata *node;

		MUTEX_LOCK(ad);

		if ((node = getDataFromId(&ad->list, id)) &&
		    node->current.time == 0)
		{
			if (on != 0 && node->off == 1)
				ad->tog_diffs++;
			else if (on == 0 && node->off == 0)
				ad->tog_diffs--;
			node->off = (on != 0) ? 0 : 1;
			lm->LogA(L_DRIVEL, "objects", t->u.arena,
					"toggled object %i. saving %i objects", node->defaults.id, ad->tog_diffs);
		}

		MUTEX_UNLOCK(ad);
	}
}

void ToggleSet(const Target *t, short *id, char *ons, int size)
{
	int pktlen = 1 + 2 * size;
	byte *pkt = alloca(pktlen);

	pkt[0] = S2C_TOGGLEOBJ;

	while (size--)
	{
		*(u16*)(pkt + 1 + 2 * size) = id[size] | (ons[size] ? 0 : 0x8000);

		if (t->type == T_ARENA)
		{
			aodata *ad = P_ARENA_DATA(t->u.arena, aokey);
			lvzdata *node;

			MUTEX_LOCK(ad);

			if ((node = getDataFromId(&ad->list, id[size])) &&
			    node->current.time == 0)
			{
				if (ons[size] != 0 && node->off == 1)
					ad->tog_diffs++;
				else if (ons[size] == 0 && node->off == 0)
					ad->tog_diffs--;
				node->off = (ons[size] != 0) ? 0 : 1;
				lm->LogA(L_DRIVEL, "objects", t->u.arena,
					"toggled object %i. saving %i objects", node->defaults.id, ad->tog_diffs);
			}

			MUTEX_UNLOCK(ad);
		}
	}

	net->SendToTarget(t, pkt, pktlen, NET_RELIABLE);
}

#define BEGIN_EXTENDED(t, id) \
	byte pkt[1 + sizeof(struct ObjectMove)]; \
	struct ObjectMove *objm = (struct ObjectMove *)(pkt + 1); \
	aodata *ad; \
	lvzdata *node; \
	if (t->type == T_ARENA) \
		ad = P_ARENA_DATA(t->u.arena, aokey); \
	else if (t->type == T_PLAYER) \
		ad = P_ARENA_DATA(t->u.p->arena, aokey); \
	else \
		return; \
	pkt[0] = S2C_MOVEOBJECT; \
	MUTEX_LOCK(ad); \
	if ((node = getDataFromId(&ad->list, id))) \
	{ \
		memcpy((byte*)objm + 1, &node->current, sizeof(struct ObjectData))

#define END_EXTENDED(t, id) \
		if (t->type == T_ARENA) \
		{ \
			if (memcmp(&node->defaults, (byte*)objm + 1, sizeof(struct ObjectData))) \
			{ \
				if (!memcmp(&node->current, &node->defaults, sizeof(struct ObjectData))) \
					ad->ext_diffs++; \
			} \
			else if (memcmp(&node->current, &node->defaults, sizeof(struct ObjectData))) \
					ad->ext_diffs--; \
			memcpy(&node->current, (byte*)objm + 1, sizeof(struct ObjectData)); \
			lm->LogA(L_DRIVEL, "objects", t->u.arena, \
				"morphed object %i. saving %i objects", node->defaults.id, ad->ext_diffs); \
		} \
		net->SendToTarget(t, pkt, sizeof(pkt), NET_RELIABLE); \
	} \
	MUTEX_UNLOCK(ad)


void Move(const Target *t, int id, int x, int y, int rx, int ry)
{
	BEGIN_EXTENDED(t, id);

	*(u8*)objm = 0;
	objm->change_xy = 1;

	if ((objm->data.mapobj = node->defaults.mapobj))
	{
		objm->data.map_x = x;
		objm->data.map_y = y;
	}
	else
	{
		objm->data.scrn_x = x;
		objm->data.scrn_y = y;
		objm->data.rela_x = rx;
		objm->data.rela_y = ry;
	}

	END_EXTENDED(t, id);
}

void Image(const Target *t, int id, int image)
{
	BEGIN_EXTENDED(t, id);

	*(u8*)objm = 0;
	objm->change_image = 1;
	objm->data.image = image;

	END_EXTENDED(t, id);
}

void Layer(const Target *t, int id, int layer)
{
	BEGIN_EXTENDED(t, id);

	*(u8*)objm = 0;
	objm->change_layer = 1;
	objm->data.layer = layer;

	END_EXTENDED(t, id);
}

void Timer(const Target *t, int id, int time)
{
	BEGIN_EXTENDED(t, id);

	*(u8*)objm = 0;
	objm->change_time = 1;
	objm->data.time = time;

	END_EXTENDED(t, id);
}

void Mode(const Target *t, int id, int mode)
{
	BEGIN_EXTENDED(t, id);

	*(u8*)objm = 0;
	objm->change_mode = 1;
	objm->data.mode = mode;

	END_EXTENDED(t, id);
}

#pragma pack(pop)
