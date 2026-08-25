#include "bsp_write_protection.h"
#include "bsp_board_config.h"
#include "hc32_ll.h"
void bsp_write_protection_unlock(void) {
    LL_PERIPH_WE(CONFIG_PERIPH_WE);
}
void bsp_write_protection_restore(void) {
    LL_PERIPH_WP(CONFIG_PERIPH_WP);
}
