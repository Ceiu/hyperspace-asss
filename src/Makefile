
#
# this is the makefile for asss. it works on linux, freebsd, cygwin, and
# mingw32/msys.
#
# the file "system.mk" might need tweaking by the user to specify
# locations of libraries and things.
#
# just running "make" will build asss with the default options
# (debug=yes, optmization=no, profiling=no).
#
# if you want an optimized build, use "make opt=yes".
# if you want a profiled build, use "make prof=yes".
# if you want a _non_debug build, use "make debug=no".
# if you want to link bdb statically, use "make link_db_statically=yes".
#

# this file contains system-specific definitions, including library and
# include file paths, and build options. generally, you should only
# modify system.mk, and not this file, to customize the behavior of the
# makefile.
have_system_mk := $(wildcard system.mk)
ifeq ($(strip $(have_system_mk)),)
$(error system.mk does not exist! Copy system.mk.dist to system.mk and edit it to your needs.)
endif
include system.mk

# this file adjusts the makefile depending on the operating system we're
# running on.
include os.mk

# apply build options
ifeq ($(debug),yes)
DEBUG_CFLAGS = -g -Wall
endif

ifeq ($(prof),yes)
PROF_FLAGS = -pg
endif

ifeq ($(opt),yes)
OPT_CFLAGS = -O2 -Wall
endif

# for older gcc, remove the -std option
CC = gcc -std=gnu99 -pipe
STD_CFLAGS = -I. -Iinclude -I$(BUILDDIR) $(EXTRA_INCLUDE_DIRS) -D_REENTRANT -D_GNU_SOURCE $(DASH_F_PIC)
CFLAGS = $(STD_CFLAGS) $(DEBUG_CFLAGS) $(PROF_FLAGS) $(OPT_CFLAGS)
LDFLAGS = $(DASH_PTHREAD) $(DASH_F_PIC) $(PROF_FLAGS)

# some macros to help with putting output in $(BUILDDIR)
tobuild = $(addprefix $(BUILDDIR)/,$(1))
tobuildo = $(addprefix $(BUILDDIR)/,$(addsuffix .o,$(1)))
builddir = $(call tobuild,.stamp)

# these object files might need to be linked into more than one file
UTIL_OBJS = util pathutil protutil rwlock app md5 $(W32COMPAT)

# this is the primary target
all: install

# this template makes it easy to build new dynamic libraries
define dl_template
# the main rule:
$$(call tobuild, $(1).$(SO)): $$(call tobuildo,$$($(1)_mods))
	$(CC) $(LDFLAGS) -shared -o $$@ $$^ $(SO_LDFLAGS) $$($(1)_libs)
# this macro can be used to refer to the library file.
# the optional $(2) is used to support conditional inclusion:
$(2)$(1)_DL := $$(call tobuild, $(1).$(SO))
# add the library file to the global list of libraries to build:
DLS += $$($(1)_DL)
# define a convenience alias for building a specific library:
$(1): $$($(2)$(1)_DL)
.PHONY: $(1)
endef

# these gets initialized to empty, and will be added to by expansions of
# dl_template, and other rules in the included makefile fragments.
DLS :=
BINS :=

# custom callbacks. must not have spaces in them due to the limitations of foreach.
# append a callback via CALLBACKS := $(CALLBACKS) function,param,param
CALLBACKS :=

# this grabs all the makefile fragments in our subdirectories
include */*.mk

# allow some fancy .mk files to execute things now that all .mk files have been included.
define executecb
$$(eval $$(call $(1)))
endef


$(foreach cb,$(CALLBACKS),$(eval $(call executecb,$(cb))))


# some of our phony targets
install: $(BINDIR)/.stamp install2

install2: $(BINS) $(DLS)
ifeq ($(symlink_bins),yes)
	$(LN) -sf $(foreach f,$^ $(wildcard */*.py) $(EXTRA_INSTALL_FILES),$(shell $(REALPATH) $(f))) $(BINDIR)
	@echo "Finished symlinking."
else
	$(INSTALL) $^ $(wildcard */*.py) $(EXTRA_INSTALL_FILES) $(BINDIR)
	@echo "Finished installing."
endif

clean:
	-rm -f $(BUILDDIR)/*


# a rule for creating directories
%/.stamp:
	-$(INSTALL) -d $(dir $@)
	-$(INSTALL) Makefile $@

# the basic rule for compiling .c files
%.o:
	$(CC) $(CFLAGS) -c -o $@ $<

# generating dependencies. automatic, but can be forced with "make deps".
$(call tobuild, deps.mk) deps: $(builddir) # */*.c */*.h
	$(PYTHON) $(SCRIPTS)/makedeps.py $(STD_CFLAGS) $(DB_CFLAGS) $(MYSQL_CFLAGS) $(PYTHON_CFLAGS) \
		*/*.c -P '$$(BUILDDIR)/' -o $(call tobuild, deps.mk)

-include $(call tobuild, deps.mk)


.PHONY: clean install install2 all deps

# dist: public

