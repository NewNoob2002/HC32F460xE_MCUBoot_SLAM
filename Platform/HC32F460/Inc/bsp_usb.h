#ifndef BSP_USB_H
#define BSP_USB_H

#include <stdbool.h>
#include <stdint.h>

#define BSP_USB_SERIAL_ID_WORDS 3U

uint32_t bsp_usb_device_base(void);
bool bsp_usb_serial_id(uint32_t words[BSP_USB_SERIAL_ID_WORDS]);

#endif
