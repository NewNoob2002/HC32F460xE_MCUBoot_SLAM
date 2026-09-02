#ifndef CORE_DEBUG_H
#define CORE_DEBUG_H

#if defined(BSP_DEBUG)
#include "elog.h"

void core_debug_init(void);

#define CORE_DEBUG_INIT()           core_debug_init()
#define CORE_DEBUG_PRINTF(fmt, ...) elog_d("core", fmt, ##__VA_ARGS__)
#else
#define CORE_DEBUG_INIT()      ((void)0)
#define CORE_DEBUG_PRINTF(...) ((void)0)
#endif

#endif
