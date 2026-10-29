#include "App.h"
#include "bsp_boot_request.h"
#include "bsp_clock.h"
#include "bsp_log_uart.h"
#include "bsp_panic.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "core_debug.h"

extern "C" {
#include "cmsis_gcc.h"
}

#if defined(BSP_DEBUG)
namespace {
debug::Service* g_log_sink = nullptr;
}

/* Copies each EasyLogger line into the Debug Link log ring, like the UART/RTT console. */
extern "C" void core_debug_forward(const char* line, size_t size) {
    // EasyLogger may be called from an interrupt handler, but the Debug Service ring is main-loop only.
    if (g_log_sink != nullptr && __get_IPSR() == 0U)
        g_log_sink->log_line(line, size, bsp_millis());
}
#endif

#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
volatile int g_app_confirm_result;

[[noreturn]] void App::runPhase2Hil() {
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
extern "C" {
app::DiagnosticsSnapshot g_app_diagnostics{};
}

[[noreturn]] void App::runApplication() {
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_APP_UPDATER);
    (void)settings_store_.init();
    if (!settings_.init(broker_))
        bsp_panic("app settings init failed");
    (void)power_.init(broker_);
    (void)charger_.init(broker_, bsp_millis());
    if (!system_.init(broker_, bsp_millis()))
        bsp_panic("app watchdog init failed");
    if (update_.init(broker_) != 0)
        bsp_panic("app USB init failed");
    if (system_.confirm(APP_AUTO_CONFIRM != 0) != 0)
        bsp_panic("app startup check failed");
    if (!diagnostics_.init(broker_, bsp_millis()))
        bsp_panic("app diagnostics init failed");

    if (!debug_.init(broker_, usb_fw_update_serial(), bsp_reset_capture()))
        bsp_panic("app debug init failed");
    debug_.log(3U, bsp_millis(), "app startup complete");
    bsp_write_protection_restore();
    for (;;) {
        const uint32_t now_ms = bsp_millis();
        system_.poll(now_ms);
        const auto action = update_.poll(now_ms);
        (void)broker_.handleTimers(now_ms);
        const bool debug_reset = debug_.poll(now_ms);
        if (action == USB_FW_UPDATE_ACTION_RESET_TO_RECOVERY)
            bsp_boot_request_recovery();
        if (action != USB_FW_UPDATE_ACTION_NONE || debug_reset)
            bsp_system_reset();
        bsp_status_led_poll(bsp_millis());
        __WFI();
    }
}
#endif

[[noreturn]] void App::run() {
    bsp_write_protection_unlock();
    bsp_debug_port_use_swd();
    if (!bsp_clock_init())
        bsp_panic("app clock init failed");
    if (!bsp_timebase_init())
        bsp_panic("app timebase init failed");
    (void)bsp_log_uart_init();
#if defined(BSP_DEBUG)
    // The log ring is valid before Service::init(), so startup lines are kept until a host streams them.
    g_log_sink = &debug_;
#endif
    CORE_DEBUG_INIT();
    CORE_DEBUG_PRINTF("app startup");
    if (!bsp_status_led_init())
        bsp_panic("app status LED init failed");
#if APP_PHASE2_HIL_MODE != 0
    runPhase2Hil();
#else
    runApplication();
#endif
}
