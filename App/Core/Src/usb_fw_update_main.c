#include "app_confirm.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "cmsis_gcc.h"
#include "usb_fw_update.h"
#if defined(HC32_DEBUG_LOG)
#include "elog.h"
#include "hc32_debug_log.h"
#endif

volatile int g_usb_fw_update_init_result;
volatile int g_usb_fw_update_confirm_result;

int main(void) {
    uint32_t now_ms;

    if (!bsp_clock_init())
        bsp_panic("updater clock init failed");
    if (!bsp_status_led_init())
        bsp_panic("updater status LED init failed");
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_UPDATER);
    if (!bsp_timebase_init())
        bsp_panic("updater timebase init failed");
#if defined(HC32_DEBUG_LOG)
    if (hc32_debug_log_init())
        elog_i("updater", "startup");
#endif
    now_ms = bsp_millis();
    if (!bsp_external_watchdog_init(now_ms)) {
        g_usb_fw_update_init_result = -1;
        bsp_panic("updater watchdog init failed");
    }
    g_usb_fw_update_init_result = usb_fw_update_init();
    if (g_usb_fw_update_init_result != 0)
        bsp_panic("updater USB init failed");

    g_usb_fw_update_confirm_result = app_confirm_running_image(APP_AUTO_CONFIRM != 0);
    if (g_usb_fw_update_confirm_result != 0)
        bsp_panic("updater image confirmation failed");
#if defined(HC32_DEBUG_LOG)
    elog_i("updater", "init=%d confirm=%d", g_usb_fw_update_init_result, g_usb_fw_update_confirm_result);
#endif
    for (;;) {
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
        if (usb_fw_update_poll(now_ms) == USB_FW_UPDATE_ACTION_RESET) {
            bsp_system_reset();
        }
        __WFI();
    }
}
