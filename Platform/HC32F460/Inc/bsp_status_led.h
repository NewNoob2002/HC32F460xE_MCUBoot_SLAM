#ifndef BSP_STATUS_LED_H
#define BSP_STATUS_LED_H
#include <stdbool.h>
#include <stdint.h>
typedef enum {
    BOOT_LED_MODE_OFF = 0,
    BOOT_LED_MODE_BOOTING,
    BOOT_LED_MODE_UPDATE_WINDOW,
    BOOT_LED_MODE_RECOVERY,
    BOOT_LED_MODE_FATAL
} boot_led_mode_t;
typedef void (*led_gpio_write_t)(bool level, void* context);
typedef struct {
    boot_led_mode_t mode;
    uint32_t previous_ms;
    uint32_t interval_ms;
    bool active_high;
    bool logical_on;
    led_gpio_write_t write;
    void* write_context;
} led_scheduler_t;
void led_scheduler_init(led_scheduler_t* scheduler, bool active_high, led_gpio_write_t write, void* context);
void led_scheduler_set_mode(led_scheduler_t* scheduler, boot_led_mode_t mode, uint32_t now_ms);
void led_scheduler_poll(led_scheduler_t* scheduler, uint32_t now_ms);
bool bsp_status_led_init(void);
void bsp_status_led_set_mode(boot_led_mode_t mode);
void bsp_status_led_poll(uint32_t now_ms);
void bsp_status_led_off(void);
bool bsp_status_led_is_ready(void);
#endif
