#include "bsp_isr.h"

#include "bsp_compiler.h"

bool bsp_in_isr(void) {
    return __get_IPSR() != 0U;
}

void bsp_wait_for_interrupt(void) {
    __WFI();
}
