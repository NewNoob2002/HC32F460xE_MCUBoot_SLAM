#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

bool bsp_clock_init(void);
#include <stdint.h>

/** Re-clocks SYSCLK and every bus to `hz` (25..50 MHz, 400 MHz divisible) from MPLL after bsp_clock_init(), before
 * bsp_timebase_init() and peripheral init. USB keeps its own UPLL. Must not run while Flash is being programmed. */
bool bsp_clock_set_pll_hz(uint32_t hz);

#ifdef __cplusplus
}
#endif

#endif
