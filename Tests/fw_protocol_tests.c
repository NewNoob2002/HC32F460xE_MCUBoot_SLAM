#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "fw_update/protocol.h"

_Static_assert(FW_PROTOCOL_HEADER_SIZE == 16U, "Protocol V1 header size changed");
_Static_assert(FW_PROTOCOL_MAX_PAYLOAD == 512U, "Protocol V1 payload limit changed");
_Static_assert(FW_PROTOCOL_MAX_FRAME_SIZE == 532U, "Protocol V1 frame limit changed");
_Static_assert(FW_PROTOCOL_STATUS_OK == 0, "Protocol V1 status changed");
_Static_assert(FW_PROTOCOL_STATUS_INTERNAL_ERROR == 13, "Protocol V1 status changed");

static const char* const golden_frames[] = {
    "46575550010001000000000000000000b38a895a",
    "4657555001000101000000000c000000000000020700000088130000773bc13a",
    "465755500100020001000000000000002e31147d",
    "46575550010002010100000028000000000002000046000001000000000003000400000000200000020103000400000001000000000000000c"
    "d046ae",
    "4657555001001000020000001c000000572d0000efcdab8900460000010000000200000002010100000000009ba36d9f",
    "46575550010010010200000002000000000056d0529c",
    "4657555001001100030000001400000000000000000102030405060708090a0b0c0d0e0f4700cabe",
    "46575550010011010300000006000000000010000000e9dd0a59",
    "46575550010012000400000000000000a2be5a12",
    "4657555001001201040000000a0000000000572d0000efcdab89e73986c0",
    "4657555001001300050000000000000002d53231",
    "465755500100130105000000020000000000df191009",
    "4657555001001400020000000000000062c72bd9",
    "46575550010014010200000002000000000029eb549f",
    "465755500200010000000000000000004358172d",
    "465755500100010300000000020000000200bb7a4065",
    "4657555001001103090000000200000004000b1b17c3",
    "46575550010010030200000002000000070050ff7f8b",
    "465755500100100302000000020000000900ded2fc15",
};

static uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9')
        return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f')
        return (uint8_t)(value - 'a' + 10);
    assert(0);
    return 0U;
}

static size_t decode_hex(const char* hex, uint8_t* output, size_t capacity) {
    size_t hex_length = strlen(hex);
    size_t output_length = hex_length / 2U;
    size_t index;

    assert((hex_length % 2U) == 0U);
    assert(output_length <= capacity);
    for (index = 0U; index < output_length; ++index) {
        uint8_t high = hex_nibble(hex[index * 2U]);
        uint8_t low = hex_nibble(hex[index * 2U + 1U]);
        output[index] = (uint8_t)((uint8_t)(high << 4U) | low);
    }
    return output_length;
}

static size_t make_frame(uint16_t payload_length, uint8_t* output) {
    static uint8_t payload[FW_PROTOCOL_MAX_PAYLOAD];
    struct fw_protocol_frame frame = {
        .major = FW_PROTOCOL_VERSION_MAJOR,
        .minor = FW_PROTOCOL_VERSION_MINOR,
        .command = FW_PROTOCOL_COMMAND_DATA,
        .flags = 0U,
        .sequence = UINT32_C(0x12345678),
        .payload = payload_length == 0U ? NULL : payload,
        .payload_length = payload_length,
    };
    size_t index;
    size_t output_size = 0U;

    for (index = 0U; index < payload_length; ++index)
        payload[index] = (uint8_t)index;
    assert(fw_protocol_encode(&frame, output, FW_PROTOCOL_MAX_FRAME_SIZE, &output_size) == FW_PROTOCOL_OK);
    return output_size;
}

static void assert_frame_ready(const struct fw_protocol_frame* frame, uint16_t payload_length) {
    assert(frame->major == FW_PROTOCOL_VERSION_MAJOR);
    assert(frame->minor == FW_PROTOCOL_VERSION_MINOR);
    assert(frame->command == FW_PROTOCOL_COMMAND_DATA);
    assert(frame->flags == 0U);
    assert(frame->sequence == UINT32_C(0x12345678));
    assert(frame->payload_length == payload_length);
    if (payload_length == 0U) {
        assert(frame->payload == NULL);
    } else {
        assert(frame->payload != NULL);
        assert(frame->payload[0] == 0U);
        assert(frame->payload[payload_length - 1U] == (uint8_t)(payload_length - 1U));
    }
}

static void test_crc32_reference(void) {
    static const uint8_t input[] = "123456789";
    struct fw_protocol_crc32_context context;

    assert(fw_protocol_crc32(NULL, 0U) == 0U);
    assert(fw_protocol_crc32(input, sizeof(input) - 1U) == UINT32_C(0xCBF43926));
    fw_protocol_crc32_init(&context);
    assert(fw_protocol_crc32_update(&context, input, 4U) == FW_PROTOCOL_OK);
    assert(fw_protocol_crc32_update(&context, &input[4], sizeof(input) - 5U) == FW_PROTOCOL_OK);
    assert(fw_protocol_crc32_finalize(&context) == UINT32_C(0xCBF43926));
    assert(fw_protocol_crc32_update(NULL, input, sizeof(input) - 1U) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(fw_protocol_crc32_update(&context, NULL, 1U) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    fw_protocol_crc32_init(NULL);
    assert(fw_protocol_crc32_finalize(NULL) == 0U);
}

static void test_all_golden_vectors(void) {
    uint8_t bytes[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t encoded[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t vector_index;

    for (vector_index = 0U; vector_index < sizeof(golden_frames) / sizeof(golden_frames[0]); ++vector_index) {
        struct fw_protocol_frame frame;
        size_t encoded_size = 0U;
        size_t frame_size = decode_hex(golden_frames[vector_index], bytes, sizeof(bytes));

        assert(fw_protocol_decode(bytes, frame_size, &frame) == FW_PROTOCOL_OK);
        assert(fw_protocol_encode(&frame, encoded, sizeof(encoded), &encoded_size) == FW_PROTOCOL_OK);
        assert(encoded_size == frame_size);
        assert(memcmp(encoded, bytes, frame_size) == 0);
    }
}

static void test_codec_boundaries(void) {
    static const uint16_t lengths[] = {0U, 1U, 511U, 512U};
    uint8_t encoded[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t index;

    for (index = 0U; index < sizeof(lengths) / sizeof(lengths[0]); ++index) {
        struct fw_protocol_frame decoded;
        size_t frame_size = make_frame(lengths[index], encoded);
        assert(frame_size == FW_PROTOCOL_HEADER_SIZE + lengths[index] + FW_PROTOCOL_CRC_SIZE);
        assert(fw_protocol_decode(encoded, frame_size, &decoded) == FW_PROTOCOL_OK);
        assert_frame_ready(&decoded, lengths[index]);
    }
}

static void test_codec_rejections(void) {
    uint8_t frame_bytes[FW_PROTOCOL_MAX_FRAME_SIZE + 1U];
    uint8_t payload = 0xA5U;
    struct fw_protocol_frame decoded;
    struct fw_protocol_frame frame = {
        .major = 1U,
        .minor = 0U,
        .command = FW_PROTOCOL_COMMAND_HELLO,
        .flags = 0U,
        .sequence = 0U,
        .payload = &payload,
        .payload_length = 1U,
    };
    size_t output_size = 99U;
    size_t frame_size;

    assert(fw_protocol_encode(NULL, frame_bytes, sizeof(frame_bytes), &output_size)
           == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(output_size == 0U);
    assert(fw_protocol_encode(&frame, NULL, 0U, &output_size) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    frame.payload = NULL;
    assert(fw_protocol_encode(&frame, frame_bytes, sizeof(frame_bytes), &output_size)
           == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    frame.payload = &payload;
    frame.flags = FW_PROTOCOL_FLAG_ERROR;
    assert(fw_protocol_encode(&frame, frame_bytes, sizeof(frame_bytes), &output_size) == FW_PROTOCOL_ERR_FORMAT);
    frame.flags = 0U;
    assert(fw_protocol_encode(&frame, frame_bytes, FW_PROTOCOL_HEADER_SIZE, &output_size)
           == FW_PROTOCOL_ERR_BUFFER_TOO_SMALL);

    frame_size = make_frame(0U, frame_bytes);
    assert(fw_protocol_decode(NULL, frame_size, &decoded) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(fw_protocol_decode(frame_bytes, frame_size, NULL) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(fw_protocol_decode(frame_bytes, frame_size - 1U, &decoded) == FW_PROTOCOL_NEED_MORE);
    frame_bytes[frame_size] = 0U;
    assert(fw_protocol_decode(frame_bytes, frame_size + 1U, &decoded) == FW_PROTOCOL_ERR_FORMAT);

    frame_size = make_frame(0U, frame_bytes);
    frame_bytes[0] ^= 1U;
    assert(fw_protocol_decode(frame_bytes, frame_size, &decoded) == FW_PROTOCOL_ERR_FORMAT);
    frame_size = make_frame(1U, frame_bytes);
    frame_bytes[8] ^= 1U;
    assert(fw_protocol_decode(frame_bytes, frame_size, &decoded) == FW_PROTOCOL_ERR_CRC);
    frame_size = make_frame(1U, frame_bytes);
    frame_bytes[FW_PROTOCOL_HEADER_SIZE] ^= 1U;
    assert(fw_protocol_decode(frame_bytes, frame_size, &decoded) == FW_PROTOCOL_ERR_CRC);
    frame_size = make_frame(1U, frame_bytes);
    frame_bytes[frame_size - 1U] ^= 1U;
    assert(fw_protocol_decode(frame_bytes, frame_size, &decoded) == FW_PROTOCOL_ERR_CRC);
    frame_size = make_frame(0U, frame_bytes);
    frame_bytes[14] = 1U;
    assert(fw_protocol_decode(frame_bytes, frame_size, &decoded) == FW_PROTOCOL_ERR_FORMAT);
    frame_size = make_frame(0U, frame_bytes);
    frame_bytes[7] = FW_PROTOCOL_FLAG_ERROR;
    assert(fw_protocol_decode(frame_bytes, frame_size, &decoded) == FW_PROTOCOL_ERR_FORMAT);
    (void)make_frame(0U, frame_bytes);
    frame_bytes[12] = 0x01U;
    frame_bytes[13] = 0x02U;
    assert(fw_protocol_decode(frame_bytes, FW_PROTOCOL_HEADER_SIZE, &decoded) == FW_PROTOCOL_ERR_FORMAT);
}

static void test_every_split(const uint8_t* bytes, size_t frame_size, uint16_t payload_length) {
    size_t split;

    for (split = 0U; split <= frame_size; ++split) {
        struct fw_protocol_parser parser;
        struct fw_protocol_frame frame;
        size_t consumed = 99U;
        enum fw_protocol_result result;

        fw_protocol_parser_init(&parser);
        result = fw_protocol_parser_feed(&parser, bytes, split, &consumed, &frame);
        assert(consumed == split);
        if (split == frame_size) {
            assert(result == FW_PROTOCOL_FRAME_READY);
        } else {
            assert(result == FW_PROTOCOL_NEED_MORE);
            result = fw_protocol_parser_feed(&parser, &bytes[split], frame_size - split, &consumed, &frame);
            assert(result == FW_PROTOCOL_FRAME_READY);
            assert(consumed == frame_size - split);
        }
        assert_frame_ready(&frame, payload_length);
    }
}

static void test_parser_splits_and_bytes(void) {
    uint8_t frame_bytes[FW_PROTOCOL_MAX_FRAME_SIZE];
    struct fw_protocol_parser parser;
    struct fw_protocol_frame frame = {0};
    size_t frame_size;
    size_t index;

    frame_size = make_frame(0U, frame_bytes);
    test_every_split(frame_bytes, frame_size, 0U);
    frame_size = make_frame(16U, frame_bytes);
    test_every_split(frame_bytes, frame_size, 16U);
    frame_size = make_frame(FW_PROTOCOL_MAX_PAYLOAD, frame_bytes);
    test_every_split(frame_bytes, frame_size, FW_PROTOCOL_MAX_PAYLOAD);

    fw_protocol_parser_init(&parser);
    for (index = 0U; index < frame_size; ++index) {
        size_t consumed = 0U;
        enum fw_protocol_result result = fw_protocol_parser_feed(&parser, &frame_bytes[index], 1U, &consumed, &frame);
        assert(consumed == 1U);
        assert(result == (index + 1U == frame_size ? FW_PROTOCOL_FRAME_READY : FW_PROTOCOL_NEED_MORE));
    }
    assert_frame_ready(&frame, FW_PROTOCOL_MAX_PAYLOAD);
}

static void test_parser_coalesced_and_resync(void) {
    uint8_t frame_bytes[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t stream[FW_PROTOCOL_MAX_FRAME_SIZE * 2U + 16U];
    struct fw_protocol_parser parser;
    struct fw_protocol_frame frame;
    size_t frame_size = make_frame(16U, frame_bytes);
    size_t consumed = 0U;
    enum fw_protocol_result result;

    memcpy(stream, frame_bytes, frame_size);
    memcpy(&stream[frame_size], frame_bytes, frame_size);
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, frame_size * 2U, &consumed, &frame);
    assert(result == FW_PROTOCOL_FRAME_READY && consumed == frame_size);
    result = fw_protocol_parser_feed(&parser, &stream[consumed], frame_size * 2U - consumed, &consumed, &frame);
    assert(result == FW_PROTOCOL_FRAME_READY && consumed == frame_size);

    stream[0] = 'F';
    stream[1] = 'W';
    stream[2] = 'F';
    memcpy(&stream[3], frame_bytes, frame_size);
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, frame_size + 3U, &consumed, &frame);
    assert(result == FW_PROTOCOL_FRAME_READY && consumed == frame_size + 3U);

    memcpy(stream, frame_bytes, frame_size);
    stream[frame_size - 1U] ^= 1U;
    memcpy(&stream[frame_size], frame_bytes, frame_size);
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, frame_size * 2U, &consumed, &frame);
    assert(result == FW_PROTOCOL_ERR_CRC && consumed == frame_size);
    result = fw_protocol_parser_feed(&parser, &stream[consumed], frame_size * 2U - consumed, &consumed, &frame);
    assert(result == FW_PROTOCOL_FRAME_READY && consumed == frame_size);
}

static void test_parser_rejects_header_before_payload(void) {
    uint8_t valid[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t stream[FW_PROTOCOL_HEADER_SIZE + FW_PROTOCOL_MAX_FRAME_SIZE];
    struct fw_protocol_parser parser;
    struct fw_protocol_frame frame;
    size_t valid_size = make_frame(0U, valid);
    size_t consumed = 0U;
    enum fw_protocol_result result;

    memcpy(stream, valid, FW_PROTOCOL_HEADER_SIZE);
    stream[12] = 0x01U;
    stream[13] = 0x02U;
    memcpy(&stream[FW_PROTOCOL_HEADER_SIZE], valid, valid_size);

    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, FW_PROTOCOL_HEADER_SIZE + valid_size, &consumed, &frame);
    assert(result == FW_PROTOCOL_ERR_FORMAT && consumed == FW_PROTOCOL_HEADER_SIZE);
    result = fw_protocol_parser_feed(&parser, &stream[consumed], FW_PROTOCOL_HEADER_SIZE + valid_size - consumed,
                                     &consumed, &frame);
    assert(result == FW_PROTOCOL_FRAME_READY && consumed == valid_size);

    memcpy(stream, valid, FW_PROTOCOL_HEADER_SIZE);
    stream[12] = 0xFFU;
    stream[13] = 0xFFU;
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, FW_PROTOCOL_HEADER_SIZE, &consumed, &frame);
    assert(result == FW_PROTOCOL_ERR_FORMAT && consumed == FW_PROTOCOL_HEADER_SIZE);

    memcpy(stream, valid, FW_PROTOCOL_HEADER_SIZE);
    stream[14] = 1U;
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, FW_PROTOCOL_HEADER_SIZE, &consumed, &frame);
    assert(result == FW_PROTOCOL_ERR_FORMAT && consumed == FW_PROTOCOL_HEADER_SIZE);

    memcpy(stream, valid, FW_PROTOCOL_HEADER_SIZE);
    stream[7] = FW_PROTOCOL_FLAG_ERROR;
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, FW_PROTOCOL_HEADER_SIZE, &consumed, &frame);
    assert(result == FW_PROTOCOL_ERR_FORMAT && consumed == FW_PROTOCOL_HEADER_SIZE);

    memcpy(stream, valid, 13U);
    stream[12] = 0x01U;
    memcpy(&stream[13], valid, valid_size);
    fw_protocol_parser_init(&parser);
    result = fw_protocol_parser_feed(&parser, stream, 13U + valid_size, &consumed, &frame);
    assert(result == FW_PROTOCOL_ERR_FORMAT && consumed == FW_PROTOCOL_HEADER_SIZE);
    result = fw_protocol_parser_feed(&parser, &stream[consumed], 13U + valid_size - consumed, &consumed, &frame);
    assert(result == FW_PROTOCOL_FRAME_READY && consumed == valid_size - 3U);
}

static void test_parser_arguments(void) {
    struct fw_protocol_parser parser;
    struct fw_protocol_frame frame;
    size_t consumed = 1U;

    fw_protocol_parser_init(&parser);
    assert(fw_protocol_parser_feed(&parser, NULL, 0U, &consumed, &frame) == FW_PROTOCOL_NEED_MORE);
    assert(consumed == 0U);
    assert(fw_protocol_parser_feed(NULL, NULL, 0U, &consumed, &frame) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(fw_protocol_parser_feed(&parser, NULL, 1U, &consumed, &frame) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(fw_protocol_parser_feed(&parser, NULL, 0U, NULL, &frame) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    assert(fw_protocol_parser_feed(&parser, NULL, 0U, &consumed, NULL) == FW_PROTOCOL_ERR_INVALID_ARGUMENT);
    fw_protocol_parser_reset(NULL);
}

static uint32_t corpus_random(uint32_t* state) {
    uint32_t value = *state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    *state = value;
    return value;
}

static void test_fixed_seed_malformed_corpus(void) {
    enum { CORPUS_CASES = 10000, CORPUS_MAX_INPUT = 1024 };
    uint8_t input[CORPUS_MAX_INPUT];
    uint8_t frame_bytes[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint32_t random_state = UINT32_C(0x5EED3001);
    size_t frame_count = 0U;
    size_t format_count = 0U;
    size_t crc_count = 0U;
    size_t case_index;

    for (case_index = 0U; case_index < CORPUS_CASES; ++case_index) {
        struct fw_protocol_parser parser;
        struct fw_protocol_frame frame;
        size_t input_length = case_index % (CORPUS_MAX_INPUT + 1U);
        size_t offset = 0U;
        size_t index;

        for (index = 0U; index < input_length; ++index)
            input[index] = (uint8_t)corpus_random(&random_state);

        if (input_length >= FW_PROTOCOL_HEADER_SIZE + FW_PROTOCOL_CRC_SIZE && case_index % 5U != 0U) {
            size_t maximum_payload = input_length - FW_PROTOCOL_HEADER_SIZE - FW_PROTOCOL_CRC_SIZE;
            uint16_t payload_length =
                (uint16_t)(maximum_payload < FW_PROTOCOL_MAX_PAYLOAD ? maximum_payload : FW_PROTOCOL_MAX_PAYLOAD);
            size_t frame_size = make_frame(payload_length, frame_bytes);
            memcpy(input, frame_bytes, frame_size);
            if (case_index % 5U == 2U)
                input[frame_size - 1U] ^= 1U;
            if (case_index % 5U == 3U) {
                input[12] = 0x01U;
                input[13] = 0x02U;
            }
        }

        fw_protocol_parser_init(&parser);
        while (offset < input_length) {
            size_t remaining = input_length - offset;
            size_t limit = remaining < 73U ? remaining : 73U;
            size_t chunk = 1U + (size_t)(corpus_random(&random_state) % (uint32_t)limit);
            size_t consumed = 0U;
            enum fw_protocol_result result = fw_protocol_parser_feed(&parser, &input[offset], chunk, &consumed, &frame);

            assert(consumed != 0U && consumed <= chunk);
            offset += consumed;
            if (result == FW_PROTOCOL_FRAME_READY) {
                ++frame_count;
            } else if (result == FW_PROTOCOL_ERR_FORMAT) {
                ++format_count;
            } else if (result == FW_PROTOCOL_ERR_CRC) {
                ++crc_count;
            } else {
                assert(result == FW_PROTOCOL_NEED_MORE);
            }
        }
    }

    assert(frame_count != 0U && format_count != 0U && crc_count != 0U);
    printf("Malformed corpus: cases=%u seed=0x%08X frames=%zu format=%zu crc=%zu\n", (unsigned int)CORPUS_CASES,
           UINT32_C(0x5EED3001), frame_count, format_count, crc_count);
}

int main(void) {
    test_crc32_reference();
    test_all_golden_vectors();
    test_codec_boundaries();
    test_codec_rejections();
    test_parser_splits_and_bytes();
    test_parser_coalesced_and_resync();
    test_parser_rejects_header_before_payload();
    test_parser_arguments();
    test_fixed_seed_malformed_corpus();
    return 0;
}
