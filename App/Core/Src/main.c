#include "app_confirm.h"
#include "bsp_clock.h"
#if APP_PHASE2_HIL_MODE != 0
#include "phase2_hil.h"
#endif

volatile int g_app_confirm_result;

int main(void) {
    bsp_clock_init();
#if APP_PHASE2_HIL_MODE != 0
    g_app_confirm_result = phase2_hil_run(APP_PHASE2_HIL_MODE);
#else
    g_app_confirm_result = app_confirm_running_image(APP_AUTO_CONFIRM != 0);
#endif
    for (;;) {}
}
