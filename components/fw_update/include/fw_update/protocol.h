#ifndef FW_UPDATE_PROTOCOL_H
#define FW_UPDATE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FW_PROTOCOL_VERSION_MAJOR  1U
#define FW_PROTOCOL_VERSION_MINOR  0U
#define FW_PROTOCOL_HEADER_SIZE    16U
#define FW_PROTOCOL_MAX_PAYLOAD    512U
#define FW_PROTOCOL_CRC_SIZE       4U
#define FW_PROTOCOL_MAX_FRAME_SIZE (FW_PROTOCOL_HEADER_SIZE + FW_PROTOCOL_MAX_PAYLOAD + FW_PROTOCOL_CRC_SIZE)

/** Protocol V1 command identifiers. */
enum fw_protocol_command {
    FW_PROTOCOL_COMMAND_HELLO = 0x01,
    FW_PROTOCOL_COMMAND_DEVICE_INFO = 0x02,
    FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_GET = 0x03,
    FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_SET = 0x04,
    FW_PROTOCOL_COMMAND_BEGIN = 0x10,
    FW_PROTOCOL_COMMAND_DATA = 0x11,
    FW_PROTOCOL_COMMAND_END = 0x12,
    FW_PROTOCOL_COMMAND_COMMIT = 0x13,
    FW_PROTOCOL_COMMAND_ABORT = 0x14,
};

/** Protocol V1 frame flags. */
enum fw_protocol_flag {
    FW_PROTOCOL_FLAG_RESPONSE = 0x01,
    FW_PROTOCOL_FLAG_ERROR = 0x02,
};

/** Stable Protocol V1 wire status values carried in response payloads. */
enum fw_protocol_status {
    FW_PROTOCOL_STATUS_OK = 0,
    FW_PROTOCOL_STATUS_BAD_FRAME = 1,
    FW_PROTOCOL_STATUS_INCOMPATIBLE_VERSION = 2,
    FW_PROTOCOL_STATUS_UNSUPPORTED_COMMAND = 3,
    FW_PROTOCOL_STATUS_BAD_SEQUENCE = 4,
    FW_PROTOCOL_STATUS_INVALID_STATE = 5,
    FW_PROTOCOL_STATUS_INVALID_ARGUMENT = 6,
    FW_PROTOCOL_STATUS_IMAGE_TOO_LARGE = 7,
    FW_PROTOCOL_STATUS_OFFSET_MISMATCH = 8,
    FW_PROTOCOL_STATUS_STORAGE_ERROR = 9,
    FW_PROTOCOL_STATUS_VERIFY_ERROR = 10,
    FW_PROTOCOL_STATUS_BOOT_CONTROL_ERROR = 11,
    FW_PROTOCOL_STATUS_TIMEOUT = 12,
    FW_PROTOCOL_STATUS_INTERNAL_ERROR = 13,
};

/** Protocol V1 capability bits returned by HELLO. */
enum fw_protocol_capability {
    FW_PROTOCOL_CAPABILITY_TEST_UPGRADE = 1U << 0U,
    FW_PROTOCOL_CAPABILITY_READBACK_CRC = 1U << 1U,
    FW_PROTOCOL_CAPABILITY_STRICT_DATA = 1U << 2U,
    FW_PROTOCOL_CAPABILITY_PRODUCT_CONFIG = 1U << 3U,
};

/** Results returned by the Protocol codec and incremental parser. */
enum fw_protocol_result {
    FW_PROTOCOL_OK = 0,
    FW_PROTOCOL_NEED_MORE = 1,
    FW_PROTOCOL_FRAME_READY = 2,
    FW_PROTOCOL_ERR_INVALID_ARGUMENT = -1,
    FW_PROTOCOL_ERR_BUFFER_TOO_SMALL = -2,
    FW_PROTOCOL_ERR_FORMAT = -3,
    FW_PROTOCOL_ERR_CRC = -4,
};

/** Decoded frame view. Payload memory is borrowed, never owned. */
struct fw_protocol_frame {
    uint8_t major;
    uint8_t minor;
    uint8_t command;
    uint8_t flags;
    uint32_t sequence;
    const uint8_t* payload;
    uint16_t payload_length;
};

/** Caller-allocated incremental parser state. Treat members as private. */
struct fw_protocol_parser {
    uint8_t buffer[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t length;
    size_t expected_size;
};

/** Caller-allocated incremental CRC-32/ISO-HDLC state. */
struct fw_protocol_crc32_context {
    uint32_t state;
};

/** Initialize an incremental CRC context. */
void fw_protocol_crc32_init(struct fw_protocol_crc32_context* context);

/** Add bytes to an incremental CRC context. */
enum fw_protocol_result fw_protocol_crc32_update(struct fw_protocol_crc32_context* context, const void* data,
                                                 size_t length);

/** Finalize an incremental CRC context without modifying it. */
uint32_t fw_protocol_crc32_finalize(const struct fw_protocol_crc32_context* context);

/** Compute CRC-32/ISO-HDLC. A null data pointer is valid only for zero length. */
uint32_t fw_protocol_crc32(const void* data, size_t length);

/**
 * Encode one complete frame into caller-owned output storage. Output storage
 * must not overlap frame payload storage.
 */
enum fw_protocol_result fw_protocol_encode(const struct fw_protocol_frame* frame, uint8_t* output,
                                           size_t output_capacity, size_t* output_size);

/**
 * Decode one exact complete frame. The returned payload borrows input storage
 * and remains valid only while that storage is unchanged.
 */
enum fw_protocol_result fw_protocol_decode(const uint8_t* input, size_t input_size, struct fw_protocol_frame* frame);

/** Reset a parser to its initial magic-search state. */
void fw_protocol_parser_init(struct fw_protocol_parser* parser);

/** Reset a parser and discard any partial frame. */
void fw_protocol_parser_reset(struct fw_protocol_parser* parser);

/**
 * Consume borrowed byte-stream input until one frame or error is produced.
 *
 * The parser must be initialized before the first feed call.
 * The call is non-blocking with work bounded by input_size. Access to one parser
 * instance must be serialized by the caller and is intended for Application
 * context, not concurrent ISR/task use.
 *
 * On FW_PROTOCOL_FRAME_READY, frame->payload points into parser storage and is
 * valid until the next feed/reset call. Unconsumed coalesced bytes remain with
 * the caller. A malformed header is rejected after 16 bytes while retaining a
 * possible overlapping magic prefix for the next feed call.
 */
enum fw_protocol_result fw_protocol_parser_feed(struct fw_protocol_parser* parser, const uint8_t* input,
                                                size_t input_size, size_t* consumed, struct fw_protocol_frame* frame);

#ifdef __cplusplus
}
#endif

#endif
