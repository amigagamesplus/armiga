################################################################################
#
# btop
#
################################################################################

BTOP_VERSION = 1.4.0
BTOP_SITE = $(call github,aristocratos,btop,v$(BTOP_VERSION))
BTOP_LICENSE = Apache-2.0
BTOP_LICENSE_FILES = LICENSE

BTOP_BUILD_OPTS = \
	MAKEFLAGS="$(PARALLEL_JOBS:%=-j%)" \
	CXX="$(TARGET_CXX)" \
	STRIP="true" \
	PREFIX=/usr \
	DESTDIR=$(TARGET_DIR)

define BTOP_BUILD_CMDS
	$(MAKE) $(BTOP_BUILD_OPTS) -C $(@D)
endef

define BTOP_INSTALL_TARGET_CMDS
	$(MAKE) $(BTOP_BUILD_OPTS) -C $(@D) install
endef

$(eval $(generic-package))
