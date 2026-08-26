#ifndef FW_UPDATE_BOOT_CONTROL_H
#define FW_UPDATE_BOOT_CONTROL_H

#include "fw_update/error.h"

/** Backend operations that change MCUboot image state but never image bytes. */
struct fw_update_boot_control_ops {
    enum fw_update_result (*request_test_upgrade)(void* context);
    enum fw_update_result (*confirm_running_image)(void* context);
};

/** A statically configured MCUboot state-control instance. */
struct fw_update_boot_control {
    const struct fw_update_boot_control_ops* ops;
    void* context;
};

/** Marks the completed candidate image for a test upgrade on the next boot. */
enum fw_update_result fw_update_boot_control_request_test_upgrade(const struct fw_update_boot_control* control);

/** Confirms the currently running image after Application health checks pass. */
enum fw_update_result fw_update_boot_control_confirm_running_image(const struct fw_update_boot_control* control);

#endif
