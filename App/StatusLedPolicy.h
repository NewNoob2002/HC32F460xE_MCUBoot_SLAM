#ifndef APP_STATUSLEDPOLICY_H
#define APP_STATUSLEDPOLICY_H

#include "DataProc.h"
#include "bsp_status_led.h"

namespace app {
struct StatusLedPolicy {
    bsp_status_led_mode mode;
    bool override_enabled;
    uint8_t override_channels;
};

/** Main-loop composition policy. Update errors always take priority over manual override. */
StatusLedPolicy statusLedPolicy(dp::BatteryLedMode battery, usb_fw_update_activity activity, int32_t override_setting);
} // namespace app

#endif
