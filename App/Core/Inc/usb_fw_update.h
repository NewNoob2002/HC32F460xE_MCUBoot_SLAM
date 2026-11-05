#ifndef USB_FW_UPDATE_H
#define USB_FW_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

enum usb_fw_update_action {
    USB_FW_UPDATE_ACTION_NONE = 0,
    USB_FW_UPDATE_ACTION_RESET,
    /** Reset into Boot recovery; the caller records the request before resetting. */
    USB_FW_UPDATE_ACTION_RESET_TO_RECOVERY,
    /** Boot only: an authenticated administrator requested erasing both slots, scratch and Product Config. */
    USB_FW_UPDATE_ACTION_ERASE_AND_RESET,
};

enum usb_fw_update_activity {
    USB_FW_UPDATE_ACTIVITY_IDLE = 0,
    USB_FW_UPDATE_ACTIVITY_ACTIVE,
    USB_FW_UPDATE_ACTIVITY_ERROR,
};

struct usb_fw_update_status {
    uint32_t errors;
    int last_result;
    int manager_state;
    enum usb_fw_update_activity activity;
    uint8_t configured;
};

int usb_fw_update_init(void);
const char* usb_fw_update_serial(void);
enum usb_fw_update_action usb_fw_update_poll(uint32_t now_ms);
void usb_fw_update_get_status(struct usb_fw_update_status* status);
enum usb_fw_update_activity usb_fw_update_activity_from_manager_state(int manager_state);

#ifdef __cplusplus
}
#endif

#endif
