#include <assert.h>
#include <string.h>
#include "usb_debug_link.h"
#include "usbd_core.h"

static struct usbd_endpoint* out_ep;
static struct usbd_endpoint* in_ep;
static uint8_t* rx;
static uint32_t read_calls, write_calls, received, resets;
static const uint8_t* tx;
static uint32_t tx_size;
static int write_result;
void usbd_add_interface(uint8_t busid, struct usbd_interface* interface) {
    assert(busid == 0U && interface != NULL);
}
void usbd_add_endpoint(uint8_t busid, struct usbd_endpoint* ep) {
    assert(busid == 0U);
    if (ep->ep_addr == USB_DEBUG_OUT_EP)
        out_ep = ep;
    else {
        assert(ep->ep_addr == USB_DEBUG_IN_EP);
        in_ep = ep;
    }
}
int usbd_ep_start_read(uint8_t busid, uint8_t ep, uint8_t* data, uint32_t size) {
    assert(busid == 0U && ep == USB_DEBUG_OUT_EP && size == 64U);
    rx = data;
    ++read_calls;
    return 0;
}
int usbd_ep_start_write(uint8_t busid, uint8_t ep, const uint8_t* data, uint32_t size) {
    assert(busid == 0U && ep == USB_DEBUG_IN_EP && ((uintptr_t)data % 4U) == 0U);
    tx = data;
    tx_size = size;
    ++write_calls;
    return write_result;
}
static void receive(void* context, const uint8_t* data, size_t size, uint32_t now) {
    assert(context == &received && size == 3U && now == 2U && data[0] == 42U);
    ++received;
}
static void reset(void* context) {
    assert(context == &received);
    ++resets;
}
static void complete(uint32_t now) {
    in_ep->ep_cb(0U, USB_DEBUG_IN_EP, tx_size);
    usb_debug_link_poll(now);
}
int main(void) {
    const uint8_t event[20] = {11U};
    const uint8_t response[20] = {22U};
    usb_debug_link_register();
    usb_debug_link_bind(&received, receive, reset);
    assert(out_ep && in_ep);
    usb_debug_link_event(USBD_EVENT_CONFIGURED);
    assert(resets == 0U); // ISR only records flags.
    usb_debug_link_poll(1U);
    assert(resets == 1U && read_calls == 1U);
    assert(usb_debug_link_send(event, sizeof(event), false));
    assert(usb_debug_link_send(event, sizeof(event), false));
    assert(usb_debug_link_send(event, sizeof(event), false));
    assert(!usb_debug_link_send(event, sizeof(event), false));
    assert(usb_debug_link_dropped() == 1U);
    assert(usb_debug_link_response_available());
    assert(usb_debug_link_send(response, sizeof(response), true));
    assert(!usb_debug_link_response_available());
    assert(!usb_debug_link_send(response, sizeof(response), true));
    usb_debug_link_poll(2U);
    assert(write_calls == 1U && tx[0] == 22U); // Response slot has priority.
    rx[0] = 42U;
    out_ep->ep_cb(0U, USB_DEBUG_OUT_EP, 3U);
    assert(received == 0U);
    usb_debug_link_poll(2U);
    assert(received == 1U && read_calls == 2U); // OUT rearmed while IN remains blocked.
    complete(3U);
    assert(!usb_debug_link_response_pending() && tx[0] == 11U);
    assert(!usb_debug_link_send(event, sizeof(event), false)); // Active event owns its slot.
    complete(4U);
    assert(usb_debug_link_send(event, sizeof(event), false));
    const uint32_t before_reset = write_calls;
    usb_debug_link_event(USBD_EVENT_RESET);
    assert(resets == 1U);
    usb_debug_link_poll(5U);
    assert(resets == 2U && !usb_debug_link_response_pending());
    usb_debug_link_event(USBD_EVENT_CONFIGURED);
    usb_debug_link_poll(6U);
    assert(write_calls == before_reset && usb_debug_link_response_available());
    // A 64-byte response is not drained until the terminating ZLP completes.
    const uint8_t packet[64] = {33U};
    assert(usb_debug_link_send(packet, sizeof(packet), true));
    usb_debug_link_poll(7U);
    complete(8U);
    assert(tx_size == 0U && usb_debug_link_response_pending());
    complete(9U);
    assert(!usb_debug_link_response_pending());
    // SET_CONFIGURATION clears active transfers and ignores completions captured with it.
    assert(usb_debug_link_send(response, sizeof(response), true));
    usb_debug_link_poll(10U);
    in_ep->ep_cb(0U, USB_DEBUG_IN_EP, tx_size);
    usb_debug_link_event(USBD_EVENT_CONFIGURED);
    usb_debug_link_poll(11U);
    assert(!usb_debug_link_response_pending());
    write_result = -1;
    assert(usb_debug_link_send(response, sizeof(response), true));
    usb_debug_link_poll(12U);
    assert(!usb_debug_link_response_pending() && resets == 5U);
    return 0;
}
