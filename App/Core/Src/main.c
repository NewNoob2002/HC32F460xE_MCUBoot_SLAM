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
#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
#endif

volatile int g_app_confirm_result;
volatile int g_app_usb_init_result;

int main(void) {
#if APP_PHASE2_HIL_MODE == 0
    uint32_t now_ms;
#endif

    if (!bsp_clock_init())
        bsp_panic("app clock init failed");
    if (!bsp_status_led_init())
        bsp_panic("app status LED init failed");
#if APP_PHASE2_HIL_MODE != 0
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_PRIMARY);
#else
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_UPDATER);
#endif
    if (!bsp_timebase_init())
        bsp_panic("app timebase init failed");
#if defined(HC32_DEBUG_LOG)
    if (hc32_debug_log_init())
        elog_i("app", "startup");
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
#endif
    if (g_app_confirm_result != 0)
        bsp_panic("app startup check failed");
#if defined(HC32_DEBUG_LOG)
    elog_i("app", "usb=%d confirm=%d", g_app_usb_init_result, g_app_confirm_result);
#endif
    for (;;) {
#if APP_PHASE2_HIL_MODE != 0
        bsp_delay_ms(1U);
#else
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
        if (usb_fw_update_poll(now_ms) == USB_FW_UPDATE_ACTION_RESET)
            bsp_system_reset();
#endif
        bsp_status_led_poll(bsp_millis());
#if APP_PHASE2_HIL_MODE == 0
        __WFI();
#endif
    }
}
