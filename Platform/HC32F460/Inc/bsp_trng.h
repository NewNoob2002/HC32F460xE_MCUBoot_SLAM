#ifndef BSP_TRNG_H
#define BSP_TRNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Enables the TRNG clock and discards its warm-up output. Call inside the caller-owned write-protection window. */
bool bsp_trng_init(void);
/** Fills output with hardware random bytes; returns false if the TRNG is not initialized or times out. */
bool bsp_trng_read(uint8_t* output, size_t length);

#endif
