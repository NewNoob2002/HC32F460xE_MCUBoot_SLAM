#ifndef BSP_WRITE_PROTECTION_H
#define BSP_WRITE_PROTECTION_H
void bsp_write_protection_unlock(void);
void bsp_write_protection_restore(void);
/* Requires write protection to be unlocked by the caller. */
void bsp_debug_port_use_swd(void);
#endif
