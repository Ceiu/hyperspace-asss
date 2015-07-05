hscore_mods = hscore_buysell hscore_commands hscore_database \
	hscore_items hscore_money hscore_mysql hscore_rewards\
	hscore_spawner hscore_spawner2 hscore_storeman \
	hscore_wepevents hscore_weapons hscore_enforcer hscore_balancer \
  hscore_advitemprops


hscore_libs = $(MYSQL_LDFLAGS) -lm

$(eval $(call dl_template,hscore))

$(call tobuild, hscore_mysql.o): CFLAGS += $(MYSQL_CFLAGS)

EXTRA_INCLUDE_DIRS := $(EXTRA_INCLUDE_DIRS) -Ihscore
