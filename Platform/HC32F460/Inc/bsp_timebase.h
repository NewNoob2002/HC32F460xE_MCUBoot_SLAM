#ifndef BSP_TIMEBASE_H
#define BSP_TIMEBASE_H

#include <stdbool.h>
#include <stdint.h>

bool bsp_timebase_init(void);
uint32_t bsp_millis(void);
void bsp_delay_ms(uint32_t milliseconds);
void bsp_delay_us(uint32_t microseconds);

#endif
