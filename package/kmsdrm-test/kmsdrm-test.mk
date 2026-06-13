################################################################################
#
# kmsdrm-test
#
################################################################################

KMSDRM_TEST_VERSION = 1.0
KMSDRM_TEST_SITE = $(BR2_EXTERNAL_ARMIGA_PATH)/board/armiga/tests
KMSDRM_TEST_SITE_METHOD = local
KMSDRM_TEST_DEPENDENCIES = sdl3

define KMSDRM_TEST_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) \
		$(@D)/kmsdrm_test.c \
		-o $(@D)/kmsdrm_test \
		-I$(STAGING_DIR)/usr/include/SDL3 \
		-L$(STAGING_DIR)/usr/lib \
		$(TARGET_LDFLAGS) \
		-lSDL3
endef

define KMSDRM_TEST_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/kmsdrm_test \
		$(TARGET_DIR)/usr/bin/kmsdrm_test
endef

$(eval $(generic-package))
