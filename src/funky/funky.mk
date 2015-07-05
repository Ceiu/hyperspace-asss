
funky_mods = brickwriter autowarp autoturret record sgcompat $(EXTRA_FUNKY_MODS)
funky_libs = $(ZLIB_LIB) -lm

$(eval $(call dl_template,funky))

# generated file for brickwriter
$(call tobuild, letters.inc): $(builddir) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font
	$(PYTHON) $(SCRIPTS)/processfont.py $(SCRIPTS)/banner.font $@

# dist: public

