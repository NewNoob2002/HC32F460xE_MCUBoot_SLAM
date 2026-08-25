#include "bsp_external_watchdog.h"
#include "bsp_board_config.h"
#include "hc32_ll.h"

void watchdog_scheduler_init(watchdog_scheduler_t* scheduler, uint32_t now_ms, uint32_t interval_ms, uint32_t pulse_ms,
                             bool active_level, watchdog_gpio_write_t write, void* context) {
    scheduler->state = BSP_WDOG_STATE_IDLE;
    scheduler->interval_ms = interval_ms;
    scheduler->pulse_ms = pulse_ms;
    scheduler->next_feed_ms = now_ms + interval_ms;
    scheduler->pulse_start_ms = now_ms;
    scheduler->active_level = active_level;
    scheduler->enabled = (write != NULL) && (interval_ms != 0U) && (pulse_ms != 0U) && (pulse_ms < interval_ms);
    scheduler->write = write;
    scheduler->write_context = context;
    if (scheduler->enabled)
        scheduler->write(!active_level, context);
}

bool watchdog_scheduler_force(watchdog_scheduler_t* scheduler, uint32_t now_ms) {
    if ((scheduler == NULL) || !scheduler->enabled || (scheduler->state != BSP_WDOG_STATE_IDLE))
        return false;
    scheduler->write(scheduler->active_level, scheduler->write_context);
    scheduler->state = BSP_WDOG_STATE_PULSE_HIGH;
    scheduler->pulse_start_ms = now_ms;
    scheduler->next_feed_ms = now_ms + scheduler->interval_ms;
    return true;
}

void watchdog_scheduler_poll(watchdog_scheduler_t* scheduler, uint32_t now_ms) {
    if ((scheduler == NULL) || !scheduler->enabled)
        return;
    if (scheduler->state == BSP_WDOG_STATE_PULSE_HIGH) {
        if ((uint32_t)(now_ms - scheduler->pulse_start_ms) >= scheduler->pulse_ms) {
            scheduler->write(!scheduler->active_level, scheduler->write_context);
            scheduler->state = BSP_WDOG_STATE_IDLE;
            if ((int32_t)(now_ms - scheduler->next_feed_ms) >= 0) {
                uint32_t late_ms = now_ms - scheduler->next_feed_ms;
                uint32_t periods = (late_ms / scheduler->interval_ms) + 1U;
                scheduler->next_feed_ms += periods * scheduler->interval_ms;
            }
        }
        return;
    }
    if ((int32_t)(now_ms - scheduler->next_feed_ms) >= 0) {
        uint32_t late_ms = now_ms - scheduler->next_feed_ms;
        uint32_t periods = (late_ms / scheduler->interval_ms) + 1U;
        scheduler->write(scheduler->active_level, scheduler->write_context);
        scheduler->state = BSP_WDOG_STATE_PULSE_HIGH;
        scheduler->pulse_start_ms = now_ms;
        scheduler->next_feed_ms += periods * scheduler->interval_ms;
    }
}

void watchdog_scheduler_stop_low(watchdog_scheduler_t* scheduler) {
    if ((scheduler != NULL) && scheduler->enabled)
        scheduler->write(!scheduler->active_level, scheduler->write_context);
    if (scheduler != NULL) {
        scheduler->state = BSP_WDOG_STATE_IDLE;
        scheduler->enabled = false;
    }
}

#if !defined(BOOT_HOST_TEST)
static watchdog_scheduler_t board_scheduler;
static bool board_ready;

static void board_gpio_write(bool level, void* context) {
    (void)context;
    if (level)
        GPIO_SetPins(BOOT_EXTERNAL_WATCHDOG_PORT, BOOT_EXTERNAL_WATCHDOG_PIN);
    else
        GPIO_ResetPins(BOOT_EXTERNAL_WATCHDOG_PORT, BOOT_EXTERNAL_WATCHDOG_PIN);
}

bool bsp_external_watchdog_init(uint32_t now_ms) {
    stc_gpio_init_t init;
    board_ready = false;
    GPIO_ResetPins(BOOT_EXTERNAL_WATCHDOG_PORT, BOOT_EXTERNAL_WATCHDOG_PIN);
    (void)GPIO_StructInit(&init);
    init.u16PinState = PIN_STAT_RST;
    init.u16PinDir = PIN_DIR_OUT;
    init.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    if (GPIO_Init(BOOT_EXTERNAL_WATCHDOG_PORT, BOOT_EXTERNAL_WATCHDOG_PIN, &init) != LL_OK)
        return false;
    if (GPIO_ReadOutputPins(BOOT_EXTERNAL_WATCHDOG_PORT, BOOT_EXTERNAL_WATCHDOG_PIN) != PIN_RESET)
        return false;
    watchdog_scheduler_init(&board_scheduler, now_ms, BOOT_EXTERNAL_WATCHDOG_INTERVAL_MS,
                            BOOT_EXTERNAL_WATCHDOG_PULSE_MS, BOOT_EXTERNAL_WATCHDOG_ACTIVE_LEVEL != 0U,
                            board_gpio_write, NULL);
    board_ready = board_scheduler.enabled;
    return board_ready;
}
void bsp_external_watchdog_poll(uint32_t now_ms) {
    if (board_ready)
        watchdog_scheduler_poll(&board_scheduler, now_ms);
}
bool bsp_external_watchdog_force_feed(uint32_t now_ms) {
    return board_ready && watchdog_scheduler_force(&board_scheduler, now_ms);
}
bool bsp_external_watchdog_is_ready(void) {
    return board_ready;
}
bool bsp_external_watchdog_is_pulse_active(void) {
    return board_ready && (board_scheduler.state == BSP_WDOG_STATE_PULSE_HIGH);
}
void bsp_external_watchdog_prepare_handover(void) {
    if (board_ready)
        watchdog_scheduler_stop_low(&board_scheduler);
    board_ready = false;
}
#endif
