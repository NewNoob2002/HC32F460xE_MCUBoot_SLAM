#ifndef CORE_DEBUG_H
#define CORE_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(BSP_DEBUG)
#include <stddef.h>

#include "elog.h"

void core_debug_init(void);
/** Receives every formatted EasyLogger line after UART/RTT output. The weak default discards it;
 *  the Application forwards it to Debug Link. Called in the caller's context. */
void core_debug_forward(const char* line, size_t size);

#define CORE_DEBUG_INIT()           core_debug_init()
#define CORE_DEBUG_PRINTF(fmt, ...) elog_d("core", fmt, ##__VA_ARGS__)
#else
#define CORE_DEBUG_INIT()      ((void)0)
#define CORE_DEBUG_PRINTF(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
