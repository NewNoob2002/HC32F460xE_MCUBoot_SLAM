#include "bsp_clock.h"
#include "bsp_log_uart.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "cmsis_gcc.h"
#include "core_debug.h"

#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
#else
#include "app_diagnostics.h"
#include "data_broker.h"
#include "data_proc.h"
#endif

#if APP_PHASE2_HIL_MODE != 0
volatile int g_app_confirm_result;

static _Noreturn void run_phase2_hil(void) {
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_PRIMARY);
    g_app_confirm_result = phase2_hil_run(APP_PHASE2_HIL_MODE);
    if (g_app_confirm_result != 0)
        bsp_panic("app startup check failed");
    bsp_write_protection_restore();
    for (;;) {
        bsp_delay_ms(1U);
        bsp_status_led_poll(bsp_millis());
    }
}
#else
/* Global so J-Link/RTT sessions can inspect the latest snapshot by symbol. */
struct app_diagnostics g_app_diagnostics;
static struct data_broker g_app_broker;

static _Noreturn void run_application(void) {
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_UPDATER);
    data_broker_init(&g_app_broker);
    dp_power_init(&g_app_broker);
    dp_charger_init(&g_app_broker, bsp_millis());
    if (!dp_system_init(&g_app_broker, bsp_millis()))
        bsp_panic("app watchdog init failed");
    if (dp_update_init(&g_app_broker) != 0)
        bsp_panic("app USB init failed");
    if (dp_system_confirm(APP_AUTO_CONFIRM != 0) != 0)
        bsp_panic("app startup check failed");
    if (!app_diagnostics_init(&g_app_diagnostics, &g_app_broker, bsp_millis()))
        bsp_panic("app diagnostics init failed");

    bsp_write_protection_restore();
    for (;;) {
        const uint32_t now_ms = bsp_millis();
        dp_system_poll(now_ms);
        const enum usb_fw_update_action action = dp_update_poll(now_ms);
        (void)data_broker_handle_timers(&g_app_broker, now_ms);
        if (action == USB_FW_UPDATE_ACTION_RESET)
            bsp_system_reset();
        bsp_status_led_poll(bsp_millis());
        __WFI();
    }
}
#endif

int main(void) {
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
    run_phase2_hil();
#else
    run_application();
#endif
}
