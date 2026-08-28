#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_timebase.h"
#include "hc32f460.h"
#include "usb_vendor_bulk.h"
#if defined(HC32_DEBUG_LOG)
#include "elog.h"
#include "hc32_debug_log.h"
#endif

volatile int g_usb_vendor_bulk_init_result;

int main(void) {
    uint32_t now_ms;

    bsp_clock_init();
    if (!bsp_timebase_init())
        for (;;) {}
#if defined(HC32_DEBUG_LOG)
    if (hc32_debug_log_init())
        elog_i("usb-loopback", "startup");
#endif
    now_ms = bsp_millis();
    g_usb_vendor_bulk_init_result = bsp_external_watchdog_init(now_ms) ? usb_vendor_bulk_init() : -1;
    for (;;) {
        bsp_delay_ms(1U);
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
    }
}
