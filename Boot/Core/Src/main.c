#include "boot_handover.h"
#include "bootutil/bootutil.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_timebase.h"
#include "usb_fw_update.h"
#if defined(HC32_DEBUG_LOG)
#include "elog.h"
#include "hc32_debug_log.h"
#endif

volatile int g_boot_recovery_init_result;

static _Noreturn void run_recovery_updater(void) {
    uint32_t now_ms = bsp_millis();

    if (!bsp_external_watchdog_init(now_ms))
        bsp_panic("recovery watchdog init failed");
    g_boot_recovery_init_result = usb_fw_update_init();
    if (g_boot_recovery_init_result != 0)
        bsp_panic("recovery updater init failed");
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

    bsp_clock_init();
    if (!bsp_timebase_init())
        bsp_panic("boot timebase init failed");
#if defined(HC32_DEBUG_LOG)
    if (hc32_debug_log_init())
        elog_i("boot", "startup");
#endif
    FIH_CALL(boot_go, rc, &rsp);
    if (FIH_NOT_EQ(rc, FIH_SUCCESS))
        run_recovery_updater();
    boot_handover(&rsp);
}
