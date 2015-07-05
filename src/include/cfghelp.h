
/* dist: public */

#ifndef __CFGHELP_H
#define __CFGHELP_H

struct key_help
{
	const char *name, *loc, *type, *range, *mod, *def, *helptext;
};

struct section_help
{
	const char *name;
	const int keycount;
	const struct key_help *keys;
	const char *all_key_names;
};


#define I_CFGHELP "cfghelp-2"

typedef struct Icfghelp
{
	INTERFACE_HEAD_DECL

	/* the raw data */
	const struct section_help *sections;
	const int section_count;
	const char *all_section_names;

	/* utility functions */
	const struct section_help *(*find_sec)(const char *secname);
	const struct key_help *(*find_key)
		(const struct section_help *sh, const char *keyname);
} Icfghelp;

#endif

