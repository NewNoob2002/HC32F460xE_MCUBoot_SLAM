#include "app_confirm.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_log_uart.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "cmsis_gcc.h"
#include "core_debug.h"
#include "usb_fw_update.h"

#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
#else
#include "app_diagnostics.h"
#include "power_devices.h"
#endif

volatile int g_app_confirm_result;
volatile int g_app_usb_init_result;
#if APP_PHASE2_HIL_MODE == 0
struct power_devices_status g_app_power_devices_status;
struct app_diagnostics g_app_diagnostics;
#endif

int main(void) {
#if APP_PHASE2_HIL_MODE == 0
    uint32_t now_ms;
#endif
    bsp_write_protection_unlock();
    bsp_debug_port_use_swd();
    if (!bsp_clock_init())
        bsp_panic("app clock init failed");
    if (!bsp_timebase_init())
        bsp_panic("app timebase init failed");
    (void)bsp_log_uart_init();
    CORE_DEBUG_INIT();
    CORE_DEBUG_PRINTF("app startup");
    if (!bsp_status_led_init())
        bsp_panic("app status LED init failed");
#if APP_PHASE2_HIL_MODE != 0
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_PRIMARY);
#else
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_UPDATER);
    power_devices_init(&g_app_power_devices_status);
#endif
#if APP_PHASE2_HIL_MODE != 0
    g_app_confirm_result = phase2_hil_run(APP_PHASE2_HIL_MODE);
#else
    now_ms = bsp_millis();
    if (!bsp_external_watchdog_init(now_ms))
        bsp_panic("app watchdog init failed");
    g_app_usb_init_result = usb_fw_update_init();
    if (g_app_usb_init_result != 0)
        bsp_panic("app USB init failed");
    g_app_confirm_result = app_confirm_running_image(APP_AUTO_CONFIRM != 0);
    app_diagnostics_init(&g_app_diagnostics, bsp_millis(), &g_app_power_devices_status, g_app_usb_init_result,
                         g_app_confirm_result);
#endif
    if (g_app_confirm_result != 0)
        bsp_panic("app startup check failed");

    bsp_write_protection_restore();
    for (;;) {
#if APP_PHASE2_HIL_MODE != 0
        bsp_delay_ms(1U);
#else
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
        const enum usb_fw_update_action action = usb_fw_update_poll(now_ms);
        (void)app_diagnostics_poll(&g_app_diagnostics, now_ms);
        if (action == USB_FW_UPDATE_ACTION_RESET)
            bsp_system_reset();
#endif
        bsp_status_led_poll(bsp_millis());
#if APP_PHASE2_HIL_MODE == 0
        __WFI();
#endif
    }
}
