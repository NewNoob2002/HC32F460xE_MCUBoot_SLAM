#include "usb_debug_link.h"
#include <string.h>
#include "bsp_critical.h"
#include "usbd_core.h"

#define BUS_ID 0U
#define MPS    64U
#define SLOTS  4U

struct frame_slot {
    USB_MEM_ALIGNX uint8_t bytes[USB_DEBUG_MAX_FRAME];
    uint32_t length;
};
static struct frame_slot frames[SLOTS];
USB_MEM_ALIGNX static uint8_t rx[MPS];
static volatile uint8_t link_changed, configured, rx_ready, tx_ready;
static volatile uint32_t rx_length, tx_length;
static uint8_t event_head, event_count, active_slot;
static bool response_used, active, zlp, rx_armed;
static uint32_t offset, dropped;
static void* service;
static usb_debug_receive_fn receive_fn;
static usb_debug_reset_fn reset_fn;

static void out_complete(uint8_t busid, uint8_t ep, uint32_t length) {
    (void)busid;
    (void)ep;
    rx_length = length;
    rx_ready = 1U;
}
static void in_complete(uint8_t busid, uint8_t ep, uint32_t length) {
    (void)busid;
    (void)ep;
    tx_length = length;
    tx_ready = 1U;
}
static struct usbd_interface interface;
static struct usbd_endpoint out_ep = {.ep_addr = USB_DEBUG_OUT_EP, .ep_cb = out_complete};
static struct usbd_endpoint in_ep = {.ep_addr = USB_DEBUG_IN_EP, .ep_cb = in_complete};
void usb_debug_link_register(void) {
    usbd_add_interface(BUS_ID, &interface);
    usbd_add_endpoint(BUS_ID, &out_ep);
    usbd_add_endpoint(BUS_ID, &in_ep);
}
void usb_debug_link_bind(void* context, usb_debug_receive_fn receive, usb_debug_reset_fn reset) {
    service = context;
    receive_fn = receive;
    reset_fn = reset;
}
void usb_debug_link_event(uint8_t event) {
    if (event == USBD_EVENT_CONFIGURED || event == USBD_EVENT_RESET || event == USBD_EVENT_DISCONNECTED) {
        configured = event == USBD_EVENT_CONFIGURED ? 1U : 0U;
        link_changed = 1U;
    }
}
static void clear_queue(void) {
    event_head = 0U;
    event_count = 0U;
    response_used = false;
    active = false;
    zlp = false;
    offset = 0U;
    if (reset_fn != NULL)
        reset_fn(service);
}
bool usb_debug_link_response_available(void) {
    return configured != 0U && link_changed == 0U && !response_used;
}
bool usb_debug_link_response_pending(void) {
    return response_used;
}
uint32_t usb_debug_link_dropped(void) {
    return dropped;
}
bool usb_debug_link_send(const uint8_t* frame, size_t length, bool response) {
    if (frame == NULL || length == 0U || length > USB_DEBUG_MAX_FRAME)
        return false;
    if (configured == 0U || link_changed != 0U || (response ? response_used : event_count == SLOTS - 1U)) {
        if (!response)
            ++dropped;
        return false;
    }
    const uint8_t slot = response ? 0U : (uint8_t)(1U + ((event_head + event_count) % (SLOTS - 1U)));
    memcpy(frames[slot].bytes, frame, length);
    frames[slot].length = (uint32_t)length;
    if (response)
        response_used = true;
    else
        ++event_count;
    return true;
}
static int start_write(void) {
    return usbd_ep_start_write(BUS_ID, USB_DEBUG_IN_EP, frames[active_slot].bytes + offset,
                               zlp ? 0U : frames[active_slot].length - offset);
}
static void finish_frame(void) {
    if (active_slot == 0U)
        response_used = false;
    else {
        event_head = (uint8_t)((event_head + 1U) % (SLOTS - 1U));
        --event_count;
    }
    active = false;
}
void usb_debug_link_poll(uint32_t now_ms) {
    uint8_t changed, have_rx, have_tx;
    uint32_t received, sent;
    const bsp_irq_state_t irq = bsp_enter_critical();
    changed = link_changed;
    link_changed = 0U;
    have_rx = rx_ready;
    rx_ready = 0U;
    have_tx = tx_ready;
    tx_ready = 0U;
    received = rx_length;
    sent = tx_length;
    bsp_exit_critical(irq);
    if (changed != 0U) {
        clear_queue();
        rx_armed = false;
        have_rx = 0U;
        have_tx = 0U;
    }
    if (configured == 0U)
        return;
    if (have_tx != 0U && active) {
        const uint32_t remaining = frames[active_slot].length - offset;
        if (zlp && sent == 0U) {
            finish_frame();
        } else if (sent == 0U || sent > remaining || zlp) {
            clear_queue();
        } else {
            offset += sent;
            if (offset == frames[active_slot].length) {
                if ((offset % MPS) == 0U) {
                    zlp = true;
                    if (start_write() != 0)
                        clear_queue();
                } else
                    finish_frame();
            } else if ((offset % 4U) != 0U || start_write() != 0)
                clear_queue();
        }
    }
    if (have_rx != 0U) {
        rx_armed = false;
        if (received <= sizeof(rx) && receive_fn != NULL)
            receive_fn(service, rx, received, now_ms);
        else
            clear_queue();
    }
    // OUT is independent of IN, including when a host stops reading events/responses.
    if (!rx_armed && usbd_ep_start_read(BUS_ID, USB_DEBUG_OUT_EP, rx, sizeof(rx)) == 0)
        rx_armed = true;
    if (!active && (response_used || event_count != 0U)) {
        active_slot = response_used ? 0U : (uint8_t)(1U + event_head);
        offset = 0U;
        zlp = false;
        active = true;
        if (start_write() != 0)
            clear_queue();
    }
}
