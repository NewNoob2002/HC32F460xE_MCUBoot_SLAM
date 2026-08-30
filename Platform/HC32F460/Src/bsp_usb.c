#include "bsp_usb.h"

#include <stddef.h>

#include "hc32f460.h"

uint32_t bsp_usb_device_base(void) {
    return CM_USBFS_BASE;
}

bool bsp_usb_serial_id(uint32_t words[BSP_USB_SERIAL_ID_WORDS]) {
    if (words == NULL)
        return false;

    words[0] = CM_EFM->UQID0;
    words[1] = CM_EFM->UQID1;
    words[2] = CM_EFM->UQID2;
    const uint32_t all_bits = words[0] & words[1] & words[2];
    const uint32_t any_bits = words[0] | words[1] | words[2];
    return any_bits != 0U && all_bits != UINT32_MAX;
}
