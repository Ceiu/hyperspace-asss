
# this builds the dbtool binary
$(call tobuild, dbtool$(EXE)): $(call tobuildo, statcodes) main/util.c main/dbtool.c
	$(CC) $(CFLAGS) $(DB_CFLAGS) $(LDFLAGS) \
		-DNODQ -DNOTREAP -DNOSTRINGCHUNK -DNOMPQUEUE -DNOMMAP \
		-o $@ $^ $(DB_LDFLAGS)

ifeq ($(have_bdb),yes)
BINS += $(call tobuild, dbtool$(EXE))
endif

# dist: public

