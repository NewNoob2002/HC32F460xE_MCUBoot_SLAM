#ifndef FW_UPDATE_MANAGER_H
#define FW_UPDATE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include "fw_update/boot_control.h"
#include "fw_update/product_config.h"
#include "fw_update/protocol.h"
#include "fw_update/storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Numeric firmware version encoded by Protocol V1. */
struct fw_update_version {
    uint8_t major;
    uint8_t minor;
    uint16_t revision;
    uint32_t build;
};

/** Static Manager configuration owned by the Application. Referenced backend instances must outlive the Manager. */
struct fw_update_manager_config {
    const struct fw_update_storage* storage;
    const struct fw_update_boot_control* boot_control;
    const struct fw_update_product_config* product_config;
    uint8_t product_config_writable;
    uint16_t usb_boot_pid;
    uint16_t usb_application_pid_min;
    uint16_t usb_application_pid_max;
    struct fw_update_version application_version;
    struct fw_update_version bootloader_version;
    uint32_t session_timeout_ms;
};

/** Observable update-session states. */
enum fw_update_manager_state {
    FW_UPDATE_MANAGER_STATE_IDLE = 0,
    FW_UPDATE_MANAGER_STATE_NEGOTIATING,
    FW_UPDATE_MANAGER_STATE_PREPARING,
    FW_UPDATE_MANAGER_STATE_RECEIVING,
    FW_UPDATE_MANAGER_STATE_VERIFYING,
    FW_UPDATE_MANAGER_STATE_READY_TO_COMMIT,
    FW_UPDATE_MANAGER_STATE_COMMITTING,
    FW_UPDATE_MANAGER_STATE_COMPLETED,
    FW_UPDATE_MANAGER_STATE_ABORTED,
    FW_UPDATE_MANAGER_STATE_ERROR,
};

/** Bounded lifecycle actions emitted for Application orchestration. */
enum fw_update_manager_action {
    FW_UPDATE_MANAGER_ACTION_NONE = 0,
    FW_UPDATE_MANAGER_ACTION_RESET,
};

/** Results returned by Manager ingress/egress operations. */
enum fw_update_manager_result {
    FW_UPDATE_MANAGER_OK = 0,
    FW_UPDATE_MANAGER_NEED_MORE = 1,
    FW_UPDATE_MANAGER_RESPONSE_READY = 2,
    FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT = -1,
    FW_UPDATE_MANAGER_ERR_BUSY = -2,
    FW_UPDATE_MANAGER_ERR_PROTOCOL = -3,
    FW_UPDATE_MANAGER_ERR_STORAGE = -4,
    FW_UPDATE_MANAGER_ERR_INTERNAL = -5,
};

/** Caller-allocated Manager state. Treat members as private. */
struct fw_update_manager {
    struct fw_update_manager_config config;
    struct fw_update_storage_info storage_info;
    struct fw_update_product_config_state product_config_state;
    struct fw_protocol_parser parser;
    uint8_t tx_buffer[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t work_buffer[FW_PROTOCOL_MAX_PAYLOAD];
    uint8_t cached_response[FW_PROTOCOL_HEADER_SIZE + FW_UPDATE_PRODUCT_CONFIG_MAX_WIRE_SIZE + FW_PROTOCOL_CRC_SIZE];
    size_t tx_size;
    size_t tx_offset;
    size_t cached_request_size;
    size_t cached_response_size;
    size_t pending_request_size;
    size_t staging_length;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t received_size;
    uint32_t write_offset;
    uint32_t expected_sequence;
    uint32_t last_activity_ms;
    struct fw_update_version candidate_version;
    enum fw_update_manager_state state;
    enum fw_protocol_status last_status;
    enum fw_update_manager_action pending_action;
    uint8_t protocol_error_count;
    uint8_t session_active;
    uint8_t tx_uses_parser_buffer;
    uint8_t cache_request_after_tx;
    uint8_t return_to_idle_after_tx;
    uint8_t clear_cache_after_tx;
    uint8_t cached_commit_success;
    uint8_t tx_is_successful_commit;
    uint8_t commit_succeeded;
    uint8_t commit_response_consumed;
    uint8_t tx_idle_seen;
    uint8_t reset_action_taken;
};

/** Initialize one Manager instance and cache validated Storage geometry. */
enum fw_update_manager_result fw_update_manager_init(struct fw_update_manager* manager,
                                                     const struct fw_update_manager_config* config);

/**
 * Feed borrowed transport-neutral bytes. Calls must be serialized in
 * Application context; BEGIN/DATA/END may synchronously erase, write or read
 * Storage. No input is consumed while TX is pending.
 */
enum fw_update_manager_result fw_update_manager_feed(struct fw_update_manager* manager, const uint8_t* input,
                                                     size_t input_size, uint32_t now_ms, size_t* consumed);

/** Return the current Manager-owned TX range, valid until consume/feed/init. */
enum fw_update_manager_result fw_update_manager_tx_view(const struct fw_update_manager* manager, const uint8_t** data,
                                                        size_t* size);

/** Report bytes consumed from the current Manager-owned TX range. */
enum fw_update_manager_result fw_update_manager_consume_tx(struct fw_update_manager* manager, size_t size);

/** Apply inactivity timeout processing using a wrapping monotonic millisecond tick. */
enum fw_update_manager_result fw_update_manager_poll(struct fw_update_manager* manager, uint32_t now_ms);

/** Discard transport/session state without erasing candidate bytes or undoing a successful COMMIT. */
enum fw_update_manager_result fw_update_manager_notify_disconnect(struct fw_update_manager* manager);

/** Report that the Transport has physically completed the currently queued transmission. */
enum fw_update_manager_result fw_update_manager_notify_tx_idle(struct fw_update_manager* manager);

/** Take one pending lifecycle action. RESET is emitted at most once per successful COMMIT. */
enum fw_update_manager_action fw_update_manager_take_action(struct fw_update_manager* manager);

/** Return the current session state. */
enum fw_update_manager_state fw_update_manager_get_state(const struct fw_update_manager* manager);

/** Return the last stable wire status produced or observed by the Manager. */
enum fw_protocol_status fw_update_manager_get_last_status(const struct fw_update_manager* manager);

#ifdef __cplusplus
}
#endif

#endif
