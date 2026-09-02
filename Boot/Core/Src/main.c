#include "boot_handover.h"
#include "bootutil/bootutil.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_log_uart.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "core_debug.h"
#include "usb_fw_update.h"

volatile int g_boot_recovery_init_result;

static _Noreturn void run_recovery_updater(void) {
    uint32_t now_ms = bsp_millis();

    bsp_write_protection_unlock();
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_RECOVERY);
    if (!bsp_external_watchdog_init(now_ms))
        bsp_panic("recovery watchdog init failed");
    g_boot_recovery_init_result = usb_fw_update_init();
    if (g_boot_recovery_init_result != 0)
        bsp_panic("recovery updater init failed");
    bsp_write_protection_restore();
    for (;;) {
        bsp_delay_ms(1U);
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
        if (g_boot_recovery_init_result == 0 && usb_fw_update_poll(now_ms) == USB_FW_UPDATE_ACTION_RESET)
            bsp_system_reset();
    }
}

int main(void) {
    struct boot_rsp rsp = {0};
    FIH_DECLARE(rc, FIH_FAILURE);

    bsp_write_protection_unlock();
    if (!bsp_clock_init())
        bsp_panic("boot clock init failed");
    if (!bsp_timebase_init())
        bsp_panic("boot timebase init failed");
    (void)bsp_log_uart_init();

    CORE_DEBUG_INIT();
    CORE_DEBUG_PRINTF("boot startup");
    if (!bsp_status_led_init())
        bsp_panic("boot status LED init failed");
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_BOOT);
    bsp_write_protection_restore();
    FIH_CALL(boot_go, rc, &rsp);
    if (FIH_NOT_EQ(rc, FIH_SUCCESS)) {
        CORE_DEBUG_PRINTF("no valid image found, running recovery updater");
        run_recovery_updater();
    }
    boot_handover(&rsp);
}
