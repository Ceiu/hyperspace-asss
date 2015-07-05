
/* dist: public */

/* app - the asss preprocessor
 *
 * handles selected features of the C preprocessor, including #include,
 * #define, #if[n]def, #else, #endif.
 *
 * initial ; or / for comments.
 *
 * macros are no longer expanded.
 */


#include <limits.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "defs.h"
#include "util.h"

#include "app.h"


#define DIRECTIVECHAR '#'
#define CONTINUECHAR '\\'
#define COMMENTCHARS "/;"

#define MAX_RECURSION_DEPTH 50


typedef struct FileEntry
{
	FILE *file;
	char *fname;
	int lineno;
	struct FileEntry *prev;
} FileEntry;


typedef struct IfBlock
{
	enum { in_if = 0, in_else = 1 } where;
	enum { is_false = 0, is_true = 1 }  cond;
	struct IfBlock *prev;
} IfBlock;


struct APPContext
{
	APPFileFinderFunc finder;
	APPReportErrFunc err;
	char *arena;
	FileEntry *file;
	IfBlock *ifs;
	int processing;
	HashTable *defs;
	int depth;
};



static void do_error(APPContext *ctx, const char *fmt, ...)
{
	va_list args;
	int len;
	char *buf;

	va_start(args, fmt);
	len = vsnprintf(NULL, 0, fmt, args);
	va_end(args);

	buf = alloca(len + 1);
	va_start(args, fmt);
	vsnprintf(buf, len+1, fmt, args);
	va_end(args);

	ctx->err(buf);
}


APPContext *APPInitContext(APPFileFinderFunc finder, APPReportErrFunc err, const char *arena)
{
	APPContext *ctx = amalloc(sizeof(*ctx));

	ctx->finder = finder;
	ctx->err = err;
	ctx->arena = arena ? astrdup(arena) : NULL;
	ctx->file = NULL;
	ctx->ifs = NULL;
	ctx->processing = 1;
	ctx->defs = HashAlloc();
	ctx->depth = 0;

	return ctx;
}


void APPFreeContext(APPContext *ctx)
{
	FileEntry *f = ctx->file;
	IfBlock *ifs = ctx->ifs;

	HashEnum(ctx->defs, hash_enum_afree, NULL);
	HashFree(ctx->defs);

	afree(ctx->arena);

	while (f)
	{
		FileEntry *of = f;
		if (f->file)
			fclose(f->file);
		afree(f->fname);
		f = f->prev;
		afree(of);
	}

	while (ifs)
	{
		IfBlock *oif = ifs;
		ifs = ifs->prev;
		afree(oif);
	}

	afree(ctx);
}


void APPAddDef(APPContext *ctx, const char *key, const char *val)
{
	char *oval = HashGetOne(ctx->defs, key);
	HashReplace(ctx->defs, key, astrdup(val));
	if (oval) afree(oval);
}

void APPRemoveDef(APPContext *ctx, const char *key)
{
	char *val = HashGetOne(ctx->defs, key);
	HashRemove(ctx->defs, key, val);
	if (val) afree(val);
}


static FileEntry *get_file(APPContext *ctx, const char *name)
{
	char fname[PATH_MAX];
	int ret;
	FILE *file;
	FileEntry *fe;

	/* try to find it */
	ret = ctx->finder(fname, sizeof(fname), ctx->arena, name);
	if (ret == -1)
	{
		do_error(ctx, "Can't find file for arena '%s', name '%s'", ctx->arena, name);
		return NULL;
	}

	/* try to open it */
	file = fopen(fname, "r");
	if (!file)
	{
		do_error(ctx, "Can't open file '%s' for reading", fname);
		return NULL;
	}

	/* package into struct */
	fe = amalloc(sizeof(*fe));
	fe->file = file;
	fe->fname = astrdup(fname);
	fe->lineno = 0;
	fe->prev = NULL;
	return fe;
}


void APPAddFile(APPContext *ctx, const char *name)
{
	FileEntry *fe;

	if (ctx->depth >= MAX_RECURSION_DEPTH)
	{
		do_error(ctx, "Maximum #include recursion depth reached while adding '%s'",
				name);
		return;
	}

	fe = get_file(ctx, name);
	if (fe)
	{
		if (!ctx->file)
			ctx->file = fe;
		else
		{
			FileEntry *tfe = ctx->file;
			while (tfe->prev)
				tfe = tfe->prev;
			tfe->prev = fe;
		}
		ctx->depth++;
	}
}


static void update_processing(APPContext *ctx)
{
	IfBlock *i = ctx->ifs;

	ctx->processing = 1;
	while (i)
	{
		if ((int)i->cond == (int)i->where)
			ctx->processing = 0;
		i = i->prev;
	}
}

static void push_if(APPContext *ctx, int cond)
{
	IfBlock *i = amalloc(sizeof(*i));
	i->cond = cond ? is_true : is_false;
	i->where = in_if;
	i->prev = ctx->ifs;
	ctx->ifs = i;
	update_processing(ctx);
}

static void pop_if(APPContext *ctx)
{
	if (ctx->ifs)
	{
		IfBlock *oif = ctx->ifs;
		ctx->ifs = ctx->ifs->prev;
		afree(oif);
	}
	else
		do_error(ctx, "No #if blocks to end (%s:%d)",
				ctx->file->fname, ctx->file->lineno);
	update_processing(ctx);
}

static void switch_if(APPContext *ctx)
{
	if (ctx->ifs)
	{
		if (ctx->ifs->where == in_if)
			ctx->ifs->where = in_else;
		else
			do_error(ctx, "Multiple #else directives (%s:%d)",
					ctx->file->fname, ctx->file->lineno);
	}
	else
		do_error(ctx, "Unexpected #else directive (%s:%d)",
				ctx->file->fname, ctx->file->lineno);
	update_processing(ctx);
}


static void handle_directive(APPContext *ctx, char *buf)
{
	char *t;

	/* skip DIRECTIVECHAR */
	buf++;

	/* first handle the stuff that you don't skip if !processing */
	if (!strncmp(buf, "ifdef", 5) || !strncmp(buf, "ifndef", 6))
	{
		int cond;
		/* trailing space, }, or ) */
		t = buf + strlen(buf) - 1;
		while (isspace(*t) || *t == '}' || *t == ')') *t-- = 0;
		/* leading space, }, ), or $ */
		t = buf + 6;
		while (isspace(*t) || *t == '{' || *t == '(') t++;
		/* check it */
		cond = (HashGetOne(ctx->defs, t) != NULL);
		push_if(ctx, buf[2] == 'd' ? cond : !cond);
	}
	else if (!strncmp(buf, "else", 4))
		switch_if(ctx);
	else if (!strncmp(buf, "endif", 5))
		pop_if(ctx);
	else
	{
		/* now handle the stuff valid while processing */
		if (!ctx->processing)
			return;

		if (!strncmp(buf, "define", 6))
		{
			char *key;

			/* leading space */
			t = buf + 7;
			while (isspace(*t)) t++;
			key = t;
			while (*t && !isspace(*t)) t++;
			if (*t)
			{
				/* define with a value */
				*t = 0;
				t++;
				while (isspace(*t)) t++;
				APPAddDef(ctx, key, t);
			}
			else
			{
				/* define with no value */
				APPAddDef(ctx, key, "1");
			}
		}
		else if (!strncmp(buf, "undef", 5))
		{
			t = buf + strlen(buf) - 1;
			while (isspace(*t)) *t-- = 0;
			/* leading space */
			t = buf + 5;
			while (isspace(*t)) t++;
			APPRemoveDef(ctx, t);
		}
		else if (!strncmp(buf, "include", 7))
		{
			FileEntry *fe;
			/* trailing space */
			t = buf + strlen(buf) - 1;
			while (isspace(*t) || *t == '"' || *t == '>') *t-- = 0;
			/* leading space */
			t = buf + 7;
			while (isspace(*t) || *t == '"' || *t == '<') t++;
			/* get file */
			fe = get_file(ctx, t);
			if (fe)
			{
				/* push on top of file stack */
				fe->prev = ctx->file;
				ctx->file = fe;
			}
		}
	}
}



#ifdef CFG_MAX_LINE
#define MAXLINE CFG_MAX_LINE
#else
#define MAXLINE 1024
#endif

/* returns false on eof */
int APPGetLine(APPContext *ctx, char *buf, int buflen)
{
	char mybuf[MAXLINE], *t;

	for (;;)
	{
		/* set to start of buf */
		t = mybuf;

		for (;;)
		{
			/* first find an actual line to process */
			for (;;)
			{
				if (ctx->file == NULL)
					return 0;

				if (fgets(t, MAXLINE - (t-mybuf), ctx->file->file) == NULL)
				{
					/* we hit eof on this file, pop it off and try next */
					FileEntry *of = ctx->file;
					ctx->file = of->prev;
					fclose(of->file);
					afree(of->fname);
					afree(of);
					ctx->depth--;
				}
				else
					break;
			}

			ctx->file->lineno++;
			RemoveCRLF(t);
			t = mybuf + strlen(mybuf);

			/* check for \ continued lines */
			if (strlen(mybuf) && t[-1] == CONTINUECHAR)
				t--;
			else
				break;
		}

		t = mybuf;

		/* check for directives */
		if (*t == DIRECTIVECHAR)
		{
			handle_directive(ctx, t);
			continue;
		}
		else
		{
			/* then comments and empty lines */
			while (*t && isspace(*t)) t++;
			if (*t == 0 || strchr(COMMENTCHARS, *t))
				continue;
		}

		/* here we have an actual line */
		/* if we're not processing, skip it */
		if (ctx->processing)
		{
			astrncpy(buf, mybuf, buflen);
			return 1;
		}
	}
}

