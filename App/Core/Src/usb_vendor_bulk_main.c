#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "hc32_ll_utility.h"
#include "hc32f460.h"
#include "usb_vendor_bulk.h"

volatile int g_usb_vendor_bulk_init_result;

int main(void) {
    uint32_t now_ms = 0U;

    bsp_clock_init();
    g_usb_vendor_bulk_init_result = bsp_external_watchdog_init(now_ms) ? usb_vendor_bulk_init() : -1;
    for (;;) {
        DDL_DelayMS(1U);
        bsp_external_watchdog_poll(++now_ms);
    }
}
