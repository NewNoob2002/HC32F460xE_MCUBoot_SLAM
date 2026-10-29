#ifndef BSP_BOOT_REQUEST_H
#define BSP_BOOT_REQUEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * One-shot Application -> Boot request kept in the 4 KiB retention SRAM, which startup code never initializes at this
 * address. The Application sets it immediately before a software reset; Boot takes (and clears) it once.
 */
void bsp_boot_request_recovery(void);
/** Returns true once if a recovery request was set and the reset was a software reset. Always clears the request. */
bool bsp_boot_request_take_recovery(uint32_t reset_flags);

#ifdef __cplusplus
}
#endif

#endif
