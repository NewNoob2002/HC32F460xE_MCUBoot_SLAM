#include "boot_handover.h"
#include "boot_memory_map.h"
#include "bootutil/bootutil.h"
#include "bsp_boot_request.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_flash.h"
#include "bsp_log_uart.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "cmsis_gcc.h"
#include "core_debug.h"
#include "usb_fw_update.h"

volatile int g_boot_recovery_init_result;
volatile int g_boot_factory_erase_result;

static int erase_range(uint32_t base, uint32_t size) {
    for (uint32_t offset = 0U; offset < size; offset += FLASH_SECTOR_SIZE) {
        bsp_external_watchdog_poll(bsp_millis());
        if (bsp_flash_erase_sector((base + offset) / FLASH_SECTOR_SIZE) != BSP_FLASH_OK)
            return -1;
    }
    return 0;
}

/* Administrator factory erase, run only after the authenticated FACTORY_ERASE response has been sent. Secondary goes
 * first, header sector first: with MCUBOOT_BOOTSTRAP an invalid Primary next to a valid Secondary is restored from
 * Secondary, so erasing Primary first could resurrect the previous image after a power loss. An interruption before
 * Primary is touched leaves the current image running; a later one leaves no valid image, so Boot enters recovery
 * still provisioned and the erase can be repeated. Product Config goes last so the device stays locked until every
 * image byte is gone. App settings (app_kv) are kept. */
static void factory_erase(void) {
    g_boot_factory_erase_result = erase_range(SECONDARY_SLOT_BASE, SECONDARY_SLOT_SIZE);
    if (g_boot_factory_erase_result == 0)
        g_boot_factory_erase_result = erase_range(SCRATCH_BASE, SCRATCH_SIZE);
    if (g_boot_factory_erase_result == 0)
        g_boot_factory_erase_result = erase_range(PRIMARY_SLOT_BASE, PRIMARY_SLOT_SIZE);
    if (g_boot_factory_erase_result == 0)
        g_boot_factory_erase_result = erase_range(RESERVED_BASE + PRODUCT_KV_OFFSET, PRODUCT_KV_SIZE);
    if (g_boot_factory_erase_result != 0)
        CORE_DEBUG_PRINTF("factory erase failed; resetting into recovery");
}

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
        now_ms = bsp_millis();
        bsp_external_watchdog_poll(now_ms);
        const enum usb_fw_update_action action =
            g_boot_recovery_init_result == 0 ? usb_fw_update_poll(now_ms) : USB_FW_UPDATE_ACTION_NONE;
        if (action == USB_FW_UPDATE_ACTION_ERASE_AND_RESET)
            factory_erase();
        /* REBOOT to recovery from recovery behaves as from the Application: boot_go() runs, then recovery again. */
        if (action == USB_FW_UPDATE_ACTION_RESET_TO_RECOVERY)
            bsp_boot_request_recovery();
        if (action != USB_FW_UPDATE_ACTION_NONE)
            bsp_system_reset();
        __WFI();
    }
}

int main(void) {
    struct boot_rsp rsp = {0};
    FIH_DECLARE(rc, FIH_FAILURE);
    /* Taken before anything else so a request never survives into a second boot. */
    const bool recovery_requested = bsp_boot_request_take_recovery(bsp_reset_capture());

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
    /* boot_go() always runs first so a pending swap or revert completes before recovery may erase Secondary. */
    FIH_CALL(boot_go, rc, &rsp);
    if (FIH_NOT_EQ(rc, FIH_SUCCESS)) {
        CORE_DEBUG_PRINTF("no valid image found, running recovery updater");
        run_recovery_updater();
    }
    if (recovery_requested) {
        CORE_DEBUG_PRINTF("recovery requested by application");
        run_recovery_updater();
    }
    boot_handover(&rsp);
}
