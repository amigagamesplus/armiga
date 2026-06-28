################################################################################
#
# armiga-launcher
#
################################################################################
ARMIGA_LAUNCHER_VERSION = 1.0
ARMIGA_LAUNCHER_SITE = $(BR2_EXTERNAL_ARMIGA_PATH)/package/armiga-launcher/src
ARMIGA_LAUNCHER_SITE_METHOD = local
ARMIGA_LAUNCHER_DEPENDENCIES = sdl3 sdl3_ttf
define ARMIGA_LAUNCHER_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY) --define-prefix" \
		PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig" \
		-C $(@D)
endef
define ARMIGA_LAUNCHER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/armiga-launcher \
		$(TARGET_DIR)/usr/bin/armiga-launcher
endef
$(eval $(generic-package))
