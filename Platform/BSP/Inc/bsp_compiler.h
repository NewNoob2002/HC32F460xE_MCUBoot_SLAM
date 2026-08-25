#pragma once
#include <stdint.h>
#if defined(__GNUC__) || defined(__clang__)
#include <cmsis_gcc.h>
#define BSP_ATTR_UNUSED        __attribute__((unused))
#define BSP_ATTR_ALWAYS_INLINE __attribute__((always_inline))
#else
#define BSP_ATTR_UNUSED
#define BSP_ATTR_ALWAYS_INLINE
#endif