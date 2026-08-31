#include "bsp_status_led.h"

#define STATUS_LED_SLOW_PERIOD_MS      1000U
#define STATUS_LED_SLOW_ON_MS          250U
#define STATUS_LED_FAST_HALF_PERIOD_MS 100U
#define STATUS_LED_HEARTBEAT_ON_MS     100U

#if !defined(BOOT_HOST_TEST)
#include "bsp_board_config.h"
#include "hc32_ll.h"
#endif

static enum bsp_status_led_mode g_mode;
static uint8_t g_channels;
static bool g_ready;

uint8_t bsp_status_led_sample(enum bsp_status_led_mode mode, uint32_t now_ms) {
    switch (mode) {
        case BSP_STATUS_LED_MODE_BOOT:
            return BSP_STATUS_LED_BLUE;
        case BSP_STATUS_LED_MODE_RECOVERY:
            return now_ms % STATUS_LED_SLOW_PERIOD_MS < STATUS_LED_SLOW_ON_MS ? BSP_STATUS_LED_RED : 0U;
        case BSP_STATUS_LED_MODE_UPDATE:
            return (now_ms / STATUS_LED_FAST_HALF_PERIOD_MS) % 2U == 0U ? BSP_STATUS_LED_BLUE : 0U;
        case BSP_STATUS_LED_MODE_APP_PRIMARY:
            return now_ms % STATUS_LED_SLOW_PERIOD_MS < STATUS_LED_HEARTBEAT_ON_MS ? BSP_STATUS_LED_GREEN : 0U;
        case BSP_STATUS_LED_MODE_APP_UPDATER:
            return now_ms % STATUS_LED_SLOW_PERIOD_MS < STATUS_LED_HEARTBEAT_ON_MS ? BSP_STATUS_LED_BLUE : 0U;
        case BSP_STATUS_LED_MODE_APP_DIAGNOSTIC:
            return now_ms % STATUS_LED_SLOW_PERIOD_MS < STATUS_LED_HEARTBEAT_ON_MS
                       ? BSP_STATUS_LED_GREEN | BSP_STATUS_LED_BLUE
                       : 0U;
        case BSP_STATUS_LED_MODE_ERROR:
            return BSP_STATUS_LED_RED;
        case BSP_STATUS_LED_MODE_OFF:
        default:
            return 0U;
    }
}

static void apply_channels(uint8_t channels) {
    if (!g_ready || channels == g_channels)
        return;
#if !defined(BOOT_HOST_TEST)
    const uint8_t changed = channels ^ g_channels;
    if ((changed & BSP_STATUS_LED_RED) != 0U) {
        if ((channels & BSP_STATUS_LED_RED) != 0U)
            GPIO_ResetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_RED_PIN);
        else
            GPIO_SetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_RED_PIN);
    }
    if ((changed & BSP_STATUS_LED_GREEN) != 0U) {
        if ((channels & BSP_STATUS_LED_GREEN) != 0U)
            GPIO_ResetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_GREEN_PIN);
        else
            GPIO_SetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_GREEN_PIN);
    }
    if ((changed & BSP_STATUS_LED_BLUE) != 0U) {
        if ((channels & BSP_STATUS_LED_BLUE) != 0U)
            GPIO_ResetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_BLUE_PIN);
        else
            GPIO_SetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_BLUE_PIN);
    }
#endif
    g_channels = channels;
}

bool bsp_status_led_init(void) {
    g_ready = false;
    g_mode = BSP_STATUS_LED_MODE_OFF;
    g_channels = 0U;
#if !defined(BOOT_HOST_TEST)
    stc_gpio_init_t init;
    GPIO_SetPins(BSP_STATUS_LED_PORT, BSP_STATUS_LED_PINS);
    (void)GPIO_StructInit(&init);
    init.u16PinState = PIN_STAT_SET;
    init.u16PinDir = PIN_DIR_OUT;
    init.u16PinOutputType = PIN_OUT_TYPE_CMOS;
    if (GPIO_Init(BSP_STATUS_LED_PORT, BSP_STATUS_LED_PINS, &init) != LL_OK)
        return false;
#endif
    g_ready = true;
    return true;
}

void bsp_status_led_set_mode(enum bsp_status_led_mode mode) {
    if (mode > BSP_STATUS_LED_MODE_ERROR)
        mode = BSP_STATUS_LED_MODE_ERROR;
    if (g_mode == mode)
        return;
    g_mode = mode;
    apply_channels(bsp_status_led_sample(mode, 0U));
}

void bsp_status_led_poll(uint32_t now_ms) {
    apply_channels(bsp_status_led_sample(g_mode, now_ms));
}
