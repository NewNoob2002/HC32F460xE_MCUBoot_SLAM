#include "app_confirm.h"
#include "bq40z50.h"
#include "bsp_clock.h"
#include "bsp_external_watchdog.h"
#include "bsp_i2c2.h"
#include "bsp_log_uart.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "cmsis_gcc.h"
#include "core_debug.h"
#include "usb_fw_update.h"

#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
#endif

volatile int g_app_confirm_result;
volatile int g_app_usb_init_result;
#if APP_PHASE2_HIL_MODE == 0
volatile int g_app_i2c2_init_result;
volatile int g_app_bq40z50_probe_result;
volatile int g_app_husb238_probe_result;
volatile int g_app_mp2762a_probe_result;
volatile int g_app_bq40z50_identity_result;
struct bq40z50_identity g_app_bq40z50_identity;

static void probe_power_devices(void) {
    g_app_i2c2_init_result = bsp_i2c2_init();
    g_app_bq40z50_probe_result = bsp_i2c2_probe(BQ40Z50_I2C_ADDRESS);
    g_app_husb238_probe_result = bsp_i2c2_probe(0x08U);
    g_app_mp2762a_probe_result = bsp_i2c2_probe(0x5CU);
    g_app_bq40z50_identity_result = g_app_bq40z50_probe_result == BSP_I2C2_OK
                                        ? bq40z50_read_identity(&g_app_bq40z50_identity)
                                        : g_app_bq40z50_probe_result;
    CORE_DEBUG_PRINTF("i2c2=%d bq@0b=%d husb@08=%d mp@5c=%d bq_identity=%d", g_app_i2c2_init_result,
                      g_app_bq40z50_probe_result, g_app_husb238_probe_result, g_app_mp2762a_probe_result,
                      g_app_bq40z50_identity_result);
}
#endif

int main(void) {
#if APP_PHASE2_HIL_MODE == 0
    uint32_t now_ms;
#endif
    bsp_write_protection_unlock();
    bsp_debug_port_use_swd();
    if (!bsp_clock_init())
        bsp_panic("app clock init failed");
    if (!bsp_timebase_init())
        bsp_panic("app timebase init failed");
    (void)bsp_log_uart_init();
    CORE_DEBUG_INIT();
    CORE_DEBUG_PRINTF("app startup");
    if (!bsp_status_led_init())
        bsp_panic("app status LED init failed");
#if APP_PHASE2_HIL_MODE != 0
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_PRIMARY);
#else
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_UPDATER);
    probe_power_devices();
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

    CORE_DEBUG_PRINTF("usb=%d confirm=%d", g_app_usb_init_result, g_app_confirm_result);
    bsp_write_protection_restore();
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
