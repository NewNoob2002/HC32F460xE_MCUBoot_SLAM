#include "boot_handover.h"
#include "bsp_clock.h"
#include "bootutil/bootutil.h"
#include "hc32f460.h"

int main(void) {
    struct boot_rsp rsp = {0};
    FIH_DECLARE(rc, FIH_FAILURE);

    bsp_clock_init();
    FIH_CALL(boot_go, rc, &rsp);
    if (FIH_NOT_EQ(rc, FIH_SUCCESS)) {
        for (;;) {
            __WFI();
        }
    }
    boot_handover(&rsp);
}
