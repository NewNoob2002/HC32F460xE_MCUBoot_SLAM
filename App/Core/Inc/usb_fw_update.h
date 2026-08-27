#ifndef USB_FW_UPDATE_H
#define USB_FW_UPDATE_H

#include <stdint.h>

enum usb_fw_update_action {
    USB_FW_UPDATE_ACTION_NONE = 0,
    USB_FW_UPDATE_ACTION_RESET,
};

int usb_fw_update_init(void);
enum usb_fw_update_action usb_fw_update_poll(uint32_t now_ms);

#endif
