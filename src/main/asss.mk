
# this builds the main asss binary
$(call tobuild, asss$(EXE)): $(call tobuildo, main module cmod) \
		$(call tobuildo, $(UTIL_OBJS) $(INTERNAL_MODULES))
ifdef run_dlltool
	dlltool.exe --export-all-symbols -D asss.exe -e $(EXPORT_SYMBOLS) -l $(call tobuild,import.imp) $^
endif
	$(CC) $(LDFLAGS) $(EXPORT_SYMBOLS) -o $@ $^ $(DL_LIB) $(PTHREAD_LIB) $(ZLIB_LIB) -lm

BINS += $(call tobuild, asss$(EXE))

# dist: public

