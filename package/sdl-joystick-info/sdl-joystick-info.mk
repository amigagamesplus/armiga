################################################################################
#
# sdl-joystick-info
#
################################################################################
SDL_JOYSTICK_INFO_VERSION = 1.0
SDL_JOYSTICK_INFO_SITE = $(BR2_EXTERNAL_ARMIGA_PATH)/board/armiga/tests
SDL_JOYSTICK_INFO_SITE_METHOD = local
SDL_JOYSTICK_INFO_DEPENDENCIES = sdl3
define SDL_JOYSTICK_INFO_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		$(@D)/sdl_joystick_info.c \
		-o $(@D)/sdl_joystick_info \
		-I$(STAGING_DIR)/usr/include/SDL3 \
		-L$(STAGING_DIR)/usr/lib \
		$(TARGET_LDFLAGS) \
		-lSDL3
endef
define SDL_JOYSTICK_INFO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/sdl_joystick_info \
		$(TARGET_DIR)/usr/bin/sdl_joystick_info
endef
$(eval $(generic-package))
