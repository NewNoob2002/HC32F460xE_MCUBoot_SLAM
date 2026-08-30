#ifndef USB_BSP_H
#define USB_BSP_H

#include <stdint.h>

#include "hc32_ll_utility.h"

void usb_udelay(uint32_t usec);
void usb_mdelay(uint32_t msec);

#endif
