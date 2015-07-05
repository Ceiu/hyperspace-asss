
hs_akd_mods = hs_field hs_turretwar antisafelame akd_lag hs_powerball hs_greens log_file_dated suicides clocks hs_stats

$(eval $(call dl_template,hs_akd))

EXTRA_INCLUDE_DIRS := $(EXTRA_INCLUDE_DIRS) -Ihs_akd
