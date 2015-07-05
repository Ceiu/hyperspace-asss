
database_mods = mysql aliasdb
database_libs = $(MYSQL_LDFLAGS) $(ZLIB_LIB)

$(eval $(call dl_template,database,maybe_))

ifeq ($(have_mysql),yes)
DLS += $(maybe_database_DL)
endif

# special rules for mysql.o
$(call tobuild, mysql.o): CFLAGS += $(MYSQL_CFLAGS)

# dist: public

