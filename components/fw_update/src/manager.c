#include "fw_update/manager.h"

#include <limits.h>
#include <string.h>

#define FW_UPDATE_MANAGER_MAX_RESPONSE_PAYLOAD 40U
#define FW_UPDATE_MANAGER_MAX_PROTOCOL_ERRORS  3U
#define FW_UPDATE_MANAGER_CAPABILITIES                                                                                 \
    ((uint32_t)(FW_PROTOCOL_CAPABILITY_TEST_UPGRADE | FW_PROTOCOL_CAPABILITY_READBACK_CRC                              \
                | FW_PROTOCOL_CAPABILITY_STRICT_DATA))

_Static_assert(sizeof(struct fw_update_manager) <= 2048U, "Manager exceeds the Phase 3 memory limit");

static uint16_t read_u16_le(const uint8_t* input) {
    return (uint16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8U));
}

static uint32_t read_u32_le(const uint8_t* input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) | ((uint32_t)input[2] << 16U) | ((uint32_t)input[3] << 24U);
}

static void write_u16_le(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void write_version(uint8_t* output, const struct fw_update_version* version) {
    output[0] = version->major;
    output[1] = version->minor;
    write_u16_le(&output[2], version->revision);
    write_u32_le(&output[4], version->build);
}

static struct fw_update_version read_version(const uint8_t* input) {
    struct fw_update_version version = {
        .major = input[0],
        .minor = input[1],
        .revision = read_u16_le(&input[2]),
        .build = read_u32_le(&input[4]),
    };
    return version;
}

static void clear_session(struct fw_update_manager* manager) {
    manager->staging_length = 0U;
    manager->image_size = 0U;
    manager->image_crc32 = 0U;
    manager->received_size = 0U;
    manager->write_offset = 0U;
    memset(&manager->candidate_version, 0, sizeof(manager->candidate_version));
}

static void clear_cached_exchange(struct fw_update_manager* manager) {
    manager->cached_request_size = 0U;
    manager->cached_response_size = 0U;
    manager->cached_commit_success = 0U;
}

static void clear_tx(struct fw_update_manager* manager) {
    manager->tx_size = 0U;
    manager->tx_offset = 0U;
    manager->pending_request_size = 0U;
    manager->tx_uses_parser_buffer = 0U;
    manager->cache_request_after_tx = 0U;
    manager->tx_is_successful_commit = 0U;
}

static void clear_commit_lifecycle(struct fw_update_manager* manager) {
    manager->pending_action = FW_UPDATE_MANAGER_ACTION_NONE;
    manager->commit_succeeded = 0U;
    manager->commit_response_consumed = 0U;
    manager->tx_idle_seen = 0U;
    manager->reset_action_taken = 0U;
}

static void reset_to_idle(struct fw_update_manager* manager, int clear_cache) {
    clear_session(manager);
    clear_tx(manager);
    if (clear_cache != 0)
        clear_cached_exchange(manager);
    clear_commit_lifecycle(manager);
    fw_protocol_parser_reset(&manager->parser);
    manager->protocol_error_count = 0U;
    manager->session_active = 0U;
    manager->return_to_idle_after_tx = 0U;
    manager->clear_cache_after_tx = 0U;
    manager->state = FW_UPDATE_MANAGER_STATE_IDLE;
}

static int timeout_is_active(const struct fw_update_manager* manager) {
    return manager->session_active != 0U && manager->state != FW_UPDATE_MANAGER_STATE_COMPLETED
           && manager->state != FW_UPDATE_MANAGER_STATE_ABORTED;
}

static void schedule_reset_if_ready(struct fw_update_manager* manager) {
    if (manager->commit_succeeded != 0U && manager->commit_response_consumed != 0U && manager->tx_idle_seen != 0U
        && manager->reset_action_taken == 0U)
        manager->pending_action = FW_UPDATE_MANAGER_ACTION_RESET;
}

static void finish_tx(struct fw_update_manager* manager) {
    if (manager->cache_request_after_tx != 0U) {
        memcpy(manager->tx_buffer, manager->parser.buffer, manager->pending_request_size);
        manager->cached_request_size = manager->pending_request_size;
    }

    if (manager->tx_is_successful_commit != 0U) {
        manager->commit_response_consumed = 1U;
        schedule_reset_if_ready(manager);
    }

    manager->tx_size = 0U;
    manager->tx_offset = 0U;
    manager->pending_request_size = 0U;
    manager->tx_uses_parser_buffer = 0U;
    manager->cache_request_after_tx = 0U;
    manager->tx_is_successful_commit = 0U;
    fw_protocol_parser_reset(&manager->parser);

    if (manager->return_to_idle_after_tx != 0U) {
        clear_session(manager);
        manager->session_active = 0U;
        manager->protocol_error_count = 0U;
        manager->state = FW_UPDATE_MANAGER_STATE_IDLE;
        manager->return_to_idle_after_tx = 0U;
        if (manager->clear_cache_after_tx != 0U)
            clear_cached_exchange(manager);
        manager->clear_cache_after_tx = 0U;
    }
}

static int state_allows_abort(enum fw_update_manager_state state) {
    return state == FW_UPDATE_MANAGER_STATE_NEGOTIATING || state == FW_UPDATE_MANAGER_STATE_RECEIVING
           || state == FW_UPDATE_MANAGER_STATE_READY_TO_COMMIT || state == FW_UPDATE_MANAGER_STATE_ERROR;
}

static enum fw_protocol_status storage_failed(struct fw_update_manager* manager) {
    manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
    return FW_PROTOCOL_STATUS_STORAGE_ERROR;
}

static enum fw_protocol_status validate_begin(struct fw_update_manager* manager,
                                              const struct fw_protocol_frame* frame) {
    uint32_t image_size;
    uint32_t padding = 0U;
    uint32_t alignment = manager->storage_info.write_alignment;

    if (frame->payload_length != 28U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    image_size = read_u32_le(&frame->payload[0]);
    if (read_u16_le(&frame->payload[18]) != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (image_size == 0U)
        return FW_PROTOCOL_STATUS_INVALID_ARGUMENT;
    if (read_u32_le(&frame->payload[8]) != manager->config.hardware_id
        || read_u32_le(&frame->payload[12]) != manager->config.board_id
        || read_u16_le(&frame->payload[16]) != manager->config.board_revision)
        return FW_PROTOCOL_STATUS_INVALID_ARGUMENT;
    if (alignment > FW_PROTOCOL_MAX_PAYLOAD)
        return FW_PROTOCOL_STATUS_INVALID_ARGUMENT;

    if ((image_size % alignment) != 0U)
        padding = alignment - (image_size % alignment);
    if (image_size > UINT32_MAX - padding || image_size + padding > manager->storage_info.capacity)
        return FW_PROTOCOL_STATUS_IMAGE_TOO_LARGE;

    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_update_result flush_prefix(struct fw_update_manager* manager, size_t length) {
    size_t remaining;
    enum fw_update_result result;

    if (length == 0U)
        return FW_UPDATE_OK;
    if (length > manager->staging_length || length > UINT32_MAX || (uint32_t)length > manager->storage_info.capacity
        || manager->write_offset > manager->storage_info.capacity - (uint32_t)length)
        return FW_UPDATE_ERR_BOUNDS;

    result =
        fw_update_storage_write(manager->config.storage, manager->write_offset, manager->work_buffer, (uint32_t)length);
    if (result != FW_UPDATE_OK)
        return result;

    manager->write_offset += (uint32_t)length;
    remaining = manager->staging_length - length;
    if (remaining != 0U)
        memmove(manager->work_buffer, &manager->work_buffer[length], remaining);
    manager->staging_length = remaining;
    return FW_UPDATE_OK;
}

static enum fw_protocol_status receive_data(struct fw_update_manager* manager, const uint8_t* data,
                                            size_t data_length) {
    uint32_t alignment = manager->storage_info.write_alignment;
    size_t remaining = data_length;

    while (remaining != 0U) {
        size_t space = sizeof(manager->work_buffer) - manager->staging_length;
        size_t copy_size = remaining < space ? remaining : space;
        size_t flush_length;

        memcpy(&manager->work_buffer[manager->staging_length], data, copy_size);
        manager->staging_length += copy_size;
        manager->received_size += (uint32_t)copy_size;
        data += copy_size;
        remaining -= copy_size;

        flush_length = manager->staging_length - (manager->staging_length % alignment);
        if (flush_prefix(manager, flush_length) != FW_UPDATE_OK)
            return storage_failed(manager);
    }

    if (manager->received_size == manager->image_size && manager->staging_length != 0U) {
        size_t padded_length = alignment;
        memset(&manager->work_buffer[manager->staging_length], manager->storage_info.erased_value,
               padded_length - manager->staging_length);
        manager->staging_length = padded_length;
        if (flush_prefix(manager, padded_length) != FW_UPDATE_OK)
            return storage_failed(manager);
    }
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_hello(struct fw_update_manager* manager, const struct fw_protocol_frame* frame,
                                            uint8_t* body, uint16_t* body_length) {
    if (frame->payload_length != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (manager->state != FW_UPDATE_MANAGER_STATE_IDLE)
        return FW_PROTOCOL_STATUS_INVALID_STATE;
    if (frame->sequence != 0U)
        return FW_PROTOCOL_STATUS_BAD_SEQUENCE;

    write_u16_le(&body[0], FW_PROTOCOL_MAX_PAYLOAD);
    write_u32_le(&body[2], FW_UPDATE_MANAGER_CAPABILITIES);
    write_u32_le(&body[6], manager->config.session_timeout_ms);
    *body_length = 10U;
    manager->state = FW_UPDATE_MANAGER_STATE_NEGOTIATING;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_device_info(struct fw_update_manager* manager,
                                                  const struct fw_protocol_frame* frame, uint8_t* body,
                                                  uint16_t* body_length) {
    if (frame->payload_length != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (manager->state == FW_UPDATE_MANAGER_STATE_IDLE || manager->state == FW_UPDATE_MANAGER_STATE_PREPARING
        || manager->state == FW_UPDATE_MANAGER_STATE_VERIFYING || manager->state == FW_UPDATE_MANAGER_STATE_ABORTED)
        return FW_PROTOCOL_STATUS_INVALID_STATE;

    write_u16_le(&body[0], manager->config.board_revision);
    write_u32_le(&body[2], manager->config.hardware_id);
    write_u32_le(&body[6], manager->config.board_id);
    write_u32_le(&body[10], manager->storage_info.capacity);
    write_u32_le(&body[14], manager->storage_info.write_alignment);
    write_u32_le(&body[18], manager->storage_info.erase_alignment);
    write_version(&body[22], &manager->config.application_version);
    write_version(&body[30], &manager->config.bootloader_version);
    *body_length = 38U;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_begin(struct fw_update_manager* manager, const struct fw_protocol_frame* frame) {
    enum fw_protocol_status status;

    if (manager->state != FW_UPDATE_MANAGER_STATE_NEGOTIATING)
        return FW_PROTOCOL_STATUS_INVALID_STATE;
    status = validate_begin(manager, frame);
    if (status != FW_PROTOCOL_STATUS_OK)
        return status;

    manager->state = FW_UPDATE_MANAGER_STATE_PREPARING;
    clear_session(manager);
    manager->image_size = read_u32_le(&frame->payload[0]);
    manager->image_crc32 = read_u32_le(&frame->payload[4]);
    manager->candidate_version = read_version(&frame->payload[20]);
    if (fw_update_storage_erase_all(manager->config.storage) != FW_UPDATE_OK)
        return storage_failed(manager);

    manager->state = FW_UPDATE_MANAGER_STATE_RECEIVING;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_data(struct fw_update_manager* manager, const struct fw_protocol_frame* frame,
                                           uint8_t* body, uint16_t* body_length) {
    uint32_t offset;
    uint32_t data_length;
    enum fw_protocol_status status;

    if (manager->state != FW_UPDATE_MANAGER_STATE_RECEIVING)
        return FW_PROTOCOL_STATUS_INVALID_STATE;
    if (frame->payload_length < 5U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;

    offset = read_u32_le(frame->payload);
    data_length = (uint32_t)frame->payload_length - 4U;
    if (offset != manager->received_size)
        return FW_PROTOCOL_STATUS_OFFSET_MISMATCH;
    if (manager->received_size > manager->image_size) {
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        return FW_PROTOCOL_STATUS_INTERNAL_ERROR;
    }
    if (data_length > manager->image_size - manager->received_size)
        return FW_PROTOCOL_STATUS_INVALID_ARGUMENT;

    status = receive_data(manager, &frame->payload[4], data_length);
    if (status != FW_PROTOCOL_STATUS_OK)
        return status;
    write_u32_le(body, manager->received_size);
    *body_length = 4U;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_end(struct fw_update_manager* manager, const struct fw_protocol_frame* frame,
                                          uint8_t* body, uint16_t* body_length) {
    struct fw_protocol_crc32_context crc;
    uint32_t offset = 0U;
    uint32_t actual_crc;

    if (frame->payload_length != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (manager->state != FW_UPDATE_MANAGER_STATE_RECEIVING)
        return FW_PROTOCOL_STATUS_INVALID_STATE;
    if (manager->received_size != manager->image_size)
        return FW_PROTOCOL_STATUS_VERIFY_ERROR;
    if (manager->staging_length != 0U) {
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        return FW_PROTOCOL_STATUS_INTERNAL_ERROR;
    }

    manager->state = FW_UPDATE_MANAGER_STATE_VERIFYING;
    fw_protocol_crc32_init(&crc);
    while (offset < manager->image_size) {
        uint32_t remaining = manager->image_size - offset;
        uint32_t block_size = remaining < sizeof(manager->work_buffer) ? remaining : sizeof(manager->work_buffer);
        if (fw_update_storage_read(manager->config.storage, offset, manager->work_buffer, block_size) != FW_UPDATE_OK)
            return storage_failed(manager);
        if (fw_protocol_crc32_update(&crc, manager->work_buffer, block_size) != FW_PROTOCOL_OK) {
            manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
            return FW_PROTOCOL_STATUS_INTERNAL_ERROR;
        }
        offset += block_size;
    }

    actual_crc = fw_protocol_crc32_finalize(&crc);
    if (actual_crc != manager->image_crc32) {
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        return FW_PROTOCOL_STATUS_VERIFY_ERROR;
    }

    write_u32_le(&body[0], manager->image_size);
    write_u32_le(&body[4], actual_crc);
    *body_length = 8U;
    manager->state = FW_UPDATE_MANAGER_STATE_READY_TO_COMMIT;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_abort(struct fw_update_manager* manager, const struct fw_protocol_frame* frame) {
    if (frame->payload_length != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (!state_allows_abort(manager->state))
        return FW_PROTOCOL_STATUS_INVALID_STATE;

    clear_session(manager);
    manager->state = FW_UPDATE_MANAGER_STATE_ABORTED;
    manager->return_to_idle_after_tx = 1U;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status handle_commit(struct fw_update_manager* manager, const struct fw_protocol_frame* frame) {
    if (frame->payload_length != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (manager->state != FW_UPDATE_MANAGER_STATE_READY_TO_COMMIT)
        return FW_PROTOCOL_STATUS_INVALID_STATE;

    manager->state = FW_UPDATE_MANAGER_STATE_COMMITTING;
    if (fw_update_boot_control_request_test_upgrade(manager->config.boot_control) != FW_UPDATE_OK) {
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        return FW_PROTOCOL_STATUS_BOOT_CONTROL_ERROR;
    }

    manager->commit_succeeded = 1U;
    manager->commit_response_consumed = 0U;
    manager->tx_idle_seen = 0U;
    manager->pending_action = FW_UPDATE_MANAGER_ACTION_NONE;
    manager->state = FW_UPDATE_MANAGER_STATE_COMPLETED;
    return FW_PROTOCOL_STATUS_OK;
}

static enum fw_protocol_status validate_request_frame(const struct fw_protocol_frame* frame) {
    if (frame->flags != 0U)
        return FW_PROTOCOL_STATUS_BAD_FRAME;
    if (frame->major != FW_PROTOCOL_VERSION_MAJOR || frame->minor > FW_PROTOCOL_VERSION_MINOR)
        return FW_PROTOCOL_STATUS_INCOMPATIBLE_VERSION;

    switch (frame->command) {
        case FW_PROTOCOL_COMMAND_HELLO:
        case FW_PROTOCOL_COMMAND_DEVICE_INFO:
        case FW_PROTOCOL_COMMAND_END:
        case FW_PROTOCOL_COMMAND_COMMIT:
        case FW_PROTOCOL_COMMAND_ABORT:
            return frame->payload_length == 0U ? FW_PROTOCOL_STATUS_OK : FW_PROTOCOL_STATUS_BAD_FRAME;
        case FW_PROTOCOL_COMMAND_BEGIN:
            return frame->payload_length == 28U ? FW_PROTOCOL_STATUS_OK : FW_PROTOCOL_STATUS_BAD_FRAME;
        case FW_PROTOCOL_COMMAND_DATA:
            return frame->payload_length >= 5U ? FW_PROTOCOL_STATUS_OK : FW_PROTOCOL_STATUS_BAD_FRAME;
        default:
            return FW_PROTOCOL_STATUS_OK;
    }
}

static enum fw_protocol_status handle_frame(struct fw_update_manager* manager, const struct fw_protocol_frame* frame,
                                            uint8_t* body, uint16_t* body_length) {
    *body_length = 0U;

    switch (frame->command) {
        case FW_PROTOCOL_COMMAND_HELLO:
            return handle_hello(manager, frame, body, body_length);
        case FW_PROTOCOL_COMMAND_DEVICE_INFO:
            return handle_device_info(manager, frame, body, body_length);
        case FW_PROTOCOL_COMMAND_BEGIN:
            return handle_begin(manager, frame);
        case FW_PROTOCOL_COMMAND_DATA:
            return handle_data(manager, frame, body, body_length);
        case FW_PROTOCOL_COMMAND_END:
            return handle_end(manager, frame, body, body_length);
        case FW_PROTOCOL_COMMAND_COMMIT:
            return handle_commit(manager, frame);
        case FW_PROTOCOL_COMMAND_ABORT:
            return handle_abort(manager, frame);
        default:
            return FW_PROTOCOL_STATUS_UNSUPPORTED_COMMAND;
    }
}

static int protocol_error_limit_reached(struct fw_update_manager* manager) {
    if (manager->session_active == 0U || manager->state == FW_UPDATE_MANAGER_STATE_COMPLETED
        || manager->state == FW_UPDATE_MANAGER_STATE_ABORTED)
        return 0;
    if (manager->protocol_error_count < FW_UPDATE_MANAGER_MAX_PROTOCOL_ERRORS)
        ++manager->protocol_error_count;
    return manager->protocol_error_count == FW_UPDATE_MANAGER_MAX_PROTOCOL_ERRORS;
}

static enum fw_update_manager_result queue_response(struct fw_update_manager* manager,
                                                    const struct fw_protocol_frame* request,
                                                    enum fw_protocol_status status, const uint8_t* body,
                                                    uint16_t body_length, size_t request_size, int accepted) {
    uint8_t payload[FW_UPDATE_MANAGER_MAX_RESPONSE_PAYLOAD];
    uint8_t* output = accepted != 0 ? manager->tx_buffer : manager->parser.buffer;
    struct fw_protocol_frame response = {
        .major = FW_PROTOCOL_VERSION_MAJOR,
        .minor = FW_PROTOCOL_VERSION_MINOR,
        .command = request->command,
        .flags = FW_PROTOCOL_FLAG_RESPONSE,
        .sequence = request->sequence,
        .payload = payload,
        .payload_length = (uint16_t)(body_length + 2U),
    };

    if (body_length > FW_UPDATE_MANAGER_MAX_RESPONSE_PAYLOAD - 2U) {
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        manager->last_status = FW_PROTOCOL_STATUS_INTERNAL_ERROR;
        return FW_UPDATE_MANAGER_ERR_INTERNAL;
    }
    if (body_length != 0U)
        memcpy(&payload[2], body, body_length);
    manager->last_status = status;
    write_u16_le(payload, (uint16_t)status);
    if (status != FW_PROTOCOL_STATUS_OK) {
        response.flags |= FW_PROTOCOL_FLAG_ERROR;
        response.payload_length = 2U;
    }

    manager->tx_offset = 0U;
    if (fw_protocol_encode(&response, output, FW_PROTOCOL_MAX_FRAME_SIZE, &manager->tx_size) != FW_PROTOCOL_OK) {
        manager->tx_size = 0U;
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        manager->last_status = FW_PROTOCOL_STATUS_INTERNAL_ERROR;
        return FW_UPDATE_MANAGER_ERR_INTERNAL;
    }

    manager->tx_uses_parser_buffer = accepted == 0 ? 1U : 0U;
    manager->pending_request_size = accepted != 0 ? request_size : 0U;
    manager->cache_request_after_tx = accepted != 0 ? 1U : 0U;
    manager->tx_is_successful_commit = accepted != 0 && status == FW_PROTOCOL_STATUS_OK
                                               && request->command == FW_PROTOCOL_COMMAND_COMMIT
                                               && manager->state == FW_UPDATE_MANAGER_STATE_COMPLETED
                                           ? 1U
                                           : 0U;
    if (accepted != 0) {
        memcpy(manager->cached_response, output, manager->tx_size);
        manager->cached_response_size = manager->tx_size;
        manager->cached_commit_success = manager->tx_is_successful_commit;
    }
    return FW_UPDATE_MANAGER_RESPONSE_READY;
}

static enum fw_update_manager_result queue_cached_response(struct fw_update_manager* manager, size_t request_size) {
    if (manager->cached_response_size == 0U || manager->cached_response_size > sizeof(manager->cached_response))
        return FW_UPDATE_MANAGER_ERR_INTERNAL;

    memcpy(manager->tx_buffer, manager->cached_response, manager->cached_response_size);
    manager->tx_size = manager->cached_response_size;
    manager->tx_offset = 0U;
    manager->pending_request_size = request_size;
    manager->tx_uses_parser_buffer = 0U;
    manager->cache_request_after_tx = 1U;
    manager->tx_is_successful_commit = manager->cached_commit_success;
    manager->last_status = (enum fw_protocol_status)read_u16_le(&manager->cached_response[FW_PROTOCOL_HEADER_SIZE]);
    return FW_UPDATE_MANAGER_RESPONSE_READY;
}

static int request_is_exact_duplicate(const struct fw_update_manager* manager, size_t request_size) {
    return manager->cached_request_size == request_size && request_size != 0U
           && memcmp(manager->tx_buffer, manager->parser.buffer, request_size) == 0;
}

enum fw_update_manager_result fw_update_manager_init(struct fw_update_manager* manager,
                                                     const struct fw_update_manager_config* config) {
    struct fw_update_manager_config config_copy;
    struct fw_update_storage_info storage_info;

    if (manager == NULL || config == NULL || config->storage == NULL || config->boot_control == NULL
        || config->session_timeout_ms == 0U)
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;
    if (fw_update_storage_get_info(config->storage, &storage_info) != FW_UPDATE_OK)
        return FW_UPDATE_MANAGER_ERR_STORAGE;

    config_copy = *config;
    memset(manager, 0, sizeof(*manager));
    manager->config = config_copy;
    manager->storage_info = storage_info;
    manager->state = FW_UPDATE_MANAGER_STATE_IDLE;
    manager->last_status = FW_PROTOCOL_STATUS_OK;
    fw_protocol_parser_init(&manager->parser);
    return FW_UPDATE_MANAGER_OK;
}

enum fw_update_manager_result fw_update_manager_feed(struct fw_update_manager* manager, const uint8_t* input,
                                                     size_t input_size, uint32_t now_ms, size_t* consumed) {
    struct fw_protocol_frame request;
    enum fw_protocol_result result;
    enum fw_protocol_status status;
    uint8_t body[FW_UPDATE_MANAGER_MAX_RESPONSE_PAYLOAD - 2U];
    uint16_t body_length = 0U;
    size_t request_size;

    if (consumed != NULL)
        *consumed = 0U;
    if (manager == NULL || consumed == NULL || (input == NULL && input_size != 0U))
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;
    if (manager->tx_offset != manager->tx_size)
        return FW_UPDATE_MANAGER_ERR_BUSY;

    result = fw_protocol_parser_feed(&manager->parser, input, input_size, consumed, &request);
    if (result == FW_PROTOCOL_NEED_MORE)
        return FW_UPDATE_MANAGER_NEED_MORE;
    if (result == FW_PROTOCOL_ERR_CRC || result == FW_PROTOCOL_ERR_FORMAT) {
        manager->last_status = FW_PROTOCOL_STATUS_BAD_FRAME;
        if (protocol_error_limit_reached(manager) != 0) {
            reset_to_idle(manager, 1);
            manager->last_status = FW_PROTOCOL_STATUS_BAD_FRAME;
        }
        return FW_UPDATE_MANAGER_ERR_PROTOCOL;
    }
    if (result != FW_PROTOCOL_FRAME_READY) {
        manager->state = FW_UPDATE_MANAGER_STATE_ERROR;
        manager->last_status = FW_PROTOCOL_STATUS_INTERNAL_ERROR;
        return FW_UPDATE_MANAGER_ERR_INTERNAL;
    }

    request_size = FW_PROTOCOL_HEADER_SIZE + (size_t)request.payload_length + FW_PROTOCOL_CRC_SIZE;
    if (request_is_exact_duplicate(manager, request_size) != 0) {
        manager->protocol_error_count = 0U;
        manager->last_activity_ms = now_ms;
        return queue_cached_response(manager, request_size);
    }

    status = validate_request_frame(&request);
    if (manager->state == FW_UPDATE_MANAGER_STATE_IDLE && request.command == FW_PROTOCOL_COMMAND_HELLO
        && request.sequence == 0U && status == FW_PROTOCOL_STATUS_OK) {
        clear_session(manager);
        clear_cached_exchange(manager);
        clear_commit_lifecycle(manager);
        manager->protocol_error_count = 0U;
        manager->expected_sequence = 0U;
        manager->session_active = 1U;
        status = handle_frame(manager, &request, body, &body_length);
    } else if (manager->session_active == 0U || manager->state == FW_UPDATE_MANAGER_STATE_COMPLETED) {
        if (status == FW_PROTOCOL_STATUS_OK)
            status = FW_PROTOCOL_STATUS_INVALID_STATE;
        return queue_response(manager, &request, status, NULL, 0U, request_size, 0);
    } else if (request.sequence != manager->expected_sequence) {
        if (protocol_error_limit_reached(manager) != 0) {
            clear_session(manager);
            manager->state = FW_UPDATE_MANAGER_STATE_ABORTED;
            manager->return_to_idle_after_tx = 1U;
            manager->clear_cache_after_tx = 1U;
        }
        return queue_response(manager, &request, FW_PROTOCOL_STATUS_BAD_SEQUENCE, NULL, 0U, request_size, 0);
    } else if (status == FW_PROTOCOL_STATUS_OK) {
        status = handle_frame(manager, &request, body, &body_length);
    }

    if (status == FW_PROTOCOL_STATUS_BAD_FRAME || status == FW_PROTOCOL_STATUS_INCOMPATIBLE_VERSION) {
        if (status == FW_PROTOCOL_STATUS_BAD_FRAME && protocol_error_limit_reached(manager) != 0) {
            clear_session(manager);
            manager->state = FW_UPDATE_MANAGER_STATE_ABORTED;
            manager->return_to_idle_after_tx = 1U;
            manager->clear_cache_after_tx = 1U;
        }
        return queue_response(manager, &request, status, NULL, 0U, request_size, 0);
    }

    manager->protocol_error_count = 0U;
    manager->last_activity_ms = now_ms;
    ++manager->expected_sequence;
    return queue_response(manager, &request, status, body, body_length, request_size, 1);
}

enum fw_update_manager_result fw_update_manager_tx_view(const struct fw_update_manager* manager, const uint8_t** data,
                                                        size_t* size) {
    if (manager == NULL || data == NULL || size == NULL || manager->tx_offset > manager->tx_size)
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;

    *size = manager->tx_size - manager->tx_offset;
    *data = *size == 0U ? NULL
                        : &(manager->tx_uses_parser_buffer != 0U ? manager->parser.buffer
                                                                 : manager->tx_buffer)[manager->tx_offset];
    return FW_UPDATE_MANAGER_OK;
}

enum fw_update_manager_result fw_update_manager_consume_tx(struct fw_update_manager* manager, size_t size) {
    if (manager == NULL || manager->tx_offset > manager->tx_size || size > manager->tx_size - manager->tx_offset)
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;

    manager->tx_offset += size;
    if (manager->tx_offset == manager->tx_size)
        finish_tx(manager);
    return FW_UPDATE_MANAGER_OK;
}

enum fw_update_manager_result fw_update_manager_poll(struct fw_update_manager* manager, uint32_t now_ms) {
    if (manager == NULL)
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;
    if (!timeout_is_active(manager) || manager->tx_offset != manager->tx_size)
        return FW_UPDATE_MANAGER_OK;
    if ((uint32_t)(now_ms - manager->last_activity_ms) < manager->config.session_timeout_ms)
        return FW_UPDATE_MANAGER_OK;

    manager->last_status = FW_PROTOCOL_STATUS_TIMEOUT;
    reset_to_idle(manager, 1);
    manager->last_status = FW_PROTOCOL_STATUS_TIMEOUT;
    return FW_UPDATE_MANAGER_OK;
}

enum fw_update_manager_result fw_update_manager_notify_disconnect(struct fw_update_manager* manager) {
    if (manager == NULL)
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;

    if (manager->commit_succeeded != 0U) {
        if (manager->cache_request_after_tx != 0U) {
            memcpy(manager->tx_buffer, manager->parser.buffer, manager->pending_request_size);
            manager->cached_request_size = manager->pending_request_size;
        }
        clear_tx(manager);
        fw_protocol_parser_reset(&manager->parser);
        manager->commit_response_consumed = 0U;
        manager->tx_idle_seen = 0U;
        manager->pending_action = FW_UPDATE_MANAGER_ACTION_NONE;
        return FW_UPDATE_MANAGER_OK;
    }

    reset_to_idle(manager, 1);
    return FW_UPDATE_MANAGER_OK;
}

enum fw_update_manager_result fw_update_manager_notify_tx_idle(struct fw_update_manager* manager) {
    if (manager == NULL)
        return FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT;
    if (manager->commit_succeeded != 0U) {
        manager->tx_idle_seen = 1U;
        schedule_reset_if_ready(manager);
    }
    return FW_UPDATE_MANAGER_OK;
}

enum fw_update_manager_action fw_update_manager_take_action(struct fw_update_manager* manager) {
    enum fw_update_manager_action action;

    if (manager == NULL)
        return FW_UPDATE_MANAGER_ACTION_NONE;
    action = manager->pending_action;
    manager->pending_action = FW_UPDATE_MANAGER_ACTION_NONE;
    if (action == FW_UPDATE_MANAGER_ACTION_RESET)
        manager->reset_action_taken = 1U;
    return action;
}

enum fw_update_manager_state fw_update_manager_get_state(const struct fw_update_manager* manager) {
    return manager == NULL ? FW_UPDATE_MANAGER_STATE_ERROR : manager->state;
}

enum fw_protocol_status fw_update_manager_get_last_status(const struct fw_update_manager* manager) {
    return manager == NULL ? FW_PROTOCOL_STATUS_INTERNAL_ERROR : manager->last_status;
}
