#ifndef BSP_ISR_H
#define BSP_ISR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool bsp_in_isr(void);
void bsp_wait_for_interrupt(void);

#ifdef __cplusplus
}
#endif

#endif
