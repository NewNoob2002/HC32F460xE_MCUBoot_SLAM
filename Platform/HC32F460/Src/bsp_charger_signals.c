#include "bsp_charger_signals.h"

#include <stddef.h>

#include "bsp_board_config.h"
#include "hc32_ll_gpio.h"

static bool g_ready;

bool bsp_charger_signals_init(void) {
    stc_gpio_init_t init;

    g_ready = false;
    if (GPIO_StructInit(&init) != LL_OK)
        return false;
    /* Both nets are open-drain with board pull-ups; enabling the internal pull-up would only add leakage. */
    init.u16PinDir = PIN_DIR_IN;
    init.u16PullUp = PIN_PU_OFF;
    if (GPIO_Init(BSP_CHARGER_SIGNAL_PORT, BSP_CHARGER_INT_PIN | BSP_CHARGER_PG_PIN, &init) != LL_OK)
        return false;
    g_ready = true;
    return true;
}

bool bsp_charger_signals_read(struct bsp_charger_signals* signals) {
    if (!g_ready || signals == NULL)
        return false;
    signals->interrupt_asserted = GPIO_ReadInputPins(BSP_CHARGER_SIGNAL_PORT, BSP_CHARGER_INT_PIN) == PIN_RESET;
    signals->power_good = GPIO_ReadInputPins(BSP_CHARGER_SIGNAL_PORT, BSP_CHARGER_PG_PIN) == PIN_SET;
    return true;
}
