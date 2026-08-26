#include "fw_update/boot_control_mcuboot.h"

#include <stddef.h>

#include "bootutil/bootutil_public.h"

static enum fw_update_result request_test_upgrade(void* context) {
    (void)context;
    return boot_set_pending_multi(0, 0) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_BOOT_CONTROL;
}

static enum fw_update_result confirm_running_image(void* context) {
    (void)context;
    return boot_set_confirmed_multi(0) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_BOOT_CONTROL;
}

static const struct fw_update_boot_control_ops ops = {
    .request_test_upgrade = request_test_upgrade,
    .confirm_running_image = confirm_running_image,
};

void fw_update_boot_control_mcuboot_init(struct fw_update_boot_control* control) {
    if (control == NULL)
        return;
    control->ops = &ops;
    control->context = NULL;
}
