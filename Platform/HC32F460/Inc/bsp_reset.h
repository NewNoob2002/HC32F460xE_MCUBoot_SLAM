#ifndef BSP_RESET_H
#define BSP_RESET_H
#include <stdbool.h>
#include <stdint.h>
uint32_t bsp_reset_capture(void);
bool bsp_reset_was_software(uint32_t flags);
void bsp_system_reset(void);
#endif
