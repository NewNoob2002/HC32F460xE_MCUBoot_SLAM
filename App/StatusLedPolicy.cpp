#include "StatusLedPolicy.h"

namespace app {
StatusLedPolicy statusLedPolicy(dp::BatteryLedMode battery, usb_fw_update_activity activity, int32_t override_setting) {
    StatusLedPolicy policy{BSP_STATUS_LED_MODE_APP_UPDATER, false, 0U};
    switch (battery) {
        case dp::BatteryLedMode::Updater:
            break;
        case dp::BatteryLedMode::Charging:
            policy.mode = BSP_STATUS_LED_MODE_APP_CHARGING;
            break;
        case dp::BatteryLedMode::Full:
            policy.mode = BSP_STATUS_LED_MODE_APP_FULL;
            break;
        case dp::BatteryLedMode::Low:
            policy.mode = BSP_STATUS_LED_MODE_APP_LOW;
            break;
        case dp::BatteryLedMode::Critical:
            policy.mode = BSP_STATUS_LED_MODE_APP_CRITICAL;
            break;
        case dp::BatteryLedMode::NoPack:
            policy.mode = BSP_STATUS_LED_MODE_APP_NO_PACK;
            break;
    }
    if (activity == USB_FW_UPDATE_ACTIVITY_ACTIVE)
        policy.mode = BSP_STATUS_LED_MODE_UPDATE;
    else if (activity == USB_FW_UPDATE_ACTIVITY_ERROR)
        policy.mode = BSP_STATUS_LED_MODE_ERROR;
    static constexpr uint8_t Channels[] = {0U, 0U, BSP_STATUS_LED_RED, BSP_STATUS_LED_BLUE,
                                           BSP_STATUS_LED_RED | BSP_STATUS_LED_BLUE};
    policy.override_enabled = policy.mode != BSP_STATUS_LED_MODE_ERROR
                              && override_setting > static_cast<int32_t>(dp::LedOverride::Auto)
                              && override_setting <= static_cast<int32_t>(dp::LedOverride::RedBlue);
    if (policy.override_enabled)
        policy.override_channels = Channels[override_setting];
    return policy;
}
} // namespace app
