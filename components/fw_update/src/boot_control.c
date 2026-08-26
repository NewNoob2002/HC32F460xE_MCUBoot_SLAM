#include "fw_update/boot_control.h"

#include <stddef.h>

enum fw_update_result fw_update_boot_control_request_test_upgrade(const struct fw_update_boot_control* control) {
    if (control == NULL || control->ops == NULL || control->ops->request_test_upgrade == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return control->ops->request_test_upgrade(control->context);
}

enum fw_update_result fw_update_boot_control_confirm_running_image(const struct fw_update_boot_control* control) {
    if (control == NULL || control->ops == NULL || control->ops->confirm_running_image == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return control->ops->confirm_running_image(control->context);
}
