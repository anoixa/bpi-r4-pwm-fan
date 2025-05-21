include $(TOPDIR)/rules.mk

PKG_NAME:=bpi-r4-pwm-fan
PKG_VERSION:=1.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=PWM Fan Speed Controller for BPI-R4
endef

define Package/$(PKG_NAME)/description
  A simple PWM fan controller with procd integration.
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_LDFLAGS) \
		-o $(PKG_BUILD_DIR)/fan-speed $(CURDIR)/src/fan-speed.c
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/fan-speed $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/etc/init.d/fan-speed $(1)/etc/init.d/fan-speed
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
