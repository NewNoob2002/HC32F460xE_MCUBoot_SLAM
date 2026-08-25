#include "main.h"
#include "app_jump.h"
#include "app_validator.h"
#include "boot_log.h"
#include "boot_protocol_parser.h"
#include "boot_state.h"
#include "boot_timebase.h"
#include "boot_update_service.h"
#include "bsp_board_config.h"
#include "bsp_clock.h"
#include "bsp_debug_port.h"
#include "bsp_external_watchdog.h"
#include "bsp_i2c_slave.h"
#include "bsp_power.h"
#include "bsp_reset.h"
#include "bsp_status_led.h"
#include "bsp_write_protection.h"

static boot_protocol_parser_t protocol_parser;
static uint8_t transport_rx_buffer[BOOT_I2C_BUFFER_CAPACITY];
static uint32_t protocol_last_byte_ms;
static boot_context_t context = {0};
#if BOOT_ENABLE_EASYLOGGER
static const char* mode_name(boot_mode_t mode) {
    if (mode == BOOT_MODE_START_APPLICATION)
        return "app";
    if (mode == BOOT_MODE_UPDATE_WINDOW)
        return "update";
    return "recovery";
}
static bool logging_init(void) {
    if (elog_init() != ELOG_NO_ERR)
        return false;
    elog_output_lock_enabled(false);
    for (uint8_t level = ELOG_LVL_ASSERT; level <= ELOG_LVL_DEBUG; ++level)
        elog_set_fmt(level, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_start();
    return true;
}
#else
static bool logging_init(void) {
    return false;
}
#endif

#if BOOT_ENABLE_EASYLOGGER
static void log_protocol_diagnostics(const boot_protocol_parser_t* parser) {
    static uint32_t previous_fault_events;
    bsp_i2c_slave_counters_t i2c;
    boot_protocol_parser_stats_t protocol;
    bsp_i2c_slave_get_counters(&i2c);
    BootProtocolParserGetStats(parser, &protocol);
    const uint32_t fault_events = i2c.rx_overflow + i2c.empty_tx_reads + i2c.response_busy + i2c.arbitration_lost
                                  + i2c.recovery_active_stall + i2c.recovery_hw_busy
                                  + i2c.recovery_init_failures + protocol.timeout;
    if (fault_events == previous_fault_events)
        return;
    previous_fault_events = fault_events;
    BOOT_LOG_WARN(
        "I2C diag am_rx=%lu am_tx=%lu rx_b=%lu tx_b=%lu stop=%lu nack=%lu arlo=%lu ovf=%lu empty=%lu busy=%lu "
        "timeout=%lu recover_active=%lu recover_busy=%lu init_fail=%lu sr=0x%08lx rec_sr=0x%08lx state=%u reason=%u",
        (unsigned long)i2c.address_match_rx, (unsigned long)i2c.address_match_tx, (unsigned long)i2c.rx_bytes,
        (unsigned long)i2c.tx_bytes, (unsigned long)i2c.stop_events, (unsigned long)i2c.nack_events,
        (unsigned long)i2c.arbitration_lost, (unsigned long)i2c.rx_overflow, (unsigned long)i2c.empty_tx_reads,
        (unsigned long)i2c.response_busy,
        (unsigned long)protocol.timeout, (unsigned long)i2c.recovery_active_stall,
        (unsigned long)i2c.recovery_hw_busy, (unsigned long)i2c.recovery_init_failures, (unsigned long)i2c.last_sr,
        (unsigned long)i2c.last_recovery_sr, (unsigned)i2c.last_recovery_state,
        (unsigned)i2c.last_recovery_reason);
}
#else
static void log_protocol_diagnostics(const boot_protocol_parser_t* parser) {
    (void)parser;
}
#endif

static void fatal_safe_loop(bool timebase_ready, bool watchdog_ready, bool led_ready) {
    if (led_ready)
        bsp_status_led_set_mode(BOOT_LED_MODE_FATAL);
    while (1) {
        bsp_power_hold_assert();
        if (timebase_ready) {
            uint32_t now_ms = boot_time_ms();
            if (watchdog_ready)
                bsp_external_watchdog_poll(now_ms);
            if (led_ready)
                bsp_status_led_poll(now_ms);
        }
    }
}

int main(void) {
    uint32_t now_ms;

    bsp_write_protection_unlock();
    bsp_debug_port_configure_for_boot_gpio();
    bsp_clock_init();
    boot_timebase_init();
    bsp_power_init();
    boot_capture_reset_info(&context);

    now_ms = boot_time_ms();
    context.watchdog_ready = bsp_external_watchdog_init(now_ms);
    context.led_ready = bsp_status_led_init();
    if (context.led_ready)
        bsp_status_led_set_mode(BOOT_LED_MODE_BOOTING);
    context.log_ready = logging_init();
    context.app_valid = boot_application_vector_is_valid();
    context.update_started_ms = boot_time_ms();
    context.jump_requested = false;
    boot_select_mode(&context);

    BOOT_LOG_INFO("PB3 hold=%s", bsp_power_hold_is_asserted() ? "high" : "fault");
    BOOT_LOG_INFO("PSPCR=0x%04x", bsp_debug_port_pspcr());
    BOOT_LOG_INFO("TPL5010 PA6 enabled=%u idle=low pulse=high/%ums interval=%ums", context.watchdog_ready ? 1U : 0U,
                  (unsigned)BOOT_EXTERNAL_WATCHDOG_PULSE_MS, (unsigned)BOOT_EXTERNAL_WATCHDOG_INTERVAL_MS);
    BOOT_LOG_INFO("PB5 active-high enabled=%u", context.led_ready ? 1U : 0U);
    BOOT_LOG_INFO("id=%s reset=%08lx app=%u mode=%s", BOOT_BUILD_ID, (unsigned long)context.reset_info.raw_flags,
                  context.app_valid ? 1U : 0U, mode_name(context.mode));

    BOOT_LOG_INFO("init begin %ums", (unsigned)(boot_time_ms() - now_ms));
    bool i2c_ready = bsp_i2c_slave_init();
    bsp_write_protection_restore();
    if (context.mode == BOOT_MODE_START_APPLICATION) {
        (void)boot_jump_to_application(APP_FLASH_BASE);
        fatal_safe_loop(true, context.watchdog_ready, context.led_ready);
    }
    if (!i2c_ready) {
        BOOT_LOG_ERROR("I2C slave initialization failed %d", i2c_ready);
        fatal_safe_loop(true, context.watchdog_ready, context.led_ready);
    }
    BOOT_LOG_INFO("ready addr=0x11 baud=%lu mode=%s", (unsigned long)BOOT_I2C_BAUDRATE, mode_name(context.mode));
    BootUpdateServiceInit();
    BootProtocolParserInit(&protocol_parser);
    BootProtocolParserRegisterCallback(&protocol_parser, BootUpdateServiceFrameCallback, NULL);
    protocol_last_byte_ms = boot_time_ms();
    bsp_status_led_set_mode(context.mode == BOOT_MODE_UPDATE_WINDOW ? BOOT_LED_MODE_UPDATE_WINDOW
                                                                    : BOOT_LED_MODE_RECOVERY);
    while (1) {
        now_ms = boot_time_ms();
        const size_t received = bsp_i2c_slave_read(transport_rx_buffer, sizeof(transport_rx_buffer));
        if (received > 0U) {
            (void)BootProtocolParserPushBytes(&protocol_parser, transport_rx_buffer, received);
            protocol_last_byte_ms = now_ms;
        }
        if (BootProtocolParserHasPartialFrame(&protocol_parser)
            && boot_time_elapsed(now_ms, protocol_last_byte_ms, BOOT_PROTOCOL_PARTIAL_TIMEOUT_MS)) {
            BootProtocolParserTimeout(&protocol_parser);
            protocol_last_byte_ms = now_ms;
        }
        if (bsp_i2c_slave_poll(now_ms)) {
            BootProtocolParserReset(&protocol_parser);
            protocol_last_byte_ms = now_ms;
        }
        log_protocol_diagnostics(&protocol_parser);
        bsp_external_watchdog_poll(now_ms);
        bsp_status_led_poll(now_ms);
        boot_timeout_poll(&context, now_ms);
        if (context.mode == BOOT_MODE_RECOVERY)
            context.jump_requested = false;
        const bool update_jump_requested = BootUpdateServiceTakeJumpRequest();
        if ((context.jump_requested && context.app_valid) || update_jump_requested) {
#if BOOT_ENABLE_EASYLOGGER
            bsp_i2c_slave_counters_t i2c_counters;
            const uint32_t reset_flags = bsp_reset_capture();
            bsp_i2c_slave_get_counters(&i2c_counters);
            BOOT_LOG_INFO("JUMP ACK transmitted tx_reads=%lu rx=%lu PB3=%s RMU=0x%04lx",
                          (unsigned long)i2c_counters.tx_complete_reads,
                          (unsigned long)i2c_counters.rx_transactions,
                          bsp_power_hold_is_asserted() ? "high" : "fault", (unsigned long)reset_flags);
            BOOT_LOG_INFO("JUMP to application requested by %s",
                          update_jump_requested ? "update service" : "user");
#endif
            (void)boot_jump_to_application(APP_FLASH_BASE);
            fatal_safe_loop(true, context.watchdog_ready, context.led_ready);
        }
    }
}
