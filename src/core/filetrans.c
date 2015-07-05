
/* dist: public */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <paths.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>
#endif


#include "asss.h"
#include "filetrans.h"
#include "packets/requestfile.h"

struct upload_data
{
	FILE *fp;
	char *fname;
	void (*uploaded)(const char *filename, void *clos);
	void *clos;
	const char *work_dir;
};

struct download_data
{
	FILE *fp;
	char *fname, *path;
	/* path should only be set if we want to delete the file when we're
	 * done. */
};


local Imodman *mm;
local Inet *net;
local Ilogman *lm;
local Icapman *capman;
local Iplayerdata *pd;

local int udkey;

/* protects work_dir. possibly other fields later. */
local pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)


local void cleanup_ud(Player *p, int success)
{
	struct upload_data *ud = PPDATA(p, udkey);

	if (ud->fp)
	{
		fclose(ud->fp);
		ud->fp = NULL;
	}

	if (success)
	{
		if (ud->uploaded)
			ud->uploaded(ud->fname, ud->clos);
		else if (ud->fname)
			remove(ud->fname);
	}
	else
	{
		if (ud->uploaded)
			ud->uploaded(NULL, ud->clos);

		if (ud->fname)
			remove(ud->fname);
	}

	afree(ud->fname);
	ud->fname = NULL;
	ud->uploaded = NULL;
	ud->clos = NULL;
}


local void p_inc_file(Player *p, byte *data, int len)
{
	struct upload_data *ud = PPDATA(p, udkey);
	char fname[] = "tmp/uploaded-XXXXXX";
	int fd;
	int ok = FALSE;

	if (capman && !capman->HasCapability(p, CAP_UPLOADFILE))
		return;

	fd = mkstemp(fname);
	if (fd >= 0)
	{
		ud->fname = astrdup(fname);
		ud->fp = fdopen(fd, "wb");
		if (ud->fp &&
		    fwrite(data+17, len-17, 1, ud->fp) == 1)
			ok = TRUE;
	}
	else
		lm->LogP(L_WARN, "filetrans", p, "can't create temp file for upload");

	cleanup_ud(p, ok);
}


local void sized_p_inc_file(Player *p, byte *data, int len, int offset, int totallen)
{
	struct upload_data *ud = PPDATA(p, udkey);

	if (offset == -1)
	{
		/* canceled */
		cleanup_ud(p, FALSE);
	}
	else if (offset == 0 && ud->fp == NULL && len > 17)
	{
		if (!capman || capman->HasCapability(p, CAP_UPLOADFILE))
		{
			int fd;
			char fname[] = "tmp/uploaded-XXXXXX";

			fd = mkstemp(fname);
			if (fd < 0)
			{
				lm->LogP(L_WARN, "filetrans", p, "can't create temp file for upload");
				return;
			}

			lm->LogP(L_INFO, "filetrans", p, "accepted file for upload (to '%s')", fname);

			ud->fname = astrdup(fname);
			ud->fp = fdopen(fd, "wb");
			if (ud->fp)
				fwrite(data+17, len-17, 1, ud->fp);
		}
		else
			lm->LogP(L_INFO, "filetrans", p, "denied file upload");
	}
	else if (offset > 0 && ud->fp)
	{
		if (offset < totallen)
			fwrite(data, len, 1, ud->fp);
		else
		{
			lm->LogP(L_INFO, "filetrans", p, "completed upload");
			cleanup_ud(p, TRUE);
		}
	}
}


local void get_data(void *clos, int offset, byte *buf, int needed)
{
	struct download_data *dd = (struct download_data*)clos;

	if (needed == 0)
	{
		/* done */
		lm->Log(L_INFO, "<filetrans> completed send of '%s'", dd->fname);
		fclose(dd->fp);
		if (dd->path)
		{
			remove(dd->path);
			afree(dd->path);
		}
		afree(dd->fname);
		afree(dd);
	}
	else if (offset == 0 && needed >= 17)
	{
		*buf++ = S2C_INCOMINGFILE;
		strncpy((char*)buf, dd->fname, 16);
		buf += 16;
		fread(buf, needed - 17, 1, dd->fp);
	}
	else if (offset > 0)
	{
		fread(buf, needed, 1, dd->fp);
	}
}


local int SendFile(Player *p, const char *path, const char *fname, int delafter)
{
	struct download_data *dd;
	struct stat st;
	FILE *fp;
	int ret;

	ret = stat(path, &st);
	if (ret == -1)
	{
		lm->Log(L_WARN, "<filetrans> can't stat '%s': %s", path, strerror(errno));
		return MM_FAIL;
	}

	fp = fopen(path, "rb");
	if (!fp)
	{
		lm->Log(L_WARN, "<filetrans> can't open '%s' for reading: %s", path, strerror(errno));
		return MM_FAIL;
	}

	dd = amalloc(sizeof(*dd));
	dd->fp = fp;
	dd->fname = astrdup(fname);
	dd->path = NULL;

	ret = net->SendSized(p, dd, st.st_size + 17, get_data);
	if (ret == MM_FAIL)
	{
		afree(dd);
		errno = -1;
		return ret;
	}

	lm->LogP(L_INFO, "filetrans", p, "sending '%s' (as '%s')", path, fname);

	if (delafter)
#ifdef WIN32
		dd->path = astrdup(path);
#else
		/* on unix, we can unlink now because we keep it open */
		remove(path);
#endif
	return MM_OK;
}

local int RequestFile(Player *p, const char *path,
		void (*uploaded)(const char *filename, void *clos), void *clos)
{
	struct upload_data *ud = PPDATA(p, udkey);
	struct S2CRequestFile pkt;

	if (ud->fp || ud->fname || !IS_STANDARD(p))
		return MM_FAIL;

	ud->fp = NULL;
	ud->fname = NULL;
	ud->uploaded = uploaded;
	ud->clos = clos;

	memset(&pkt, 0, sizeof(pkt));

	pkt.type = S2C_REQUESTFORFILE;
	astrncpy(pkt.path, path, 256);
	astrncpy(pkt.fname, "unused-field", 16);

	net->SendToOne(p, (byte*)&pkt, sizeof(pkt), NET_RELIABLE);

	lm->LogP(L_INFO, "filetrans", p, "requesting file '%s'", path);
	if (strstr(path, ".."))
		lm->LogP(L_WARN, "filetrans", p, "sent file request with '..'");

	return MM_OK;
}


local void GetWorkingDirectory(Player *p, char *dest, int destlen)
{
	struct upload_data *ud = PPDATA(p, udkey);
	LOCK();
	astrncpy(dest, ud->work_dir, destlen);
	UNLOCK();
}

local void SetWorkingDirectory(Player *p, const char *path)
{
	struct upload_data *ud = PPDATA(p, udkey);
	LOCK();
	afree(ud->work_dir);
	ud->work_dir = astrdup(path);
	UNLOCK();
}


local void paction(Player *p, int action)
{
	struct upload_data *ud = PPDATA(p, udkey);
	LOCK();
	if (action == PA_CONNECT)
	{
		ud->work_dir = astrdup(".");
	}
	else if (action == PA_DISCONNECT)
	{
		cleanup_ud(p, FALSE);
		afree(ud->work_dir);
	}
	UNLOCK();
}


/* interface */

local Ifiletrans _int =
{
	INTERFACE_HEAD_INIT(I_FILETRANS, "filetrans")
	SendFile, RequestFile,
	GetWorkingDirectory, SetWorkingDirectory
};

EXPORT const char info_filetrans[] = CORE_MOD_INFO("filetrans");

EXPORT int MM_filetrans(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;
		net = mm->GetInterface(I_NET, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!net || !lm) return MM_FAIL;

		udkey = pd->AllocatePlayerData(sizeof(struct upload_data));
		if (udkey == -1) return MM_FAIL;

		net->AddPacket(C2S_UPLOADFILE, p_inc_file);
		net->AddSizedPacket(C2S_UPLOADFILE, sized_p_inc_file);

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		mm->RegInterface(&_int, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&_int, ALLARENAS))
			return MM_FAIL;

		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);

		net->RemovePacket(C2S_UPLOADFILE, p_inc_file);
		net->RemoveSizedPacket(C2S_UPLOADFILE, sized_p_inc_file);

		pd->FreePlayerData(udkey);
		mm->ReleaseInterface(net);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(capman);
		mm->ReleaseInterface(pd);
		return MM_OK;
	}
	return MM_FAIL;
}

