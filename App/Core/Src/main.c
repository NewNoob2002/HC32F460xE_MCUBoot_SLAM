#include "app_confirm.h"
#include "bsp_clock.h"
#include "bsp_panic.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#if defined(HC32_DEBUG_LOG)
#include "elog.h"
#include "hc32_debug_log.h"
#endif
#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
#endif

volatile int g_app_confirm_result;

int main(void) {
    if (!bsp_clock_init())
        bsp_panic("app clock init failed");
    if (!bsp_status_led_init())
        bsp_panic("app status LED init failed");
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_PRIMARY);
    if (!bsp_timebase_init())
        bsp_panic("app timebase init failed");
#if defined(HC32_DEBUG_LOG)
    if (hc32_debug_log_init())
        elog_i("app", "startup");
#endif
#if APP_PHASE2_HIL_MODE != 0
    g_app_confirm_result = phase2_hil_run(APP_PHASE2_HIL_MODE);
#else
    g_app_confirm_result = app_confirm_running_image(APP_AUTO_CONFIRM != 0);
#endif
    if (g_app_confirm_result != 0)
        bsp_panic("app startup check failed");
#if defined(HC32_DEBUG_LOG)
    elog_i("app", "confirm result=%d", g_app_confirm_result);
#endif
    for (;;) {
        bsp_delay_ms(1U);
        bsp_status_led_poll(bsp_millis());
    }
}
