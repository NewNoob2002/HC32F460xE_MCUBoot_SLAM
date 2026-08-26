#include "fw_update/protocol.h"

#include <string.h>

#define FW_PROTOCOL_MAGIC_SIZE 4U

static const uint8_t fw_protocol_magic[FW_PROTOCOL_MAGIC_SIZE] = {'F', 'W', 'U', 'P'};

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

static int flags_are_valid(uint8_t flags) {
    return flags == 0U || flags == FW_PROTOCOL_FLAG_RESPONSE
           || flags == (FW_PROTOCOL_FLAG_RESPONSE | FW_PROTOCOL_FLAG_ERROR);
}

void fw_protocol_crc32_init(struct fw_protocol_crc32_context* context) {
    if (context != NULL)
        context->state = UINT32_C(0xFFFFFFFF);
}

enum fw_protocol_result fw_protocol_crc32_update(struct fw_protocol_crc32_context* context, const void* data,
                                                 size_t length) {
    const uint8_t* bytes = data;
    size_t index;

    if (context == NULL || (bytes == NULL && length != 0U))
        return FW_PROTOCOL_ERR_INVALID_ARGUMENT;

    for (index = 0U; index < length; ++index) {
        uint32_t bit;
        context->state ^= bytes[index];
        for (bit = 0U; bit < 8U; ++bit)
            context->state = (context->state >> 1U) ^ (UINT32_C(0xEDB88320) & (0U - (context->state & 1U)));
    }
    return FW_PROTOCOL_OK;
}

uint32_t fw_protocol_crc32_finalize(const struct fw_protocol_crc32_context* context) {
    return context == NULL ? 0U : context->state ^ UINT32_C(0xFFFFFFFF);
}

uint32_t fw_protocol_crc32(const void* data, size_t length) {
    struct fw_protocol_crc32_context context;
    fw_protocol_crc32_init(&context);
    if (fw_protocol_crc32_update(&context, data, length) != FW_PROTOCOL_OK)
        return 0U;
    return fw_protocol_crc32_finalize(&context);
}

enum fw_protocol_result fw_protocol_encode(const struct fw_protocol_frame* frame, uint8_t* output,
                                           size_t output_capacity, size_t* output_size) {
    size_t frame_size;
    uint32_t crc;

    if (output_size != NULL)
        *output_size = 0U;
    if (frame == NULL || output == NULL || output_size == NULL)
        return FW_PROTOCOL_ERR_INVALID_ARGUMENT;
    if (frame->payload_length > FW_PROTOCOL_MAX_PAYLOAD || !flags_are_valid(frame->flags))
        return FW_PROTOCOL_ERR_FORMAT;
    if (frame->payload_length != 0U && frame->payload == NULL)
        return FW_PROTOCOL_ERR_INVALID_ARGUMENT;

    frame_size = FW_PROTOCOL_HEADER_SIZE + (size_t)frame->payload_length + FW_PROTOCOL_CRC_SIZE;
    if (output_capacity < frame_size)
        return FW_PROTOCOL_ERR_BUFFER_TOO_SMALL;

    memcpy(output, fw_protocol_magic, FW_PROTOCOL_MAGIC_SIZE);
    output[4] = frame->major;
    output[5] = frame->minor;
    output[6] = frame->command;
    output[7] = frame->flags;
    write_u32_le(&output[8], frame->sequence);
    write_u16_le(&output[12], frame->payload_length);
    write_u16_le(&output[14], 0U);
    if (frame->payload_length != 0U)
        memcpy(&output[FW_PROTOCOL_HEADER_SIZE], frame->payload, frame->payload_length);

    crc = fw_protocol_crc32(output, frame_size - FW_PROTOCOL_CRC_SIZE);
    write_u32_le(&output[frame_size - FW_PROTOCOL_CRC_SIZE], crc);
    *output_size = frame_size;
    return FW_PROTOCOL_OK;
}

enum fw_protocol_result fw_protocol_decode(const uint8_t* input, size_t input_size, struct fw_protocol_frame* frame) {
    uint16_t payload_length;
    size_t frame_size;
    uint32_t expected_crc;
    uint32_t actual_crc;

    if (input == NULL || frame == NULL)
        return FW_PROTOCOL_ERR_INVALID_ARGUMENT;
    if (input_size < FW_PROTOCOL_HEADER_SIZE)
        return FW_PROTOCOL_NEED_MORE;
    if (memcmp(input, fw_protocol_magic, FW_PROTOCOL_MAGIC_SIZE) != 0)
        return FW_PROTOCOL_ERR_FORMAT;

    payload_length = read_u16_le(&input[12]);
    if (payload_length > FW_PROTOCOL_MAX_PAYLOAD || read_u16_le(&input[14]) != 0U || !flags_are_valid(input[7]))
        return FW_PROTOCOL_ERR_FORMAT;

    frame_size = FW_PROTOCOL_HEADER_SIZE + (size_t)payload_length + FW_PROTOCOL_CRC_SIZE;
    if (input_size < frame_size)
        return FW_PROTOCOL_NEED_MORE;
    if (input_size != frame_size)
        return FW_PROTOCOL_ERR_FORMAT;

    expected_crc = read_u32_le(&input[frame_size - FW_PROTOCOL_CRC_SIZE]);
    actual_crc = fw_protocol_crc32(input, frame_size - FW_PROTOCOL_CRC_SIZE);
    if (actual_crc != expected_crc)
        return FW_PROTOCOL_ERR_CRC;

    frame->major = input[4];
    frame->minor = input[5];
    frame->command = input[6];
    frame->flags = input[7];
    frame->sequence = read_u32_le(&input[8]);
    frame->payload = payload_length == 0U ? NULL : &input[FW_PROTOCOL_HEADER_SIZE];
    frame->payload_length = payload_length;
    return FW_PROTOCOL_OK;
}

void fw_protocol_parser_reset(struct fw_protocol_parser* parser) {
    if (parser == NULL)
        return;
    parser->length = 0U;
    parser->expected_size = 0U;
}

void fw_protocol_parser_init(struct fw_protocol_parser* parser) {
    fw_protocol_parser_reset(parser);
}

static void parser_seek_magic(struct fw_protocol_parser* parser, uint8_t byte) {
    if (byte == fw_protocol_magic[parser->length]) {
        parser->buffer[parser->length] = byte;
        ++parser->length;
    } else if (byte == fw_protocol_magic[0]) {
        parser->buffer[0] = byte;
        parser->length = 1U;
    } else {
        parser->length = 0U;
    }
}

static void parser_resync_header(struct fw_protocol_parser* parser) {
    size_t start;

    for (start = 1U; start < parser->length; ++start) {
        size_t remaining = parser->length - start;
        size_t prefix_size = remaining < FW_PROTOCOL_MAGIC_SIZE ? remaining : FW_PROTOCOL_MAGIC_SIZE;
        if (memcmp(&parser->buffer[start], fw_protocol_magic, prefix_size) == 0) {
            memmove(parser->buffer, &parser->buffer[start], remaining);
            parser->length = remaining;
            parser->expected_size = 0U;
            return;
        }
    }
    fw_protocol_parser_reset(parser);
}

enum fw_protocol_result fw_protocol_parser_feed(struct fw_protocol_parser* parser, const uint8_t* input,
                                                size_t input_size, size_t* consumed, struct fw_protocol_frame* frame) {
    size_t index = 0U;

    if (consumed != NULL)
        *consumed = 0U;
    if (parser == NULL || consumed == NULL || frame == NULL || (input == NULL && input_size != 0U))
        return FW_PROTOCOL_ERR_INVALID_ARGUMENT;

    while (index < input_size) {
        if (parser->length < FW_PROTOCOL_MAGIC_SIZE) {
            parser_seek_magic(parser, input[index]);
            ++index;
            continue;
        }

        parser->buffer[parser->length] = input[index];
        ++parser->length;
        ++index;

        if (parser->length == FW_PROTOCOL_HEADER_SIZE) {
            uint16_t payload_length = read_u16_le(&parser->buffer[12]);
            if (payload_length > FW_PROTOCOL_MAX_PAYLOAD || read_u16_le(&parser->buffer[14]) != 0U
                || !flags_are_valid(parser->buffer[7])) {
                parser_resync_header(parser);
                *consumed = index;
                return FW_PROTOCOL_ERR_FORMAT;
            }
            parser->expected_size = FW_PROTOCOL_HEADER_SIZE + (size_t)payload_length + FW_PROTOCOL_CRC_SIZE;
        }

        if (parser->expected_size != 0U && parser->length == parser->expected_size) {
            enum fw_protocol_result result = fw_protocol_decode(parser->buffer, parser->expected_size, frame);
            fw_protocol_parser_reset(parser);
            *consumed = index;
            return result == FW_PROTOCOL_OK ? FW_PROTOCOL_FRAME_READY : result;
        }
    }

    *consumed = index;
    return FW_PROTOCOL_NEED_MORE;
}
