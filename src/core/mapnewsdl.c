
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#include "zlib.h"

#include "asss.h"


struct MapDownloadData
{
	u32 checksum, uncmplen, cmplen;
	int optional;
	byte *cmpmap;
	char filename[20];
};

struct data_locator
{
	Arena *arena;
	int lvznum, wantopt;
	u32 len;
};


/* GLOBALS */

local Imodman *mm;
local Iplayerdata *pd;
local Iconfig *cfg;
local Inet *net;
local Ilogman *lm;
local Iarenaman *aman;
local Imainloop *ml;
local Imapdata *mapdata;

local int dlkey;

local const char *cfg_newsfile;
local u32 newschecksum, cmpnewssize;
local byte *cmpnews;
local time_t newstime;


/* functions */

local int RefreshNewsTxt(void *dummy)
{
	MMapData *mmd;
	uLong csize;
	byte *cnews;

	mmd = MapFile(cfg_newsfile, FALSE);

	if (!mmd)
	{
		lm->Log(L_WARN,"<mapnewsdl> news file '%s' not found in current directory", cfg_newsfile);
		goto done1;
	}

	/* don't read if it hasn't been modified */
	if (mmd->lastmod == newstime)
		goto done2;

	newstime = mmd->lastmod;
	csize = (uLong)(1.0011 * mmd->len + 35);

	/* calculate crc on mmap'd map */
	newschecksum = crc32(crc32(0, Z_NULL, 0), mmd->data, mmd->len);

	/* allocate space for compressed version */
	cnews = amalloc(csize);

	/* set up packet header */
	cnews[0] = S2C_INCOMINGFILE;
	/* 16 bytes of zero for the name */

	/* compress the stuff! */
	compress(cnews+17, &csize, mmd->data, mmd->len);

	/* shrink the allocated memory */
	cnews = realloc(cnews, csize+17);
	if (!cnews)
	{
		lm->Log(L_ERROR,"<mapnewsdl> realloc failed in RefreshNewsTxt");
		goto done2;
	}
	cmpnewssize = csize+17;

	if (cmpnews) afree(cmpnews);
	cmpnews = cnews;
	lm->Log(L_DRIVEL,"<mapnewsdl> news file '%s' reread", cfg_newsfile);

done2:
	UnmapFile(mmd);
done1:
	return TRUE;
}


local u32 GetNewsChecksum(void)
{
	if (!cmpnews)
		RefreshNewsTxt(0);
	return newschecksum;
}


#include "packets/mapfname.h"

local void SendMapFilename(Player *p)
{
	struct MapFilename mf;
	LinkedList *dls;
	struct MapDownloadData *data;
	int len;
	Arena *arena;

	arena = p->arena;
	if (!arena) return;

	dls = P_ARENA_DATA(arena, dlkey);
	if (LLIsEmpty(dls))
	{
		lm->LogA(L_WARN, "mapnewsdl", arena, "missing map data");
		return;
	}

	/* allow vie clients that specifically ask for them to get all the
	 * lvz data, to support bots. */
	if (p->type == T_CONT || p->flags.want_all_lvz)
	{
		int idx = 0;
		Link *l;

		for (l = LLGetHead(dls); l; l = l->next)
		{
			data = l->data;
			if (!data->optional || p->flags.want_all_lvz)
			{
				strncpy(mf.files[idx].filename, data->filename, 16);
				mf.files[idx].checksum = data->checksum;
				mf.files[idx].size = data->cmplen;
				idx++;
			}
		}
		len = 1 + sizeof(mf.files[0]) * idx;
	}
	else
	{
		data = LLGetHead(dls)->data;
		strncpy(mf.files[0].filename, data->filename, 16);
		mf.files[0].checksum = data->checksum;
		len = 21;
	}

	mf.type = S2C_MAPFILENAME;
	net->SendToOne(p, (byte*)&mf, len, NET_RELIABLE);
}



local struct MapDownloadData * compress_map(const char *fname, int docomp)
{
	byte *cmap;
	uLong csize;
	const char *mapname;
	struct MapDownloadData *data;
	MMapData *mmd;

	data = amalloc(sizeof(*data));

	/* get basename */
	mapname = strrchr(fname, '/');
	if (!mapname)
		mapname = fname;
	else
		mapname++;
	astrncpy(data->filename, mapname, 20);

	mmd = MapFile(fname, FALSE);
	if (!mmd)
		goto fail1;

	/* calculate crc on mmap'd map */
	data->checksum = crc32(crc32(0, Z_NULL, 0), mmd->data, mmd->len);
	data->uncmplen = mmd->len;

	/* allocate space for compressed version */
	if (docomp)
		csize = (uLong)(1.0011 * mmd->len + 35);
	else
		csize = mmd->len + 17;

	cmap = malloc(csize);
	if (!cmap)
	{
		lm->Log(L_ERROR, "<mapnewsdl> malloc failed in compress_map for %s", fname);
		goto fail2;
	}

	/* set up packet header */
	cmap[0] = S2C_MAPDATA;
	strncpy((char*)(cmap+1), mapname, 16);

	if (docomp)
	{
		/* compress the stuff! */
		compress(cmap+17, &csize, mmd->data, mmd->len);
		csize += 17;

		/* shrink the allocated memory */
		data->cmpmap = realloc(cmap, csize);
		if (data->cmpmap == NULL)
		{
			lm->Log(L_ERROR, "<mapnewsdl> realloc failed in compress_map for %s", fname);
			goto fail3;
		}
	}
	else
	{
		/* just copy */
		memcpy(cmap+17, mmd->data, mmd->len);
		data->cmpmap = cmap;
	}

	data->cmplen = csize;

	if (csize > 256*1024)
		lm->Log(L_WARN, "<mapnewsdl> compressed map/lvz is bigger than 256k: %s", fname);

	UnmapFile(mmd);

	return data;

fail3:
	free(cmap);
fail2:
	UnmapFile(mmd);
fail1:
	afree(data);
	return NULL;
}


local void one_lvz_file(const char *fn, int optional, void *clos)
{
	struct MapDownloadData *data = compress_map(fn, FALSE);
	if (data)
	{
		data->optional = optional;
		LLAdd((LinkedList*)clos, data);
	}
}

local void aaction_work(void *clos)
{
	Arena *arena = clos;
	LinkedList *dls = P_ARENA_DATA(arena, dlkey);
	struct MapDownloadData *data = NULL;
	char fname[256];

	/* first add the map itself */
	/* cfghelp: General:Map, arena, string
	 * The name of the level file for this arena. */
	if (mapdata->GetMapFilename(arena, fname, sizeof(fname), NULL))
		data = compress_map(fname, TRUE);

	if (!data)
	{
		/* emergency hardcoded map: */
		byte emergencymap[] =
		{
			0x2a, 0x74, 0x69, 0x6e, 0x79, 0x6d, 0x61, 0x70,
			0x2e, 0x6c, 0x76, 0x6c, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x78, 0x9c, 0x63, 0x60, 0x60, 0x60, 0x04,
			0x00, 0x00, 0x05, 0x00, 0x02
		};

		lm->LogA(L_WARN, "mapnewsdl", arena, "can't load level file, falling back to tinymap.lvl");
		data = amalloc(sizeof(*data));
		data->checksum = 0x5643ef8a;
		data->uncmplen = 4;
		data->cmplen = sizeof(emergencymap);
		data->cmpmap  = amalloc(sizeof(emergencymap));
		memcpy(data->cmpmap, emergencymap, sizeof(emergencymap));
		astrncpy(data->filename, "tinymap.lvl", sizeof(data->filename));
	}

	LLAdd(dls, data);

	/* now look for lvzs */
	mapdata->EnumLVZFiles(arena, one_lvz_file, dls);

	aman->Unhold(arena);
}

local void ArenaAction(Arena *arena, int action)
{
	if (action == AA_CREATE)
	{
		ml->RunInThread(aaction_work, arena);
		aman->Hold(arena);
	}
	else if (action == AA_DESTROY)
	{
		LinkedList *dls = P_ARENA_DATA(arena, dlkey);
		Link *l;
		for (l = LLGetHead(dls); l; l = l->next)
		{
			struct MapDownloadData *data = l->data;
			afree(data->cmpmap);
			afree(data);
		}
		LLEmpty(dls);
	}
}



local struct MapDownloadData *get_map(Arena *arena, int lvznum, int wantopt)
{
	LinkedList *dls = P_ARENA_DATA(arena, dlkey);

	struct MapDownloadData *data;
	struct MapDownloadData *result = NULL;
	int i = 0;
	Link *link;
	FOR_EACH(dls, data, link)
	{
		/* skip over optional downloads if they're not wanted */
		if (data->optional && !wantopt)
			continue;

		if (i == lvznum)
		{
			result = data;
			break;
		}
		
		/* note that only downloads that the client gets count towards the
		 * lvznum (we broke out of this loop earlier if this download was
		 * optional and they didn't want it anyway) */
		++i;
	}
	return result;
}


local void get_data(void *clos, int offset, byte *buf, int needed)
{
	struct MapDownloadData *data;
	struct data_locator *dl = (struct data_locator*)clos;

	if (needed == 0)
	{
		lm->Log(L_DRIVEL, "<mapnewsdl> finished map/news download (transfer %p)", dl);
		afree(dl);
	}
	else if (dl->arena == NULL && dl->len == cmpnewssize)
		memcpy(buf, cmpnews + offset, needed);
	else if (dl->arena &&
	         (data = get_map(dl->arena, dl->lvznum, dl->wantopt)) &&
	         dl->len == data->cmplen)
		memcpy(buf, data->cmpmap + offset, needed);
	else if (buf)
		memset(buf, 0, needed);
}


local void PMapRequest(Player *p, byte *pkt, int len)
{
	struct data_locator *dl;
	Arena *arena = p->arena;
	int wantopt = p->flags.want_all_lvz;

	if (pkt[0] == C2S_MAPREQUEST)
	{
		struct MapDownloadData *data;
		unsigned short lvznum = (len == 3) ? pkt[1] | pkt[2]<<8 : 0;

		if (len != 1 && len != 3)
		{
			lm->LogP(L_MALICIOUS, "mapnewsdl", p, "bad map/LVZ req packet len=%i", len);
			return;
		}

		if (!arena)
		{
			lm->LogP(L_MALICIOUS, "mapnewsdl", p, "map request before entering arena");
			return;
		}

		data = get_map(arena, lvznum, wantopt);

		if (!data)
		{
			lm->LogP(L_WARN, "mapnewsdl", p, "can't find lvl/lvz %d", lvznum);
			return;
		}

		dl = amalloc(sizeof(*dl));

		dl->arena = arena;
		dl->lvznum = lvznum;
		dl->wantopt = wantopt;
		dl->len = data->cmplen;

		net->SendSized(p, dl, data->cmplen, get_data);
		lm->LogP(L_DRIVEL, "mapnewsdl", p, "sending map/lvz %d (%d bytes) (transfer %p)",
				lvznum, data->cmplen, dl);

		/* if we're getting these requests, it's too late to set their ship
		 * and team directly, we need to go through the in-game procedures */
		if (IS_STANDARD(p) &&
			(p->p_ship != SHIP_SPEC || p->p_freq != arena->specfreq))
		{
			struct Igame *game = mm->GetInterface(I_GAME, ALLARENAS);
			if (game) game->SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
			mm->ReleaseInterface(game);
		}
	}
	else if (pkt[0] == C2S_NEWSREQUEST)
	{
		if (len != 1)
		{
			lm->LogP(L_MALICIOUS, "mapnewsdl", p, "bad news req packet len=%i", len);
			return;
		}

		if (cmpnews)
		{
			dl = amalloc(sizeof(*dl));
			dl->arena = NULL;
			dl->len = cmpnewssize;
			net->SendSized(p, dl, cmpnewssize, get_data);
			lm->LogP(L_DRIVEL, "mapnewsdl", p, "sending news.txt (transfer %p)", dl);
		}
		else
			lm->Log(L_WARN, "<mapnewsdl> news request, but compressed news doesn't exist");
	}
}


#include "filetrans.h"

local void PUpdateRequest(Player *p, byte *pkt, int len)
{
	Ifiletrans *ft;

	if (len != 1)
	{
		lm->LogP(L_MALICIOUS, "mapnewsdl", p, "bad update req packet len=%i", len);
		return;
	}

	ft = mm->GetInterface(I_FILETRANS, ALLARENAS);
	if (ft && ft->SendFile(p, "clients/update.exe", "", FALSE) != MM_OK)
		lm->Log(L_WARN, "<mapnewsdl> update request, but clients/update.exe doesn't exist");
	mm->ReleaseInterface(ft);
}



/* interface */

local Imapnewsdl _int =
{
	INTERFACE_HEAD_INIT(I_MAPNEWSDL, "mapnewsdl")
	SendMapFilename, GetNewsChecksum
};

EXPORT const char info_mapnewsdl[] = CORE_MOD_INFO("mapnewsdl");

EXPORT int MM_mapnewsdl(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		/* get interface pointers */
		mm = mm_;
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);

		if (!net || !cfg || !lm || !ml || !aman || !pd || !mapdata) return MM_FAIL;

		dlkey = aman->AllocateArenaData(sizeof(LinkedList));
		if (dlkey == -1) return MM_FAIL;

		/* set up callbacks */
		net->AddPacket(C2S_UPDATEREQUEST, PUpdateRequest);
		net->AddPacket(C2S_MAPREQUEST, PMapRequest);
		net->AddPacket(C2S_NEWSREQUEST, PMapRequest);
		mm->RegCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		/* reread news every 5 min */
		/* cfghelp: General:NewsRefreshMinutes, global, int, def: 5
		 * How often to check for an updated news.txt. */
		ml->SetTimer(RefreshNewsTxt, 50,
				cfg->GetInt(GLOBAL, "General", "NewsRefreshMinutes", 5)
				* 60 * 100, NULL, NULL);

		/* cache some config data */
		/* cfghelp: General:NewsFile, global, string, def: news.txt
		 * The filename of the news file. */
		cfg_newsfile = cfg->GetStr(GLOBAL, "General", "NewsFile");
		if (!cfg_newsfile) cfg_newsfile = "news.txt";
		newstime = 0; cmpnews = NULL;

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;
		net->RemovePacket(C2S_UPDATEREQUEST, PUpdateRequest);
		net->RemovePacket(C2S_MAPREQUEST, PMapRequest);
		net->RemovePacket(C2S_NEWSREQUEST, PMapRequest);
		mm->UnregCallback(CB_ARENAACTION, ArenaAction, ALLARENAS);

		afree(cmpnews);
		ml->ClearTimer(RefreshNewsTxt, NULL);

		aman->FreeArenaData(dlkey);

		mm->ReleaseInterface(pd);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(ml);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(mapdata);
		return MM_OK;
	}
	return MM_FAIL;
}

