#include "usb_fw_update.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bsp_critical.h"
#include "fw_update/boot_control_mcuboot.h"
#include "fw_update/manager.h"
#include "fw_update/product_config_flashdb.h"
#include "fw_update/storage_mcuboot.h"
#include "hc32f460.h"
#include "product_identity.h"
#include "usbd_core.h"

#define UPDATE_BUS_ID         0U
#define UPDATE_IN_EP          0x81U
#define UPDATE_OUT_EP         0x02U
#define UPDATE_MPS            64U
#define UPDATE_RX_CAPACITY    1024U
#define WINUSB_VENDOR_CODE    UINT8_C(0x20)
#define USB_SERIAL_UID_WORDS  3U
#define USB_SERIAL_HEX_LENGTH (USB_SERIAL_UID_WORDS * 8U)
#define MSOS_DESCRIPTOR_SET_TOTAL_LENGTH                                                                               \
    (WINUSB_DESCRIPTOR_SET_HEADER_SIZE + USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_SINGLE_DESCRIPTOR_LEN)
#define BOS_DESCRIPTOR_TOTAL_LENGTH (5U + USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_LEN)

#ifndef USB_FW_UPDATE_BOOT_RECOVERY
#define USB_FW_UPDATE_BOOT_RECOVERY 0
#endif

#if USB_FW_UPDATE_BOOT_RECOVERY
#define UPDATE_USB_PID     HC32_PRODUCT_USB_BOOT_PID
#define UPDATE_USB_PRODUCT HC32_PRODUCT_USB_BOOT_PRODUCT
#else
#define UPDATE_USB_PID     HC32_PRODUCT_USB_APPLICATION_PID
#define UPDATE_USB_PRODUCT HC32_PRODUCT_USB_APPLICATION_PRODUCT
#endif

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_1, 0x00, 0x00, 0x00, HC32_PRODUCT_USB_VID, UPDATE_USB_PID, 0x0001, 0x01)};

static const uint8_t config_descriptor[] = {
    USB_CONFIG_DESCRIPTOR_INIT(9 + 9 + 7 + 7, 1, 1, USB_CONFIG_BUS_POWERED, 100),
    USB_INTERFACE_DESCRIPTOR_INIT(0, 0, 2, 0xFF, 0x00, 0x00, 4),
    USB_ENDPOINT_DESCRIPTOR_INIT(UPDATE_IN_EP, USB_ENDPOINT_TYPE_BULK, UPDATE_MPS, 0),
    USB_ENDPOINT_DESCRIPTOR_INIT(UPDATE_OUT_EP, USB_ENDPOINT_TYPE_BULK, UPDATE_MPS, 0),
};

static const char* string_descriptors[] = {
    (const char[]){0x09, 0x04}, HC32_PRODUCT_USB_MANUFACTURER, UPDATE_USB_PRODUCT, NULL, "Vendor Bulk Firmware Update",
};
static char usb_serial[sizeof(HC32_PRODUCT_USB_SERIAL_PREFIX) + USB_SERIAL_HEX_LENGTH];

static const uint8_t msosv2_descriptor_set[] = {
    USB_MSOSV2_COMP_ID_SET_HEADER_DESCRIPTOR_INIT(MSOS_DESCRIPTOR_SET_TOTAL_LENGTH),
    USB_MSOSV2_COMP_ID_FUNCTION_WINUSB_SINGLE_DESCRIPTOR_INIT(),
};

static const uint8_t bos_descriptor_bytes[] = {
    USB_BOS_HEADER_DESCRIPTOR_INIT(BOS_DESCRIPTOR_TOTAL_LENGTH, 1),
    USB_BOS_CAP_PLATFORM_WINUSB_DESCRIPTOR_INIT(WINUSB_VENDOR_CODE, MSOS_DESCRIPTOR_SET_TOTAL_LENGTH),
};

_Static_assert(sizeof(msosv2_descriptor_set) == MSOS_DESCRIPTOR_SET_TOTAL_LENGTH,
               "Microsoft OS 2.0 descriptor length mismatch");
_Static_assert(sizeof(bos_descriptor_bytes) == BOS_DESCRIPTOR_TOTAL_LENGTH, "BOS descriptor length mismatch");
_Static_assert((sizeof(usb_serial) * 2U) <= CONFIG_USBDEV_REQUEST_BUFFER_LEN,
               "USB serial exceeds CherryUSB request buffer");

static const struct usb_msosv2_descriptor msosv2_descriptor = {
    .compat_id = msosv2_descriptor_set,
    .compat_id_len = (uint16_t)sizeof(msosv2_descriptor_set),
    .vendor_code = WINUSB_VENDOR_CODE,
};

static const struct usb_bos_descriptor bos_descriptor = {
    .string = bos_descriptor_bytes,
    .string_len = (uint32_t)sizeof(bos_descriptor_bytes),
};

static int initialize_usb_serial(void) {
    static const char hex[] = "0123456789ABCDEF";
    const uint32_t unique_id[USB_SERIAL_UID_WORDS] = {CM_EFM->UQID0, CM_EFM->UQID1, CM_EFM->UQID2};
    const uint32_t all_bits = unique_id[0] & unique_id[1] & unique_id[2];
    const uint32_t any_bits = unique_id[0] | unique_id[1] | unique_id[2];
    if (any_bits == 0U || all_bits == UINT32_MAX)
        return -1;

    const size_t prefix_length = sizeof(HC32_PRODUCT_USB_SERIAL_PREFIX) - 1U;
    memcpy(usb_serial, HC32_PRODUCT_USB_SERIAL_PREFIX, prefix_length);
    size_t output = prefix_length;
    for (size_t word = 0U; word < USB_SERIAL_UID_WORDS; ++word) {
        for (uint32_t nibble = 0U; nibble < 8U; ++nibble) {
            const uint32_t shift = 28U - (nibble * 4U);
            const size_t digit = (size_t)((unique_id[word] >> shift) & UINT32_C(0x0F));
            usb_serial[output++] = hex[digit];
        }
    }
    usb_serial[output] = '\0';
    string_descriptors[3] = usb_serial;
    return 0;
}

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
    .msosv2_descriptor = &msosv2_descriptor,
    .bos_descriptor = &bos_descriptor,
};

USB_MEM_ALIGNX static uint8_t rx_buffer[UPDATE_RX_CAPACITY];
USB_MEM_ALIGNX static uint8_t tx_buffer[FW_PROTOCOL_MAX_FRAME_SIZE];

static struct fw_update_storage storage;
static struct fw_update_boot_control boot_control;
static struct fw_update_product_config product_config;
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
    const struct fw_update_product_identity default_identity = {
        .hardware_id = HC32_PRODUCT_HARDWARE_ID,
        .board_id = HC32_PRODUCT_BOARD_ID,
        .board_revision = HC32_PRODUCT_BOARD_REVISION,
    };
    if (initialize_usb_serial() != 0)
        return -1;
    fw_update_storage_mcuboot_init(&storage);
    if (fw_update_product_config_flashdb_init(&product_config, &default_identity) != FW_UPDATE_OK)
        return -1;
    fw_update_boot_control_mcuboot_init(&boot_control, &product_config);
    const struct fw_update_manager_config config = {
        .storage = &storage,
        .boot_control = &boot_control,
        .product_config = &product_config,
        .product_config_writable = USB_FW_UPDATE_BOOT_RECOVERY != 0 ? 1U : 0U,
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
