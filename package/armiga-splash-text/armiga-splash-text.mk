################################################################################
#
# armiga-splash-text
#
################################################################################
ARMIGA_SPLASH_TEXT_VERSION = 1.0
ARMIGA_SPLASH_TEXT_SITE = $(BR2_EXTERNAL_ARMIGA_PATH)/package/armiga-splash-text/src
ARMIGA_SPLASH_TEXT_SITE_METHOD = local
ARMIGA_SPLASH_TEXT_DEPENDENCIES = sdl3 sdl3_ttf sdl3_image
define ARMIGA_SPLASH_TEXT_BUILD_CMDS
	$(MAKE) CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS) -I$(TARGET_DIR)/usr/include" \
		LDFLAGS="$(TARGET_LDFLAGS) -L$(STAGING_DIR)/usr/lib -lSDL3 -lSDL3_ttf -lSDL3_image" \
		-C $(@D)
endef
define ARMIGA_SPLASH_TEXT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/armiga-splash-text \
		$(TARGET_DIR)/usr/bin/armiga-splash-text
endef
$(eval $(generic-package))
