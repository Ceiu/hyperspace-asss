
gamestats_mods = gamestats


$(eval $(call dl_template,gamestats))

EXTRA_INCLUDE_DIRS := $(EXTRA_INCLUDE_DIRS) -Igamestats
