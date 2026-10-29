#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bsp_status_led.h"
#include "bsp_usb.h"
#include "fw_update/boot_control_mcuboot.h"
#include "fw_update/product_config_flashdb.h"
#include "fw_update/protocol.h"
#include "fw_update/storage_mcuboot.h"
#include "usb_fw_update.h"
#include "usbd_core.h"

#define FAKE_CAPACITY 4096U

/* Fake CherryUSB device stack: records armed transfers and exposes the registered callbacks. */
static usbd_event_handler_t event_handler;
static usbd_endpoint_callback out_callback;
static usbd_endpoint_callback in_callback;
static uint8_t* armed_buffer;
static uint32_t armed_capacity;
static unsigned int read_calls;
static unsigned int write_calls;
static uint8_t written[FW_PROTOCOL_MAX_FRAME_SIZE];
static uint32_t written_length;

void usbd_desc_register(uint8_t busid, const struct usb_descriptor* desc) {
    (void)busid;
    (void)desc;
}

void usbd_add_interface(uint8_t busid, struct usbd_interface* intf) {
    (void)busid;
    (void)intf;
}

void usbd_add_endpoint(uint8_t busid, struct usbd_endpoint* ep) {
    (void)busid;
    if (USB_EP_DIR_IS_OUT(ep->ep_addr))
        out_callback = ep->ep_cb;
    else
        in_callback = ep->ep_cb;
}

int usbd_initialize(uint8_t busid, uintptr_t reg_base, usbd_event_handler_t handler) {
    (void)busid;
    (void)reg_base;
    event_handler = handler;
    return 0;
}

int usbd_ep_start_read(uint8_t busid, const uint8_t ep, uint8_t* data, uint32_t data_len) {
    (void)busid;
    (void)ep;
    armed_buffer = data;
    armed_capacity = data_len;
    ++read_calls;
    return 0;
}

int usbd_ep_start_write(uint8_t busid, const uint8_t ep, const uint8_t* data, uint32_t data_len) {
    (void)busid;
    (void)ep;
    assert(data_len <= sizeof(written));
    memcpy(written, data, data_len);
    written_length = data_len;
    ++write_calls;
    return 0;
}

void bsp_status_led_set_mode(enum bsp_status_led_mode mode) {
    (void)mode;
}

void bsp_status_led_poll(uint32_t now_ms) {
    (void)now_ms;
}

uint32_t bsp_usb_device_base(void) {
    return 0U;
}

bool bsp_usb_serial_id(uint32_t words[BSP_USB_SERIAL_ID_WORDS]) {
    for (uint32_t i = 0U; i < BSP_USB_SERIAL_ID_WORDS; ++i)
        words[i] = 0x01020304U + i;
    return true;
}

/* In-memory replacements for the MCUboot and FlashDB backends. */
static uint8_t storage_bytes[FAKE_CAPACITY];
static struct fw_update_product_config_state product_state;

static enum fw_update_result fake_get_info(void* context, struct fw_update_storage_info* info) {
    (void)context;
    info->capacity = FAKE_CAPACITY;
    info->write_alignment = 4U;
    info->erase_alignment = 256U;
    info->erased_value = 0xFFU;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_erase_all(void* context) {
    (void)context;
    memset(storage_bytes, 0xFF, sizeof(storage_bytes));
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_write(void* context, uint32_t offset, const void* data, uint32_t length) {
    (void)context;
    if (offset > FAKE_CAPACITY || length > FAKE_CAPACITY - offset)
        return FW_UPDATE_ERR_BOUNDS;
    memcpy(&storage_bytes[offset], data, length);
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_read(void* context, uint32_t offset, void* data, uint32_t length) {
    (void)context;
    if (offset > FAKE_CAPACITY || length > FAKE_CAPACITY - offset)
        return FW_UPDATE_ERR_BOUNDS;
    memcpy(data, &storage_bytes[offset], length);
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_boot_ok(void* context) {
    (void)context;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_product_get(void* context, struct fw_update_product_config_state* state) {
    (void)context;
    *state = product_state;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_product_set(void* context, const struct fw_update_product_identity* identity) {
    (void)context;
    (void)identity;
    return FW_UPDATE_ERR_LOCKED;
}

static const struct fw_update_storage_ops storage_ops = {
    .get_info = fake_get_info,
    .erase_all = fake_erase_all,
    .write = fake_write,
    .read = fake_read,
};

static const struct fw_update_boot_control_ops boot_control_ops = {
    .request_test_upgrade = fake_boot_ok,
    .confirm_running_image = fake_boot_ok,
};

static const struct fw_update_product_config_ops product_config_ops = {
    .get = fake_product_get,
    .set = fake_product_set,
};

void fw_update_storage_mcuboot_init(struct fw_update_storage* storage) {
    storage->ops = &storage_ops;
    storage->context = NULL;
}

void fw_update_boot_control_mcuboot_init(struct fw_update_boot_control* control,
                                         const struct fw_update_product_config* product_config) {
    (void)product_config;
    control->ops = &boot_control_ops;
    control->context = NULL;
}

enum fw_update_result fw_update_product_config_flashdb_init(struct fw_update_product_config* config,
                                                            const struct fw_update_product_identity* default_identity) {
    memset(&product_state, 0, sizeof(product_state));
    product_state.identity = *default_identity;
    config->ops = &product_config_ops;
    config->context = NULL;
    return FW_UPDATE_OK;
}

/* Test helpers. */
static uint32_t now_ms = 100U;

static size_t encode_request(uint8_t command, uint32_t sequence, uint8_t* output) {
    const struct fw_protocol_frame frame = {
        .major = FW_PROTOCOL_VERSION_MAJOR,
        .minor = FW_PROTOCOL_VERSION_MINOR,
        .command = command,
        .sequence = sequence,
    };
    size_t size = 0U;
    assert(fw_protocol_encode(&frame, output, FW_PROTOCOL_MAX_FRAME_SIZE, &size) == FW_PROTOCOL_OK);
    return size;
}

static void poll(void) {
    assert(usb_fw_update_poll(now_ms) == USB_FW_UPDATE_ACTION_NONE);
    now_ms += 10U;
}

static void host_out(const uint8_t* data, size_t size) {
    assert(armed_buffer != NULL && size <= armed_capacity);
    memcpy(armed_buffer, data, size);
    armed_buffer = NULL;
    out_callback(0U, 0x02U, (uint32_t)size);
    poll();
}

static void send_request(uint8_t command, uint32_t sequence) {
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    host_out(request, encode_request(command, sequence, request));
}

static void expect_response(unsigned int writes, uint8_t command, uint32_t sequence, uint16_t status) {
    struct fw_protocol_frame frame;
    assert(write_calls == writes);
    assert(fw_protocol_decode(written, written_length, &frame) == FW_PROTOCOL_OK);
    assert(frame.command == command && frame.sequence == sequence && frame.payload_length >= 2U);
    assert((uint16_t)(frame.payload[0] | (frame.payload[1] << 8U)) == status);
}

static void complete_in(void) {
    in_callback(0U, 0x81U, written_length);
    poll();
}

static void configure(void) {
    event_handler(0U, USBD_EVENT_CONFIGURED);
    poll();
}

int main(void) {
    uint8_t frames[2U * FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t size;

    assert(usb_fw_update_init() == 0);
    assert(event_handler != NULL && out_callback != NULL && in_callback != NULL);
    configure();
    assert(read_calls == 1U && armed_buffer != NULL);

    /* Bus reset while a response is queued must not leave OUT disarmed (H1). */
    send_request(FW_PROTOCOL_COMMAND_HELLO, 0U);
    expect_response(1U, FW_PROTOCOL_COMMAND_HELLO, 0U, FW_PROTOCOL_STATUS_OK);
    assert(armed_buffer == NULL);
    event_handler(0U, USBD_EVENT_RESET);
    poll();
    assert(armed_buffer == NULL);
    configure();
    assert(armed_buffer != NULL);
    send_request(FW_PROTOCOL_COMMAND_HELLO, 0U);
    expect_response(2U, FW_PROTOCOL_COMMAND_HELLO, 0U, FW_PROTOCOL_STATUS_OK);
    complete_in();
    assert(armed_buffer != NULL);

    /* A corrupted frame is dropped without ending the session (M2). */
    size = encode_request(FW_PROTOCOL_COMMAND_DEVICE_INFO, 1U, frames);
    frames[size - 1U] ^= 0xFFU;
    host_out(frames, size);
    assert(write_calls == 2U && armed_buffer != NULL);
    send_request(FW_PROTOCOL_COMMAND_DEVICE_INFO, 1U);
    expect_response(3U, FW_PROTOCOL_COMMAND_DEVICE_INFO, 1U, FW_PROTOCOL_STATUS_OK);
    complete_in();

    /* Bytes following a complete frame are retained and answered after the first response drains. */
    size = encode_request(FW_PROTOCOL_COMMAND_DEVICE_INFO, 2U, frames);
    size += encode_request(FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_GET, 3U, &frames[size]);
    host_out(frames, size);
    expect_response(4U, FW_PROTOCOL_COMMAND_DEVICE_INFO, 2U, FW_PROTOCOL_STATUS_OK);
    assert(armed_buffer == NULL);
    complete_in();
    expect_response(5U, FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_GET, 3U, FW_PROTOCOL_STATUS_OK);
    assert(armed_buffer == NULL);
    complete_in();
    assert(armed_buffer != NULL);
    return 0;
}
