
hs_d1_mods = race log_smod hs_register squadperm hs_itemregen

EXTRA_INCLUDE_DIRS := $(EXTRA_INCLUDE_DIRS) -Ihscore -Ihs_akd

$(eval $(call dl_template,hs_d1))
