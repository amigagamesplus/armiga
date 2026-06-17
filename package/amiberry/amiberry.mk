################################################################################
#
# amiberry
#
################################################################################
AMIBERRY_VERSION = v8.2.0
AMIBERRY_SITE = https://github.com/BlitterStudio/amiberry
AMIBERRY_SITE_METHOD = git
AMIBERRY_GIT_SUBMODULES = YES
AMIBERRY_LICENSE = GPL-3.0
AMIBERRY_LICENSE_FILES = LICENSE
AMIBERRY_INSTALL_STAGING = NO
AMIBERRY_INSTALL_TARGET = YES
AMIBERRY_DEPENDENCIES = sdl3 sdl3_image flac libpng mpg123 zstd libcurl \
	libpcap enet libmpeg2 libserialport

AMIBERRY_CONF_OPTS = \
	-DUSE_GLES=ON \
	-DUSE_OPENGL=OFF \
	-DUSE_VULKAN=OFF \
	-DUSE_DBUS=OFF \
	-DUSE_PORTMIDI=OFF \
	-DUSE_PPC=OFF \
	-DUSE_QEMU_PPC=OFF \
	-DUSE_UAENET_TAP=OFF \
	-DUSE_GPIOD=OFF \
	-Dnlohmann_json_DIR=/usr/lib/cmake/nlohmann_json
	-DCMAKE_BUILD_TYPE=Release


define AMIBERRY_FIX_INTREE_BUILD
	sed -i '/In-tree builds are not supported/d' $(@D)/CMakeLists.txt
endef
AMIBERRY_POST_EXTRACT_HOOKS += AMIBERRY_FIX_INTREE_BUILD

$(eval $(cmake-package))
