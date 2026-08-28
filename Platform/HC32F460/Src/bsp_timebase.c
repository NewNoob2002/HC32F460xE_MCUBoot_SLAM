#include "bsp_timebase.h"

#include <limits.h>

#include "hc32f460.h"
#include "system_hc32f460.h"

static volatile uint32_t g_milliseconds;
static uint32_t g_cycles_per_microsecond;
static bool g_timebase_ready;

bool bsp_timebase_init(void) {
    if (SystemCoreClock < 1000000UL || (DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0UL)
        return false;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0UL;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_cycles_per_microsecond = SystemCoreClock / 1000000UL;
    g_milliseconds = 0UL;
    if (SysTick_Config(SystemCoreClock / 1000UL) != 0UL) {
        g_cycles_per_microsecond = 0UL;
        return false;
    }
    g_timebase_ready = true;
    return true;
}

uint32_t bsp_millis(void) {
    return g_milliseconds;
}

void bsp_delay_ms(uint32_t milliseconds) {
    if (!g_timebase_ready)
        return;

    const uint32_t started = bsp_millis();
    while ((uint32_t)(bsp_millis() - started) < milliseconds)
        __NOP();
}

void bsp_delay_us(uint32_t microseconds) {
    if (!g_timebase_ready || g_cycles_per_microsecond == 0UL)
        return;

    const uint32_t maximum_chunk = UINT32_MAX / g_cycles_per_microsecond;
    while (microseconds > 0UL) {
        const uint32_t chunk = microseconds > maximum_chunk ? maximum_chunk : microseconds;
        const uint32_t cycles = chunk * g_cycles_per_microsecond;
        const uint32_t started = DWT->CYCCNT;
        while ((uint32_t)(DWT->CYCCNT - started) < cycles)
            __NOP();
        microseconds -= chunk;
    }
}

void SysTick_Handler(void) {
    ++g_milliseconds;
}
