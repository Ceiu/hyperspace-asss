
scoring_mods = \
	persist stats statcodes points_kill points_flag \
	points_goal jackpot periodic points_periodic basicstats
scoring_libs = $(DB_LDFLAGS)

$(eval $(call dl_template,scoring,maybe_))

ifeq ($(have_bdb),yes)
DLS += $(maybe_scoring_DL)
endif

# special options for persist.o
$(call tobuild, persist.o): CFLAGS += $(DB_CFLAGS)

# dist: public

