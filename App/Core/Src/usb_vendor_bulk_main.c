#include "bsp_clock.h"
#include "hc32f460.h"
#include "usb_vendor_bulk.h"

volatile int g_usb_vendor_bulk_init_result;

int main(void)
{
    bsp_clock_init();
    g_usb_vendor_bulk_init_result = usb_vendor_bulk_init();
    for (;;) {
        __WFI();
    }
}
