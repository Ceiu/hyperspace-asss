
pymod_mods = pymod
pymod_libs = $(PYTHON_LDFLAGS)

$(eval $(call dl_template,pymod,maybe_))

ifeq ($(have_python),yes)
DLS += $(maybe_pymod_DL)
endif


# rule to generate files
$(call tobuild, py_constants.inc py_callbacks.inc py_interfaces.inc py_types.inc py_include.inc): \
		$(SCRIPTS)/pymod-process.py $(PYMOD_HEADERS)
	$(PYTHON) $(SCRIPTS)/pymod-process.py $(dir $@) $(PYMOD_HEADERS)

# special options for pymod.o
$(call tobuild, pymod.o): CFLAGS += $(PYTHON_CFLAGS)
ifneq ($(GCC_WITH_CPYCHECKER),)
$(call tobuild, pymod.o): CC := $(GCC_WITH_CPYCHECKER) -std=gnu99 -pipe --maxtrans 2048
endif

# dist: public

