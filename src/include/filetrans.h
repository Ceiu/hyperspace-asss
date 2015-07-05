
/* dist: public */

#ifndef __FILETRANS_H
#define __FILETRANS_H


#define I_FILETRANS "filetrans-3"

typedef struct Ifiletrans
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	int (*SendFile)(Player *p, const char *path, const char *fname, int delafter);
	/* pyint: player, string, string, int -> int */

	/* uploaded will get called when the file is done being uploaded.
	 * filename will be the name of the uploaded file. if filename == NULL,
	 * there was an error and you should clean up any allocated memory
	 * in clos. an error return from RequestFile means you should clean
	 * up anything immediately, as uploaded won't be called. */
	int (*RequestFile)(Player *p, const char *path,
			void (*uploaded)(const char *filename, void *clos), void *clos);
	/* pyint: player, string, (zstring, clos -> void) dynamic failval MM_FAIL, clos -> int */

	void (*GetWorkingDirectory)(Player *p, char *dest, int destlen);
	void (*SetWorkingDirectory)(Player *p, const char *path);
} Ifiletrans;


#endif

