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
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_PRIMARY, 99U) == BSP_STATUS_LED_GREEN);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_PRIMARY, 100U) == 0U);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_UPDATER, 0U) == BSP_STATUS_LED_BLUE);
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_APP_DIAGNOSTIC, 0U)
           == (BSP_STATUS_LED_GREEN | BSP_STATUS_LED_BLUE));
    assert(bsp_status_led_sample(BSP_STATUS_LED_MODE_ERROR, 500U) == BSP_STATUS_LED_RED);
    return 0;
}
