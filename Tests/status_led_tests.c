#include <assert.h>

#include "bsp_status_led.h"

int main(void) {
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_OFF, 0U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_BOOT, 0U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_RECOVERY, 249U) == BSP_STATUS_LED_RED);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_RECOVERY, 250U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_RECOVERY, 1000U) == BSP_STATUS_LED_RED);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_UPDATE, 99U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_UPDATE, 100U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_UPDATE, 200U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_PRIMARY, 99U) == (BSP_STATUS_LED_RED | BSP_STATUS_LED_BLUE));
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_PRIMARY, 100U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_UPDATER, 0U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_ERROR, 500U) == BSP_STATUS_LED_RED);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_CHARGING, 499U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_CHARGING, 500U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_FULL, 777U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_LOW, 99U) == BSP_STATUS_LED_RED);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_LOW, 100U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_CRITICAL, 100U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_CRITICAL, 250U) == BSP_STATUS_LED_RED);
    const uint8_t double_blink[] = {BSP_STATUS_LED_RED, 0U, BSP_STATUS_LED_RED, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    for (uint32_t i = 0U; i < 10U; ++i)
        assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_NO_PACK, 1000U + i * 100U + 50U) == double_blink[i]);
    return 0;
}
