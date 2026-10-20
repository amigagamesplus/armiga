################################################################################
#
# armiga-splash
#
################################################################################
ARMIGA_SPLASH_VERSION = 1.0
ARMIGA_SPLASH_SITE = $(BR2_EXTERNAL_ARMIGA_PATH)/package/armiga-splash/src
ARMIGA_SPLASH_SITE_METHOD = local
ARMIGA_SPLASH_DEPENDENCIES = zlib
define ARMIGA_SPLASH_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" \
		Z_CFLAGS="-I$(STAGING_DIR)/usr/include" \
		Z_LIBS="-L$(STAGING_DIR)/usr/lib -lz" \
		-C $(@D)
endef
define ARMIGA_SPLASH_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/armiga-splash \
		$(TARGET_DIR)/usr/bin/armiga-splash
endef
$(eval $(generic-package))
