#ifndef BSP_DEBUG_PORT_H
#define BSP_DEBUG_PORT_H
#include <stdint.h>
void bsp_debug_port_configure_for_boot_gpio(void);
uint16_t bsp_debug_port_pspcr(void);
#endif
