#ifndef BSP_PANIC_H
#define BSP_PANIC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
[[noreturn]]
#else
_Noreturn
#endif
void bsp_panic(const char* reason);

#ifdef __cplusplus
}
#endif

#endif
