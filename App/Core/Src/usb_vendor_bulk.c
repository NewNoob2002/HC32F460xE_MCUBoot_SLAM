#include <stdint.h>

#include "bsp_usb.h"
#include "usb_vendor_bulk.h"
#include "usbd_core.h"

#define LOOPBACK_BUS_ID      0U
#define LOOPBACK_IN_EP       0x81U
#define LOOPBACK_OUT_EP      0x02U
#define LOOPBACK_MPS         64U
#define LOOPBACK_BUFFER_SIZE 1024U

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0xFFFE, 0xFFFF, 0x0001, 0x01)};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(9 + 9 + 7 + 7, 1, 1, USB_CONFIG_BUS_POWERED, 100),
    USB_INTERFACE_DESCRIPTOR_INIT(0, 0, 2, 0xFF, 0x00, 0x00, 4),
    USB_ENDPOINT_DESCRIPTOR_INIT(LOOPBACK_IN_EP, USB_ENDPOINT_TYPE_BULK, LOOPBACK_MPS, 0),
    USB_ENDPOINT_DESCRIPTOR_INIT(LOOPBACK_OUT_EP, USB_ENDPOINT_TYPE_BULK, LOOPBACK_MPS, 0),
};

static const char* string_descriptors[] = {
    (const char[]){0x09, 0x04}, "HC32 Phase 4 Lab",     "CherryUSB Vendor Bulk Loopback",
    "HC32F460-PHASE4-0001",     "Vendor Bulk Loopback",
};

static const uint8_t* device_descriptor_get(uint8_t speed) {
    (void)speed;
    return device_descriptor;
}

static const uint8_t* config_descriptor_get(uint8_t speed) {
    (void)speed;
    return config_descriptor;
}

static const char* string_descriptor_get(uint8_t speed, uint8_t index) {
    (void)speed;
    if (index >= (sizeof(string_descriptors) / sizeof(string_descriptors[0]))) {
        return NULL;
    }
    return string_descriptors[index];
}

static const struct usb_descriptor descriptors = {
    .device_descriptor_callback = device_descriptor_get,
    .config_descriptor_callback = config_descriptor_get,
    .string_descriptor_callback = string_descriptor_get,
};

USB_MEM_ALIGNX static uint8_t loopback_buffer[LOOPBACK_BUFFER_SIZE];
volatile uint32_t g_usb_loopback_packets;
volatile uint32_t g_usb_loopback_errors;

static void loopback_in(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)ep;
    (void)nbytes;
    ++g_usb_loopback_packets;
    if (usbd_ep_start_read(busid, LOOPBACK_OUT_EP, loopback_buffer, sizeof(loopback_buffer)) != 0) {
        ++g_usb_loopback_errors;
    }
}

static void loopback_out(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)ep;
    if (usbd_ep_start_write(busid, LOOPBACK_IN_EP, loopback_buffer, nbytes) != 0) {
        ++g_usb_loopback_errors;
    }
}

static struct usbd_endpoint out_endpoint = {
    .ep_addr = LOOPBACK_OUT_EP,
    .ep_cb = loopback_out,
};

static struct usbd_endpoint in_endpoint = {
    .ep_addr = LOOPBACK_IN_EP,
    .ep_cb = loopback_in,
};

static struct usbd_interface vendor_interface;

static void usb_event(uint8_t busid, uint8_t event) {
    if (event == USBD_EVENT_CONFIGURED) {
        if (usbd_ep_start_read(busid, LOOPBACK_OUT_EP, loopback_buffer, sizeof(loopback_buffer)) != 0) {
            ++g_usb_loopback_errors;
        }
    }
}

int usb_vendor_bulk_init(void) {
    usbd_desc_register(LOOPBACK_BUS_ID, &descriptors);
    usbd_add_interface(LOOPBACK_BUS_ID, &vendor_interface);
    usbd_add_endpoint(LOOPBACK_BUS_ID, &out_endpoint);
    usbd_add_endpoint(LOOPBACK_BUS_ID, &in_endpoint);
    return usbd_initialize(LOOPBACK_BUS_ID, bsp_usb_device_base(), usb_event);
}
