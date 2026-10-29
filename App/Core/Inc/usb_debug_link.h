#ifndef USB_DEBUG_LINK_H
#define USB_DEBUG_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define USB_DEBUG_MAX_FRAME 276U
#define USB_DEBUG_OUT_EP    0x04U
#define USB_DEBUG_IN_EP     0x83U

/** All functions except event() and USB completion callbacks run in poll context. */
typedef void (*usb_debug_receive_fn)(void* context, const uint8_t* data, size_t length, uint32_t now_ms);
typedef void (*usb_debug_reset_fn)(void* context);
void usb_debug_link_register(void);
void usb_debug_link_bind(void* context, usb_debug_receive_fn receive, usb_debug_reset_fn reset);
void usb_debug_link_event(uint8_t event);
void usb_debug_link_poll(uint32_t now_ms);
bool usb_debug_link_send(const uint8_t* frame, size_t length, bool response);
bool usb_debug_link_response_available(void);
bool usb_debug_link_response_pending(void);
/** Free event slots in the IN queue; 0 while the link is not configured. */
uint8_t usb_debug_link_event_slots_free(void);
uint32_t usb_debug_link_dropped(void);
#ifdef __cplusplus
}
#endif
#endif
