#include "bsp_debug_port.h"
#include "bsp_board_config.h"
#include "hc32_ll.h"
void bsp_debug_port_configure_for_boot_gpio(void) {
    WRITE_REG16(CM_GPIO->PSPCR, BOOT_DEBUG_PSPCR_VALUE);
    __DSB();
    __ISB();
}
uint16_t bsp_debug_port_pspcr(void) {
    return READ_REG16(CM_GPIO->PSPCR);
}
