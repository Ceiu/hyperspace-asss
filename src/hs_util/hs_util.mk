
hs_util_mods = kill kill_cmd menu selfpos selfpos_cmd \
	formula parser lexer formula_cmd

hs_util_libs = -lm

$(eval $(call dl_template,hs_util))

EXTRA_INCLUDE_DIRS := $(EXTRA_INCLUDE_DIRS) -Ihs_util

ifneq ($(strip $(wildcard */akd_lag.h)),)
$(call tobuild, selfpos.o): CFLAGS += -DUSE_AKD_LAG
endif

# only try to build the .c files if they don't exist
have_parserc := $(wildcard hs_util/parser.c)
have_lexerc := $(wildcard hs_util/lexer.c)

ifeq ($(strip $(have_parserc)),)
ifeq ($(strip $(have_lexerc)),)

# how to build the formula source files
$(call tobuild, parser.c): hs_util/parser.y
	bison -d -o $@ $<
$(call tobuild, lexer.c): hs_util/lexer.l
	flex --fast -o $@ $<

# make sure the parser/lexer ask to build their c files
$(BUILDDIR)/parser.o: CFLAGS += -Wno-unused
$(call tobuild, parser.o): $(BUILDDIR)/parser.c
$(BUILDDIR)/lexer.o: CFLAGS += -Wno-unused
$(call tobuild, lexer.o): $(BUILDDIR)/lexer.c
endif
endif

