#include "bsp_reset.h"
#include "hc32_ll_rmu.h"
uint32_t bsp_reset_capture(void) {
    /* Preserve the latched reset cause for the application after direct handover. */
    uint32_t flags = READ_REG32_BIT(CM_RMU->RSTF0, RMU_FLAG_ALL);
    return flags;
}
bool bsp_reset_was_software(uint32_t flags) {
    return (flags & RMU_FLAG_SW) != 0U;
}
void bsp_system_reset(void) {
    NVIC_SystemReset();
    for (;;) {}
}
