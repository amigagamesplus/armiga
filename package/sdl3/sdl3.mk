################################################################################
#
# sdl3
#
################################################################################
SDL3_VERSION = 3.4.14
SDL3_SITE = https://github.com/libsdl-org/SDL/releases/download/release-$(SDL3_VERSION)
SDL3_SOURCE = SDL3-$(SDL3_VERSION).tar.gz
SDL3_LICENSE = Zlib
SDL3_LICENSE_FILES = LICENSE.txt
SDL3_INSTALL_STAGING = YES
SDL3_INSTALL_TARGET = YES
SDL3_DEPENDENCIES = host-pkgconf libdrm alsa-lib
SDL3_CONF_OPTS += -DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -I$(STAGING_DIR)/usr/include/libdrm"
SDL3_CONF_ENV += PKG_CONFIG_PATH="$(STAGING_DIR)/usr/lib/pkgconfig"
SDL3_CONF_OPTS = \
	-DSDL_TEST_LIBRARY=OFF \
	-DSDL_EXAMPLES=OFF \
	-DSDL_SHARED=ON \
	-DSDL_STATIC=OFF \
	-DSDL_DISABLE_INSTALL_DOCS=ON \
	-DSDL_INSTALL_TESTS=OFF \
	-DSDL_WAYLAND=OFF \
	-DSDL_X11=OFF \
	-DSDL_OFFSCREEN=OFF \
	-DSDL_VULKAN=OFF \
	-DSDL_OPENGL=OFF \
	-DSDL_OPENGLES=ON \
	-DSDL_KMSDRM=ON \
	-DSDL_ALLOW_NO_DISPLAY_DRIVER=ON \
	-DSDL_KMSDRM_SHARED=OFF \
	-DSDL_ALSA=ON \
	-DSDL_DBUS=OFF \
	-DSDL_IBUS=OFF


define SDL3_FIX_KMSDRM_CHECK
	sed -i 's/if(NOT SDL_UNIX_CONSOLE_BUILD)/if(FALSE)/g' \
		$(@D)/cmake/macros.cmake
endef
SDL3_POST_EXTRACT_HOOKS += SDL3_FIX_KMSDRM_CHECK

$(eval $(cmake-package))
