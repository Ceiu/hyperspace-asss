obscene_mods = obscene_regex obscene_chat
obscene_libs = -lpcre

$(eval $(call dl_template,obscene))