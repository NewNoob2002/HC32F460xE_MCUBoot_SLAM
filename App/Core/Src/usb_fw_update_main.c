#include "app_confirm.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_reset.h"
#include "hc32_ll_utility.h"
#include "usb_fw_update.h"

volatile int g_usb_fw_update_init_result;
volatile int g_usb_fw_update_confirm_result;

int main(void) {
    uint32_t now_ms = 0U;

    bsp_clock_init();
    if (!bsp_external_watchdog_init(now_ms)) {
        g_usb_fw_update_init_result = -1;
        for (;;) {}
    }
    g_usb_fw_update_init_result = usb_fw_update_init();
    if (g_usb_fw_update_init_result != 0) {
        for (;;) {
            DDL_DelayMS(1U);
            bsp_external_watchdog_poll(++now_ms);
        }
    }

    g_usb_fw_update_confirm_result = app_confirm_running_image(APP_AUTO_CONFIRM != 0);
    for (;;) {
        DDL_DelayMS(1U);
        bsp_external_watchdog_poll(++now_ms);
        if (usb_fw_update_poll(now_ms) == USB_FW_UPDATE_ACTION_RESET)
            bsp_system_reset();
    }
}
