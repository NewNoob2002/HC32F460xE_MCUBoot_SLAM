#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_panic.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "usb_vendor_bulk.h"
#if defined(HC32_DEBUG_LOG)
#include "elog.h"
#include "hc32_debug_log.h"
#endif

volatile int g_usb_vendor_bulk_init_result;

int main(void) {
    uint32_t now_ms;

    bsp_clock_init();
    if (!bsp_status_led_init())
        bsp_panic("USB loopback status LED init failed");
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_DIAGNOSTIC);
    if (!bsp_timebase_init())
        bsp_panic("USB loopback timebase init failed");
#if defined(HC32_DEBUG_LOG)
    if (hc32_debug_log_init())
        elog_i("usb-loopback", "startup");
#endif
    now_ms = bsp_millis();
    if (!bsp_external_watchdog_init(now_ms)) {
        g_usb_vendor_bulk_init_result = -1;
        bsp_panic("USB loopback watchdog init failed");
    }
    g_usb_vendor_bulk_init_result = usb_vendor_bulk_init();
    if (g_usb_vendor_bulk_init_result != 0)
        bsp_panic("USB loopback init failed");
    for (;;) {
        bsp_delay_ms(1U);
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
        bsp_status_led_poll(now_ms);
    }
}
