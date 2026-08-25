#pragma once

#include <stdint.h>
#if !defined(BOOT_HOST_TEST)
#include "bsp_compiler.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t bsp_irq_state_t;

#if defined(BOOT_HOST_TEST)
static inline bsp_irq_state_t bsp_enter_critical(void) {
    return 0U;
}

static inline void bsp_exit_critical(bsp_irq_state_t state) {
    (void)state;
}
#else
/**
 * @brief Enter a short IRQ critical section and save previous PRIMASK.
 */
static inline BSP_ATTR_UNUSED BSP_ATTR_ALWAYS_INLINE bsp_irq_state_t bsp_enter_critical(void) {
    bsp_irq_state_t state = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return state;
}

/**
 * @brief Exit a short IRQ critical section and restore previous PRIMASK.
 */
static inline BSP_ATTR_UNUSED BSP_ATTR_ALWAYS_INLINE void bsp_exit_critical(bsp_irq_state_t state) {
    __DMB();
    __set_PRIMASK(state);
}
#endif

#ifdef __cplusplus
}
#endif
