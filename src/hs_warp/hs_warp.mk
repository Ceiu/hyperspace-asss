
hs_warp_mods = hs_id hs_jumpengines hs_gates hs_transwarp hs_tw2 hs_interdict

$(eval $(call dl_template,hs_warp))

EXTRA_INCLUDE_DIRS := $(EXTRA_INCLUDE_DIRS) -Ihs_warp