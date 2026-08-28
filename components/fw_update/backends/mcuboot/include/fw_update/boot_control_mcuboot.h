#ifndef FW_UPDATE_BOOT_CONTROL_MCUBOOT_H
#define FW_UPDATE_BOOT_CONTROL_MCUBOOT_H

#include "fw_update/boot_control.h"
#include "fw_update/product_config.h"

/** Initializes MCUboot image 0 test-upgrade and confirmation control. */
void fw_update_boot_control_mcuboot_init(struct fw_update_boot_control* control,
                                         const struct fw_update_product_config* product_config);

#endif
