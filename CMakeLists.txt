# SPDX-License-Identifier: MIT

if(CONFIG_NOCFREE_KSCAN_PCA9555 OR CONFIG_NOCFREE_RECOVERY_CDC_1200_TOUCH)
  zephyr_library()
  zephyr_library_sources_ifdef(CONFIG_NOCFREE_KSCAN_PCA9555 drivers/kscan/kscan_pca9555.c)
  zephyr_library_sources_ifdef(CONFIG_NOCFREE_RECOVERY_CDC_1200_TOUCH src/cdc_1200_touch.c)
endif()
