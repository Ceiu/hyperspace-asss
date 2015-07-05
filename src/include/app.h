
/* dist: public */

#ifndef __APP_H
#define __APP_H

typedef struct APPContext APPContext;

typedef int (*APPFileFinderFunc)(char *dest, int destlen,
		const char *arena, const char *name);
typedef void (*APPReportErrFunc)(const char *error);


APPContext *APPInitContext(
		APPFileFinderFunc finder,
		APPReportErrFunc err,
		const char *arena);
void APPFreeContext(APPContext *ctx);

void APPAddDef(APPContext *ctx, const char *key, const char *val);
void APPRemoveDef(APPContext *ctx, const char *key);

void APPAddFile(APPContext *ctx, const char *name);

/* returns false on eof */
int APPGetLine(APPContext *ctx, char *buf, int buflen);

#endif

