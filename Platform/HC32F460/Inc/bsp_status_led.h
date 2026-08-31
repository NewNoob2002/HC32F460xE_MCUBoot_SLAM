#ifndef BSP_STATUS_LED_H
#define BSP_STATUS_LED_H

#include <stdbool.h>
#include <stdint.h>

enum bsp_status_led_channel {
    BSP_STATUS_LED_RED = 1U << 0,
    BSP_STATUS_LED_GREEN = 1U << 1,
    BSP_STATUS_LED_BLUE = 1U << 2,
};

enum bsp_status_led_mode {
    BSP_STATUS_LED_MODE_OFF = 0,
    BSP_STATUS_LED_MODE_BOOT,
    BSP_STATUS_LED_MODE_RECOVERY,
    BSP_STATUS_LED_MODE_UPDATE,
    BSP_STATUS_LED_MODE_APP_PRIMARY,
    BSP_STATUS_LED_MODE_APP_UPDATER,
    BSP_STATUS_LED_MODE_APP_DIAGNOSTIC,
    BSP_STATUS_LED_MODE_ERROR,
};

bool bsp_status_led_init(void);
void bsp_status_led_set_mode(enum bsp_status_led_mode mode);
void bsp_status_led_poll(uint32_t now_ms);
uint8_t bsp_status_led_sample(enum bsp_status_led_mode mode, uint32_t now_ms);

#endif
