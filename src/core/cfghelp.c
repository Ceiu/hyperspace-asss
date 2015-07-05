
/* dist: public */

#include <string.h>

#include "asss.h"

#include "cfghelp.inc"

/* possible fixme: rewrite these two functions to use binary search */
local const struct section_help *find_sec(const char *sec)
{
	int i;
	for (i = 0; i < cfg_help_section_count; i++)
		if (strcasecmp(sec, cfg_help_sections[i].name) == 0)
			return &cfg_help_sections[i];
	return NULL;
}

local const struct key_help *find_key(const struct section_help *sh, const char *key)
{
	int i;
	for (i = 0; i < sh->keycount; i++)
		if (strcasecmp(key, sh->keys[i].name) == 0)
			return &sh->keys[i];
	return NULL;
}

local Icfghelp cfghelpint =
{
	INTERFACE_HEAD_INIT(I_CFGHELP, "cfghelp-1")
	cfg_help_sections,
	cfg_help_section_count,
	cfg_help_all_section_names,
	find_sec, find_key
};

EXPORT const char info_cfghelp[] = CORE_MOD_INFO("cfghelp");

EXPORT int MM_cfghelp(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm->RegInterface(&cfghelpint, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&cfghelpint, ALLARENAS))
			return MM_FAIL;
		return MM_OK;
	}
	return MM_FAIL;
}

