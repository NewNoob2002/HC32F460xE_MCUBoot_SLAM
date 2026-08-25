#include "bsp_status_led.h"
#include "boot_timebase.h"
#include "bsp_board_config.h"
#include "hc32_ll.h"

static uint32_t mode_interval(boot_led_mode_t mode) {
    if (mode == BOOT_LED_MODE_UPDATE_WINDOW)
        return 250U;
    if (mode == BOOT_LED_MODE_RECOVERY)
        return 1000U;
    if (mode == BOOT_LED_MODE_FATAL)
        return 100U;
    return 0U;
}
static void led_write(led_scheduler_t* scheduler) {
    if (scheduler->write != NULL)
        scheduler->write(scheduler->logical_on == scheduler->active_high, scheduler->write_context);
}
void led_scheduler_init(led_scheduler_t* scheduler, bool active_high, led_gpio_write_t write, void* context) {
    scheduler->active_high = active_high;
    scheduler->write = write;
    scheduler->write_context = context;
    led_scheduler_set_mode(scheduler, BOOT_LED_MODE_OFF, 0U);
}
void led_scheduler_set_mode(led_scheduler_t* scheduler, boot_led_mode_t mode, uint32_t now_ms) {
    scheduler->mode = mode;
    scheduler->previous_ms = now_ms;
    scheduler->interval_ms = mode_interval(mode);
    scheduler->logical_on = mode == BOOT_LED_MODE_BOOTING;
    led_write(scheduler);
}
void led_scheduler_poll(led_scheduler_t* scheduler, uint32_t now_ms) {
    if ((scheduler == NULL) || (scheduler->interval_ms == 0U))
        return;
    if ((uint32_t)(now_ms - scheduler->previous_ms) >= scheduler->interval_ms) {
        uint32_t elapsed = now_ms - scheduler->previous_ms;
        uint32_t periods = elapsed / scheduler->interval_ms;
        scheduler->previous_ms += periods * scheduler->interval_ms;
        if ((periods & 1U) != 0U)
            scheduler->logical_on = !scheduler->logical_on;
        led_write(scheduler);
    }
}

#if !defined(BOOT_HOST_TEST)
static led_scheduler_t board_led;
static bool board_ready;

static void board_gpio_write(bool level, void* context) {
    (void)context;
    if (level)
        GPIO_SetPins(BOOT_STATUS_LED_PORT, BOOT_STATUS_LED_PIN);
    else
        GPIO_ResetPins(BOOT_STATUS_LED_PORT, BOOT_STATUS_LED_PIN);
}
bool bsp_status_led_init(void) {
    stc_gpio_init_t init;
    board_ready = false;
    (void)GPIO_StructInit(&init);
    init.u16PinState = PIN_STAT_RST;
    init.u16PinDir = PIN_DIR_OUT;
    init.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    if (GPIO_Init(BOOT_STATUS_LED_PORT, BOOT_STATUS_LED_PIN, &init) != LL_OK)
        return false;
    if (GPIO_ReadOutputPins(BOOT_STATUS_LED_PORT, BOOT_STATUS_LED_PIN) != PIN_RESET)
        return false;
    led_scheduler_init(&board_led, BOOT_STATUS_LED_ACTIVE_LEVEL != 0U, board_gpio_write, NULL);
    board_ready = true;
    return true;
}
void bsp_status_led_set_mode(boot_led_mode_t mode) {
    if (board_ready)
        led_scheduler_set_mode(&board_led, mode, boot_time_ms());
}
void bsp_status_led_poll(uint32_t now_ms) {
    if (board_ready)
        led_scheduler_poll(&board_led, now_ms);
}
void bsp_status_led_off(void) {
    if (board_ready)
        led_scheduler_set_mode(&board_led, BOOT_LED_MODE_OFF, boot_time_ms());
}
bool bsp_status_led_is_ready(void) {
    return board_ready;
}
#endif
