#include "app_confirm.h"
#include "bsp_clock.h"

volatile int g_app_confirm_result;

int main(void) {
    bsp_clock_init();
    g_app_confirm_result = app_confirm_running_image(APP_AUTO_CONFIRM != 0);
    for (;;) {
    }
}
