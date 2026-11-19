#include <cassert>

#include "StatusLedPolicy.h"

int main() {
    struct Case {
        dp::BatteryLedMode battery;
        bsp_status_led_mode expected;
    };
    constexpr Case cases[] = {
        {dp::BatteryLedMode::Updater, BSP_STATUS_LED_MODE_APP_UPDATER},
        {dp::BatteryLedMode::Charging, BSP_STATUS_LED_MODE_APP_CHARGING},
        {dp::BatteryLedMode::Full, BSP_STATUS_LED_MODE_APP_FULL},
        {dp::BatteryLedMode::Low, BSP_STATUS_LED_MODE_APP_LOW},
        {dp::BatteryLedMode::Critical, BSP_STATUS_LED_MODE_APP_CRITICAL},
        {dp::BatteryLedMode::NoPack, BSP_STATUS_LED_MODE_APP_NO_PACK},
    };
    constexpr uint8_t channels[] = {0U, 0U, BSP_STATUS_LED_RED, BSP_STATUS_LED_BLUE,
                                    BSP_STATUS_LED_RED | BSP_STATUS_LED_BLUE};
    for (const auto& test : cases) {
        for (int32_t setting = -1; setting <= 5; ++setting) {
            for (const auto activity :
                 {USB_FW_UPDATE_ACTIVITY_IDLE, USB_FW_UPDATE_ACTIVITY_ACTIVE, USB_FW_UPDATE_ACTIVITY_ERROR}) {
                const auto policy = app::statusLedPolicy(test.battery, activity, setting);
                const auto expected = activity == USB_FW_UPDATE_ACTIVITY_ACTIVE  ? BSP_STATUS_LED_MODE_UPDATE
                                      : activity == USB_FW_UPDATE_ACTIVITY_ERROR ? BSP_STATUS_LED_MODE_ERROR
                                                                                 : test.expected;
                assert(policy.mode == expected);
                const bool override = setting >= 1 && setting <= 4 && activity != USB_FW_UPDATE_ACTIVITY_ERROR;
                assert(policy.override_enabled == override);
                assert(policy.override_channels == (override ? channels[setting] : 0U));
            }
        }
    }
}
