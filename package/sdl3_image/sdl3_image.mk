################################################################################
#
# sdl3_image
#
################################################################################
SDL3_IMAGE_VERSION = 3.2.4
SDL3_IMAGE_SITE = https://github.com/libsdl-org/SDL_image/releases/download/release-$(SDL3_IMAGE_VERSION)
SDL3_IMAGE_SOURCE = SDL3_image-$(SDL3_IMAGE_VERSION).tar.gz
SDL3_IMAGE_LICENSE = Zlib
SDL3_IMAGE_LICENSE_FILES = LICENSE.txt
SDL3_IMAGE_INSTALL_STAGING = YES
SDL3_IMAGE_INSTALL_TARGET = YES
SDL3_IMAGE_DEPENDENCIES = sdl3 libpng

SDL3_IMAGE_CONF_OPTS = \
	-DSDLIMAGE_SAMPLES=OFF \
	-DSDLIMAGE_TESTS=OFF \
	-DSDLIMAGE_INSTALL_MAN=OFF \
	-DSDLIMAGE_AVIF=OFF \
	-DSDLIMAGE_WEBP=OFF \
	-DSDLIMAGE_JXL=OFF \
	-DSDLIMAGE_TIF=OFF

$(eval $(cmake-package))
