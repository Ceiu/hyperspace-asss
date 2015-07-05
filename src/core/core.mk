
# these modules get compiled into the asss binary
INTERNAL_MODULES = \
	config prng player core logman idle \
	mainloop net enc_null enc_vie arenaman mapdata \
	mapnewsdl clientset capman lagdata lagaction \
	bw_default bw_nolimit freqman \
	log_file log_console log_sysop log_staff \
	game game_timer chat flagcore balls \
	banners bricks objects messages koth enf_lockspec \
	cmdman playercmd admincmd watchdamage buy help notify \
	directory billing billing_ssc pwcache \
	cfghelp filetrans quickfix \
	freqowners arenaperm auth_prefix fake chatnet \
	enf_legalship enf_shipcount \
	ap_multipub sendfile auth_ban auth_file obscene \
	enf_shipchange enf_flagwin \
	deadlock redirect $(unixsignal)

# generated file for mapdata
$(call tobuild, sparse.inc): $(builddir) $(SCRIPTS)/gensparse.py $(SCRIPTS)/sparse_params.py
	$(PYTHON) $(SCRIPTS)/gensparse.py $@ $(SCRIPTS)/sparse_params.py

# generated file for cfghelp
$(call tobuild, cfghelp.inc): $(builddir) $(SCRIPTS)/extract-cfg-docs.py
	$(PYTHON) $(SCRIPTS)/extract-cfg-docs.py -c $@ */*.c */*.py core/clientset.def

# used by unixsignal
BINS += core/backtrace

# dist: public

