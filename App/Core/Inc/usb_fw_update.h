#ifndef USB_FW_UPDATE_H
#define USB_FW_UPDATE_H

#include <stdint.h>

enum usb_fw_update_action {
    USB_FW_UPDATE_ACTION_NONE = 0,
    USB_FW_UPDATE_ACTION_RESET,
};

struct usb_fw_update_status {
    uint32_t errors;
    int last_result;
    int manager_state;
    uint8_t configured;
};

int usb_fw_update_init(void);
enum usb_fw_update_action usb_fw_update_poll(uint32_t now_ms);
void usb_fw_update_get_status(struct usb_fw_update_status* status);

#endif
