#include "usb_fw_update.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp_critical.h"
#include "fw_update/boot_control_mcuboot.h"
#include "fw_update/manager.h"
#include "fw_update/storage_mcuboot.h"
#include "hc32f460.h"
#include "usbd_core.h"

#define UPDATE_BUS_ID      0U
#define UPDATE_IN_EP       0x81U
#define UPDATE_OUT_EP      0x02U
#define UPDATE_MPS         64U
#define UPDATE_RX_CAPACITY 1024U

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0xFFFE, 0xFFFF, 0x0001, 0x01)};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(9 + 9 + 7 + 7, 1, 1, USB_CONFIG_BUS_POWERED, 100),
    USB_INTERFACE_DESCRIPTOR_INIT(0, 0, 2, 0xFF, 0x00, 0x00, 4),
    USB_ENDPOINT_DESCRIPTOR_INIT(UPDATE_IN_EP, USB_ENDPOINT_TYPE_BULK, UPDATE_MPS, 0),
    USB_ENDPOINT_DESCRIPTOR_INIT(UPDATE_OUT_EP, USB_ENDPOINT_TYPE_BULK, UPDATE_MPS, 0),
};

static const char* string_descriptors[] = {
    (const char[]){0x09, 0x04},    "HC32 Phase 5 Lab", "HC32 Firmware Updater", "HC32F460-PHASE5-0001",
    "Vendor Bulk Firmware Update",
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
    if (index >= (sizeof(string_descriptors) / sizeof(string_descriptors[0])))
        return NULL;
    return string_descriptors[index];
}

static const struct usb_descriptor descriptors = {
    .device_descriptor_callback = device_descriptor_get,
    .config_descriptor_callback = config_descriptor_get,
    .string_descriptor_callback = string_descriptor_get,
};

USB_MEM_ALIGNX static uint8_t rx_buffer[UPDATE_RX_CAPACITY];
USB_MEM_ALIGNX static uint8_t tx_buffer[FW_PROTOCOL_MAX_FRAME_SIZE];

static struct fw_update_storage storage;
static struct fw_update_boot_control boot_control;
static struct fw_update_manager manager;
static volatile uint32_t rx_length;
static volatile uint32_t tx_complete_length;
static volatile uint8_t rx_ready;
static volatile uint8_t tx_complete;
static volatile uint8_t configure_pending;
static volatile uint8_t is_configured;
static volatile uint8_t disconnected;
static uint8_t tx_active;

volatile uint32_t g_usb_fw_update_errors;
volatile int g_usb_fw_update_last_result;

static void update_out(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    rx_length = nbytes;
    rx_ready = 1U;
}

static void update_in(uint8_t busid, uint8_t ep, uint32_t nbytes) {
    (void)busid;
    (void)ep;
    tx_complete_length = nbytes;
    tx_complete = 1U;
}

static struct usbd_endpoint out_endpoint = {
    .ep_addr = UPDATE_OUT_EP,
    .ep_cb = update_out,
};

static struct usbd_endpoint in_endpoint = {
    .ep_addr = UPDATE_IN_EP,
    .ep_cb = update_in,
};

static struct usbd_interface vendor_interface;

static void usb_event(uint8_t busid, uint8_t event) {
    (void)busid;
    if (event == USBD_EVENT_CONFIGURED) {
        is_configured = 1U;
        configure_pending = 1U;
    } else if (event == USBD_EVENT_DISCONNECTED) {
        is_configured = 0U;
        disconnected = 1U;
    }
}

static int arm_receive(void) {
    return usbd_ep_start_read(UPDATE_BUS_ID, UPDATE_OUT_EP, rx_buffer, sizeof(rx_buffer));
}

static int start_manager_tx(void) {
    const uint8_t* data = NULL;
    size_t size = 0U;
    enum fw_update_manager_result result = fw_update_manager_tx_view(&manager, &data, &size);
    if (result != FW_UPDATE_MANAGER_OK || data == NULL || size == 0U || size > sizeof(tx_buffer))
        return -1;
    memcpy(tx_buffer, data, size);
    if (usbd_ep_start_write(UPDATE_BUS_ID, UPDATE_IN_EP, tx_buffer, (uint32_t)size) != 0)
        return -1;
    tx_active = 1U;
    return 0;
}

static void recover_transport(void) {
    ++g_usb_fw_update_errors;
    g_usb_fw_update_last_result = fw_update_manager_notify_disconnect(&manager);
    tx_active = 0U;
    if (is_configured != 0U && arm_receive() != 0)
        ++g_usb_fw_update_errors;
}

int usb_fw_update_init(void) {
    fw_update_storage_mcuboot_init(&storage);
    fw_update_boot_control_mcuboot_init(&boot_control);
    const struct fw_update_manager_config config = {
        .storage = &storage,
        .boot_control = &boot_control,
        .hardware_id = UINT32_C(0x00004600),
        .board_id = 1U,
        .board_revision = 2U,
        .application_version =
            {
                .major = APP_VERSION_MAJOR,
                .minor = APP_VERSION_MINOR,
                .revision = APP_VERSION_REVISION,
                .build = APP_VERSION_BUILD,
            },
        .bootloader_version = {.major = 1U, .minor = 0U, .revision = 0U, .build = 0U},
        .session_timeout_ms = 5000U,
    };
    int result = fw_update_manager_init(&manager, &config);
    if (result != FW_UPDATE_MANAGER_OK)
        return result;

    usbd_desc_register(UPDATE_BUS_ID, &descriptors);
    usbd_add_interface(UPDATE_BUS_ID, &vendor_interface);
    usbd_add_endpoint(UPDATE_BUS_ID, &out_endpoint);
    usbd_add_endpoint(UPDATE_BUS_ID, &in_endpoint);
    return usbd_initialize(UPDATE_BUS_ID, CM_USBFS_BASE, usb_event);
}

enum usb_fw_update_action usb_fw_update_poll(uint32_t now_ms) {
    uint32_t received = 0U;
    uint32_t sent = 0U;
    uint8_t have_rx = 0U;
    uint8_t have_tx_complete = 0U;
    uint8_t was_configured = 0U;
    uint8_t was_disconnected = 0U;
    bsp_irq_state_t irq_state = bsp_enter_critical();
    if (configure_pending != 0U) {
        configure_pending = 0U;
        was_configured = 1U;
    }
    if (disconnected != 0U) {
        disconnected = 0U;
        was_disconnected = 1U;
    }
    if (rx_ready != 0U) {
        received = rx_length;
        rx_ready = 0U;
        have_rx = 1U;
    }
    if (tx_complete != 0U) {
        sent = tx_complete_length;
        tx_complete = 0U;
        have_tx_complete = 1U;
    }
    bsp_exit_critical(irq_state);

    if (was_disconnected != 0U) {
        g_usb_fw_update_last_result = fw_update_manager_notify_disconnect(&manager);
        tx_active = 0U;
        have_rx = 0U;
        have_tx_complete = 0U;
    }
    if (was_configured != 0U && tx_active == 0U && arm_receive() != 0)
        recover_transport();

    if (have_tx_complete != 0U) {
        tx_active = 0U;
        g_usb_fw_update_last_result = fw_update_manager_consume_tx(&manager, sent);
        if (g_usb_fw_update_last_result != FW_UPDATE_MANAGER_OK) {
            recover_transport();
        } else {
            const uint8_t* remaining = NULL;
            size_t remaining_size = 0U;
            g_usb_fw_update_last_result = fw_update_manager_tx_view(&manager, &remaining, &remaining_size);
            if (g_usb_fw_update_last_result != FW_UPDATE_MANAGER_OK)
                recover_transport();
            else if (remaining_size != 0U) {
                if (start_manager_tx() != 0)
                    recover_transport();
            } else {
                g_usb_fw_update_last_result = fw_update_manager_notify_tx_idle(&manager);
                if (g_usb_fw_update_last_result != FW_UPDATE_MANAGER_OK || arm_receive() != 0)
                    recover_transport();
            }
        }
    }

    if (have_rx != 0U) {
        if (received == 0U) {
            if (arm_receive() != 0)
                recover_transport();
        } else if (received > sizeof(rx_buffer) || tx_active != 0U) {
            recover_transport();
        } else {
            size_t consumed = 0U;
            g_usb_fw_update_last_result = fw_update_manager_feed(&manager, rx_buffer, received, now_ms, &consumed);
            if (g_usb_fw_update_last_result == FW_UPDATE_MANAGER_RESPONSE_READY && consumed == received) {
                if (start_manager_tx() != 0)
                    recover_transport();
            } else if (g_usb_fw_update_last_result == FW_UPDATE_MANAGER_NEED_MORE && consumed == received) {
                if (arm_receive() != 0)
                    recover_transport();
            } else {
                recover_transport();
            }
        }
    }

    g_usb_fw_update_last_result = fw_update_manager_poll(&manager, now_ms);
    return fw_update_manager_take_action(&manager) == FW_UPDATE_MANAGER_ACTION_RESET ? USB_FW_UPDATE_ACTION_RESET
                                                                                     : USB_FW_UPDATE_ACTION_NONE;
}
