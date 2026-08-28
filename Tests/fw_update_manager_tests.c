#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fw_update/manager.h"

#define FAKE_CAPACITY 2048U

_Static_assert(sizeof(struct fw_update_manager) <= 2048U, "Manager exceeds the Phase 3 memory limit");

struct fake_storage {
    struct fw_update_storage_info info;
    uint8_t bytes[FAKE_CAPACITY];
    unsigned int get_info_calls;
    unsigned int erase_calls;
    unsigned int write_calls;
    unsigned int read_calls;
    unsigned int fail_get_info_call;
    unsigned int fail_erase_call;
    unsigned int fail_write_call;
    unsigned int fail_read_call;
    int corrupt_read;
    int bad_alignment;
};

struct fake_boot_control {
    unsigned int request_calls;
    unsigned int confirm_calls;
    enum fw_update_result request_result;
};

struct fake_product_config {
    struct fw_update_product_config_state state;
    unsigned int get_calls;
    unsigned int set_calls;
    enum fw_update_result get_result;
    enum fw_update_result set_result;
};

struct fixture {
    struct fake_storage fake_storage;
    struct fw_update_storage storage;
    struct fake_boot_control fake_boot;
    struct fw_update_boot_control boot_control;
    struct fake_product_config fake_product;
    struct fw_update_product_config product_config;
    struct fw_update_manager manager;
    uint32_t now_ms;
    uint32_t next_sequence;
};

struct response {
    enum fw_protocol_status status;
    uint8_t flags;
    uint8_t command;
    uint32_t sequence;
    uint8_t body[38];
    uint16_t body_length;
};

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

static enum fw_update_result fake_get_info(void* context, struct fw_update_storage_info* info) {
    struct fake_storage* fake = context;
    ++fake->get_info_calls;
    if (fake->fail_get_info_call == fake->get_info_calls)
        return FW_UPDATE_ERR_IO;
    *info = fake->info;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_erase_all(void* context) {
    struct fake_storage* fake = context;
    ++fake->erase_calls;
    if (fake->fail_erase_call == fake->erase_calls)
        return FW_UPDATE_ERR_IO;
    memset(fake->bytes, fake->info.erased_value, fake->info.capacity);
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_write(void* context, uint32_t offset, const void* data, uint32_t length) {
    struct fake_storage* fake = context;
    ++fake->write_calls;
    if (fake->fail_write_call == fake->write_calls)
        return FW_UPDATE_ERR_IO;
    assert(offset <= fake->info.capacity && length <= fake->info.capacity - offset);
    if ((offset % fake->info.write_alignment) != 0U || (length % fake->info.write_alignment) != 0U)
        fake->bad_alignment = 1;
    memcpy(&fake->bytes[offset], data, length);
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_read(void* context, uint32_t offset, void* data, uint32_t length) {
    struct fake_storage* fake = context;
    ++fake->read_calls;
    if (fake->fail_read_call == fake->read_calls)
        return FW_UPDATE_ERR_IO;
    assert(offset <= fake->info.capacity && length <= fake->info.capacity - offset);
    memcpy(data, &fake->bytes[offset], length);
    if (fake->corrupt_read != 0 && length != 0U)
        ((uint8_t*)data)[0] ^= 1U;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_request(void* context) {
    struct fake_boot_control* fake = context;
    ++fake->request_calls;
    return fake->request_result;
}

static enum fw_update_result fake_confirm(void* context) {
    struct fake_boot_control* fake = context;
    ++fake->confirm_calls;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_product_get(void* context, struct fw_update_product_config_state* state) {
    struct fake_product_config* fake = context;
    ++fake->get_calls;
    if (fake->get_result != FW_UPDATE_OK)
        return fake->get_result;
    *state = fake->state;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_product_set(void* context, const struct fw_update_product_identity* identity) {
    struct fake_product_config* fake = context;
    ++fake->set_calls;
    if (fake->set_result != FW_UPDATE_OK)
        return fake->set_result;
    if (fake->state.provisioned != 0U)
        return FW_UPDATE_ERR_LOCKED;
    fake->state.identity = *identity;
    fake->state.provisioned = 1U;
    return FW_UPDATE_OK;
}

static const struct fw_update_storage_ops storage_ops = {
    .get_info = fake_get_info,
    .erase_all = fake_erase_all,
    .write = fake_write,
    .read = fake_read,
};

static const struct fw_update_boot_control_ops boot_control_ops = {
    .request_test_upgrade = fake_request,
    .confirm_running_image = fake_confirm,
};

static const struct fw_update_product_config_ops product_config_ops = {
    .get = fake_product_get,
    .set = fake_product_set,
};

static struct fw_update_manager_config make_config(const struct fixture* fixture) {
    const struct fw_update_manager_config config = {
        .storage = &fixture->storage,
        .boot_control = &fixture->boot_control,
        .product_config = &fixture->product_config,
        .product_config_writable = 1U,
        .application_version = {.major = 2U, .minor = 1U, .revision = 3U, .build = 4U},
        .bootloader_version = {.major = 1U, .minor = 0U, .revision = 0U, .build = 0U},
        .session_timeout_ms = 5000U,
    };
    return config;
}

static void fixture_init(struct fixture* fixture, uint32_t alignment) {
    struct fw_update_manager_config config;

    memset(fixture, 0, sizeof(*fixture));
    fixture->fake_storage.info.capacity = FAKE_CAPACITY;
    fixture->fake_storage.info.write_alignment = alignment;
    fixture->fake_storage.info.erase_alignment = 256U;
    fixture->fake_storage.info.erased_value = 0xFFU;
    fixture->storage.ops = &storage_ops;
    fixture->storage.context = &fixture->fake_storage;
    fixture->boot_control.ops = &boot_control_ops;
    fixture->boot_control.context = &fixture->fake_boot;
    fixture->fake_product.state.identity.hardware_id = UINT32_C(0x00004600);
    fixture->fake_product.state.identity.board_id = 1U;
    fixture->fake_product.state.identity.board_revision = 2U;
    fixture->fake_product.get_result = FW_UPDATE_OK;
    fixture->fake_product.set_result = FW_UPDATE_OK;
    fixture->product_config.ops = &product_config_ops;
    fixture->product_config.context = &fixture->fake_product;
    fixture->fake_boot.request_result = FW_UPDATE_OK;
    fixture->now_ms = 100U;
    config = make_config(fixture);
    assert(fw_update_manager_init(&fixture->manager, &config) == FW_UPDATE_MANAGER_OK);
}

static size_t encode_request(uint8_t command, uint32_t sequence, const uint8_t* payload, uint16_t payload_length,
                             uint8_t* output) {
    const struct fw_protocol_frame frame = {
        .major = FW_PROTOCOL_VERSION_MAJOR,
        .minor = FW_PROTOCOL_VERSION_MINOR,
        .command = command,
        .flags = 0U,
        .sequence = sequence,
        .payload = payload,
        .payload_length = payload_length,
    };
    size_t output_size = 0U;
    assert(fw_protocol_encode(&frame, output, FW_PROTOCOL_MAX_FRAME_SIZE, &output_size) == FW_PROTOCOL_OK);
    return output_size;
}

static void read_response(struct fw_update_manager* manager, struct response* response) {
    struct fw_protocol_frame decoded;
    const uint8_t* tx;
    size_t tx_size = 0U;

    assert(fw_update_manager_tx_view(manager, &tx, &tx_size) == FW_UPDATE_MANAGER_OK);
    assert(tx != NULL && tx_size >= FW_PROTOCOL_HEADER_SIZE + FW_PROTOCOL_CRC_SIZE + 2U);
    assert(fw_protocol_decode(tx, tx_size, &decoded) == FW_PROTOCOL_OK);
    assert(decoded.payload_length >= 2U);

    response->status = (enum fw_protocol_status)read_u16_le(decoded.payload);
    response->flags = decoded.flags;
    response->command = decoded.command;
    response->sequence = decoded.sequence;
    response->body_length = (uint16_t)(decoded.payload_length - 2U);
    assert(response->body_length <= sizeof(response->body));
    if (response->status == FW_PROTOCOL_STATUS_OK) {
        assert(response->flags == FW_PROTOCOL_FLAG_RESPONSE);
    } else {
        assert(response->flags == (FW_PROTOCOL_FLAG_RESPONSE | FW_PROTOCOL_FLAG_ERROR));
        assert(response->body_length == 0U);
    }
    if (response->body_length != 0U)
        memcpy(response->body, &decoded.payload[2], response->body_length);
    assert(fw_update_manager_consume_tx(manager, tx_size) == FW_UPDATE_MANAGER_OK);
}

static void queue_encoded_request(struct fixture* fixture, const uint8_t* request, size_t request_size) {
    size_t consumed = 0U;
    assert(fw_update_manager_feed(&fixture->manager, request, request_size, fixture->now_ms++, &consumed)
           == FW_UPDATE_MANAGER_RESPONSE_READY);
    assert(consumed == request_size);
}

static void send_encoded_request(struct fixture* fixture, const uint8_t* request, size_t request_size,
                                 struct response* response) {
    queue_encoded_request(fixture, request, request_size);
    read_response(&fixture->manager, response);
}

static size_t capture_response(struct fw_update_manager* manager, uint8_t* output, struct response* response) {
    const uint8_t* tx;
    size_t tx_size = 0U;
    assert(fw_update_manager_tx_view(manager, &tx, &tx_size) == FW_UPDATE_MANAGER_OK);
    assert(tx != NULL && tx_size <= FW_PROTOCOL_MAX_FRAME_SIZE);
    memcpy(output, tx, tx_size);
    read_response(manager, response);
    return tx_size;
}

static void send_request(struct fixture* fixture, uint8_t command, uint32_t sequence, const uint8_t* payload,
                         uint16_t payload_length, struct response* response) {
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t request_size = encode_request(command, sequence, payload, payload_length, request);
    send_encoded_request(fixture, request, request_size, response);
    assert(response->command == command);
    assert(response->sequence == sequence);
}

static void send_expected_request(struct fixture* fixture, uint8_t command, const uint8_t* payload,
                                  uint16_t payload_length, struct response* response) {
    uint32_t sequence = fixture->next_sequence;
    send_request(fixture, command, sequence, payload, payload_length, response);
    if (response->status != FW_PROTOCOL_STATUS_BAD_FRAME && response->status != FW_PROTOCOL_STATUS_INCOMPATIBLE_VERSION
        && response->status != FW_PROTOCOL_STATUS_BAD_SEQUENCE)
        ++fixture->next_sequence;
}

static void send_hello(struct fixture* fixture) {
    struct response response;
    send_request(fixture, FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    assert(response.flags == FW_PROTOCOL_FLAG_RESPONSE);
    assert(response.body_length == 10U);
    assert(read_u16_le(&response.body[0]) == FW_PROTOCOL_MAX_PAYLOAD);
    assert(read_u32_le(&response.body[2])
           == (FW_PROTOCOL_CAPABILITY_TEST_UPGRADE | FW_PROTOCOL_CAPABILITY_READBACK_CRC
               | FW_PROTOCOL_CAPABILITY_STRICT_DATA | FW_PROTOCOL_CAPABILITY_PRODUCT_CONFIG));
    assert(read_u32_le(&response.body[6]) == 5000U);
    fixture->next_sequence = 1U;
}

static void make_begin_payload(const struct fixture* fixture, uint32_t image_size, uint32_t image_crc,
                               uint8_t* payload) {
    write_u32_le(&payload[0], image_size);
    write_u32_le(&payload[4], image_crc);
    write_u32_le(&payload[8], fixture->fake_product.state.identity.hardware_id);
    write_u32_le(&payload[12], fixture->fake_product.state.identity.board_id);
    write_u16_le(&payload[16], fixture->fake_product.state.identity.board_revision);
    write_u16_le(&payload[18], 0U);
    payload[20] = 2U;
    payload[21] = 1U;
    write_u16_le(&payload[22], 1U);
    write_u32_le(&payload[24], 0U);
}

static enum fw_protocol_status send_begin(struct fixture* fixture, uint32_t image_size, uint32_t image_crc) {
    uint8_t payload[28];
    struct response response;
    make_begin_payload(fixture, image_size, image_crc, payload);
    send_expected_request(fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    return response.status;
}

static enum fw_protocol_status send_data(struct fixture* fixture, uint32_t offset, const uint8_t* data,
                                         uint16_t data_length, uint32_t* next_offset) {
    uint8_t payload[FW_PROTOCOL_MAX_PAYLOAD];
    struct response response;
    assert(data_length <= FW_PROTOCOL_MAX_PAYLOAD - 4U);
    write_u32_le(payload, offset);
    if (data_length != 0U)
        memcpy(&payload[4], data, data_length);
    send_expected_request(fixture, FW_PROTOCOL_COMMAND_DATA, payload, (uint16_t)(data_length + 4U), &response);
    if (response.status == FW_PROTOCOL_STATUS_OK) {
        assert(response.body_length == 4U);
        *next_offset = read_u32_le(response.body);
    }
    return response.status;
}

static enum fw_protocol_status send_end(struct fixture* fixture) {
    struct response response;
    send_expected_request(fixture, FW_PROTOCOL_COMMAND_END, NULL, 0U, &response);
    return response.status;
}

static void fill_image(uint8_t* image, uint32_t size) {
    uint32_t index;
    for (index = 0U; index < size; ++index)
        image[index] = (uint8_t)(index * 37U + 11U);
}

static void transfer_image(struct fixture* fixture, const uint8_t* image, uint32_t image_size) {
    uint32_t offset = 0U;
    while (offset < image_size) {
        uint32_t remaining = image_size - offset;
        uint16_t chunk = (uint16_t)(remaining < 37U ? remaining : 37U);
        uint32_t next_offset = 0U;
        assert(send_data(fixture, offset, &image[offset], chunk, &next_offset) == FW_PROTOCOL_STATUS_OK);
        assert(next_offset == offset + chunk);
        offset = next_offset;
    }
}

static void test_complete_lifecycle(void) {
    struct fixture fixture;
    uint8_t image[13];
    struct response response;
    uint32_t crc;

    fixture_init(&fixture, 4U);
    fill_image(image, sizeof(image));
    crc = fw_protocol_crc32(image, sizeof(image));
    send_hello(&fixture);

    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_DEVICE_INFO, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && response.body_length == 38U);
    assert(read_u16_le(&response.body[0]) == 2U);
    assert(read_u32_le(&response.body[2]) == UINT32_C(0x00004600));
    assert(read_u32_le(&response.body[10]) == FAKE_CAPACITY);
    assert(read_u32_le(&response.body[14]) == 4U);
    assert(read_u32_le(&response.body[18]) == 256U);
    assert(response.body[22] == 2U && response.body[23] == 1U);
    assert(read_u16_le(&response.body[24]) == 3U && read_u32_le(&response.body[26]) == 4U);
    assert(response.body[30] == 1U && response.body[31] == 0U);

    assert(send_begin(&fixture, sizeof(image), crc) == FW_PROTOCOL_STATUS_OK);
    assert(fixture.fake_storage.erase_calls == 1U);
    transfer_image(&fixture, image, sizeof(image));
    assert(memcmp(fixture.fake_storage.bytes, image, sizeof(image)) == 0);
    assert(fixture.fake_storage.bytes[13] == 0xFFU);
    assert(fixture.fake_storage.bytes[14] == 0xFFU);
    assert(fixture.fake_storage.bytes[15] == 0xFFU);
    assert(send_end(&fixture) == FW_PROTOCOL_STATUS_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_READY_TO_COMMIT);

    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_COMMIT, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    assert(fixture.fake_boot.request_calls == 1U);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_COMPLETED);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_RESET);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);
    assert(fixture.fake_storage.erase_calls == 1U);
}

static void test_storage_alignments(void) {
    static const uint32_t alignments[] = {1U, 4U, 8U, 16U, 24U, 256U, 512U};
    uint8_t image[1023];
    size_t alignment_index;

    fill_image(image, sizeof(image));
    for (alignment_index = 0U; alignment_index < sizeof(alignments) / sizeof(alignments[0]); ++alignment_index) {
        uint32_t alignment = alignments[alignment_index];
        const uint32_t sizes[] = {alignment, alignment + 1U, alignment * 2U - 1U};
        size_t size_index;

        for (size_index = 0U; size_index < sizeof(sizes) / sizeof(sizes[0]); ++size_index) {
            struct fixture fixture;
            uint32_t image_size = sizes[size_index];
            uint32_t remainder = image_size % alignment;
            uint32_t padded_size = image_size + (remainder == 0U ? 0U : alignment - remainder);
            uint32_t offset;

            if (size_index == 2U && image_size == sizes[0])
                continue;
            fixture_init(&fixture, alignment);
            send_hello(&fixture);
            assert(send_begin(&fixture, image_size, fw_protocol_crc32(image, image_size)) == FW_PROTOCOL_STATUS_OK);
            transfer_image(&fixture, image, image_size);
            assert(send_end(&fixture) == FW_PROTOCOL_STATUS_OK);
            assert(fixture.fake_storage.bad_alignment == 0);
            assert(memcmp(fixture.fake_storage.bytes, image, image_size) == 0);
            for (offset = image_size; offset < padded_size; ++offset)
                assert(fixture.fake_storage.bytes[offset] == 0xFFU);
            assert(fixture.fake_boot.request_calls == 0U);
        }
    }
}

static void test_metadata_and_range_rejections(void) {
    struct fixture fixture;
    uint8_t payload[28];
    uint8_t data[9] = {0U};
    struct response response;
    uint32_t next_offset = 0U;

    fixture_init(&fixture, 4U);
    make_begin_payload(&fixture, 8U, 0U, payload);
    send_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, 1U, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_STATE && fixture.fake_storage.erase_calls == 0U);
    send_hello(&fixture);

    assert(send_begin(&fixture, 0U, 0U) == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);
    assert(send_begin(&fixture, FAKE_CAPACITY + 1U, 0U) == FW_PROTOCOL_STATUS_IMAGE_TOO_LARGE);
    assert(send_begin(&fixture, UINT32_MAX, 0U) == FW_PROTOCOL_STATUS_IMAGE_TOO_LARGE);
    make_begin_payload(&fixture, 8U, 0U, payload);
    payload[8] ^= 1U;
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);
    make_begin_payload(&fixture, 8U, 0U, payload);
    payload[12] ^= 1U;
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);
    make_begin_payload(&fixture, 8U, 0U, payload);
    payload[16] ^= 1U;
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);
    make_begin_payload(&fixture, 8U, 0U, payload);
    payload[18] = 1U;
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_FRAME);
    assert(fixture.fake_storage.erase_calls == 0U);

    assert(send_begin(&fixture, 8U, fw_protocol_crc32(data, 8U)) == FW_PROTOCOL_STATUS_OK);
    assert(send_data(&fixture, 1U, data, 1U, &next_offset) == FW_PROTOCOL_STATUS_OFFSET_MISMATCH);
    assert(send_data(&fixture, 0U, data, 0U, &next_offset) == FW_PROTOCOL_STATUS_BAD_FRAME);
    assert(send_data(&fixture, 0U, data, 9U, &next_offset) == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);
    assert(send_data(&fixture, 0U, data, 4U, &next_offset) == FW_PROTOCOL_STATUS_OK);
    assert(send_data(&fixture, 0U, data, 1U, &next_offset) == FW_PROTOCOL_STATUS_OFFSET_MISMATCH);
    assert(send_end(&fixture) == FW_PROTOCOL_STATUS_VERIFY_ERROR);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_RECEIVING);
    assert(send_data(&fixture, 4U, &data[4], 4U, &next_offset) == FW_PROTOCOL_STATUS_OK);
    assert(send_end(&fixture) == FW_PROTOCOL_STATUS_OK);
}

static void test_alignment_above_buffer(void) {
    struct fixture fixture;
    fixture_init(&fixture, FW_PROTOCOL_MAX_PAYLOAD + 1U);
    send_hello(&fixture);
    assert(send_begin(&fixture, 1U, 0U) == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);
    assert(fixture.fake_storage.erase_calls == 0U);
}

static void test_storage_failures(void) {
    uint8_t image[600];
    uint32_t next_offset = 0U;
    struct response response;

    fill_image(image, sizeof(image));

    {
        struct fixture fixture;
        fixture_init(&fixture, 4U);
        send_hello(&fixture);
        fixture.fake_storage.fail_erase_call = 1U;
        assert(send_begin(&fixture, 4U, fw_protocol_crc32(image, 4U)) == FW_PROTOCOL_STATUS_STORAGE_ERROR);
        assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_ERROR);
        send_expected_request(&fixture, FW_PROTOCOL_COMMAND_ABORT, NULL, 0U, &response);
        assert(response.status == FW_PROTOCOL_STATUS_OK);
    }

    {
        struct fixture fixture;
        fixture_init(&fixture, 4U);
        send_hello(&fixture);
        assert(send_begin(&fixture, 12U, fw_protocol_crc32(image, 12U)) == FW_PROTOCOL_STATUS_OK);
        fixture.fake_storage.fail_write_call = 2U;
        assert(send_data(&fixture, 0U, image, 4U, &next_offset) == FW_PROTOCOL_STATUS_OK);
        assert(send_data(&fixture, 4U, &image[4], 4U, &next_offset) == FW_PROTOCOL_STATUS_STORAGE_ERROR);
        assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_ERROR);
    }

    {
        struct fixture fixture;
        fixture_init(&fixture, 4U);
        send_hello(&fixture);
        assert(send_begin(&fixture, sizeof(image), fw_protocol_crc32(image, sizeof(image))) == FW_PROTOCOL_STATUS_OK);
        transfer_image(&fixture, image, sizeof(image));
        fixture.fake_storage.fail_read_call = 2U;
        assert(send_end(&fixture) == FW_PROTOCOL_STATUS_STORAGE_ERROR);
        assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_ERROR);
    }

    {
        struct fixture fixture;
        fixture_init(&fixture, 4U);
        send_hello(&fixture);
        assert(send_begin(&fixture, 8U, fw_protocol_crc32(image, 8U)) == FW_PROTOCOL_STATUS_OK);
        transfer_image(&fixture, image, 8U);
        fixture.fake_storage.corrupt_read = 1;
        assert(send_end(&fixture) == FW_PROTOCOL_STATUS_VERIFY_ERROR);
        assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_ERROR);
    }
}

static void test_protocol_error_and_tx_busy(void) {
    struct fixture fixture;
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    const uint8_t* tx;
    size_t request_size;
    size_t consumed = 0U;
    size_t tx_size = 0U;

    fixture_init(&fixture, 4U);
    request_size = encode_request(FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, request);
    request[request_size - 1U] ^= 1U;
    assert(fw_update_manager_feed(&fixture.manager, request, request_size, fixture.now_ms++, &consumed)
           == FW_UPDATE_MANAGER_ERR_PROTOCOL);
    assert(consumed == request_size);
    assert(fw_update_manager_tx_view(&fixture.manager, &tx, &tx_size) == FW_UPDATE_MANAGER_OK);
    assert(tx == NULL && tx_size == 0U);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);

    request_size = encode_request(FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, request);
    assert(fw_update_manager_feed(&fixture.manager, request, request_size, fixture.now_ms++, &consumed)
           == FW_UPDATE_MANAGER_RESPONSE_READY);
    assert(fw_update_manager_feed(&fixture.manager, request, request_size, fixture.now_ms++, &consumed)
           == FW_UPDATE_MANAGER_ERR_BUSY);
    assert(consumed == 0U);
    assert(fw_update_manager_tx_view(&fixture.manager, &tx, &tx_size) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_consume_tx(&fixture.manager, tx_size + 1U) == FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT);
    assert(fw_update_manager_consume_tx(&fixture.manager, tx_size) == FW_UPDATE_MANAGER_OK);
}

static void test_ingress_version_and_state_rules(void) {
    struct fixture fixture;
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    struct fw_protocol_frame frame = {
        .major = 2U,
        .minor = 0U,
        .command = FW_PROTOCOL_COMMAND_HELLO,
        .flags = 0U,
        .sequence = 0U,
        .payload = NULL,
        .payload_length = 0U,
    };
    struct response response;
    size_t request_size = 0U;
    size_t consumed = 0U;

    fixture_init(&fixture, 4U);
    assert(fw_protocol_encode(&frame, request, sizeof(request), &request_size) == FW_PROTOCOL_OK);
    send_encoded_request(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_INCOMPATIBLE_VERSION);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);

    frame.major = FW_PROTOCOL_VERSION_MAJOR;
    frame.flags = FW_PROTOCOL_FLAG_RESPONSE;
    assert(fw_protocol_encode(&frame, request, sizeof(request), &request_size) == FW_PROTOCOL_OK);
    send_encoded_request(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_FRAME);

    request_size = encode_request(FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, request);
    assert(fw_update_manager_feed(&fixture.manager, request, 7U, fixture.now_ms, &consumed)
           == FW_UPDATE_MANAGER_NEED_MORE);
    assert(consumed == 7U);
    assert(fw_update_manager_feed(&fixture.manager, &request[7], request_size - 7U, fixture.now_ms++, &consumed)
           == FW_UPDATE_MANAGER_RESPONSE_READY);
    assert(consumed == request_size - 7U);
    read_response(&fixture.manager, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);

    send_request(&fixture, FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    fixture.next_sequence = 1U;
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_ABORT, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    send_request(&fixture, FW_PROTOCOL_COMMAND_DEVICE_INFO, 2U, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_STATE);
    send_request(&fixture, FW_PROTOCOL_COMMAND_ABORT, 3U, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_STATE);
}

static void test_init_validation(void) {
    struct fixture fixture;
    struct fw_update_manager_config config;

    memset(&fixture, 0, sizeof(fixture));
    fixture.fake_storage.info.capacity = FAKE_CAPACITY;
    fixture.fake_storage.info.write_alignment = 4U;
    fixture.fake_storage.info.erase_alignment = 256U;
    fixture.fake_storage.info.erased_value = 0xFFU;
    fixture.storage.ops = &storage_ops;
    fixture.storage.context = &fixture.fake_storage;
    fixture.boot_control.ops = &boot_control_ops;
    fixture.boot_control.context = &fixture.fake_boot;
    fixture.fake_product.state.identity.hardware_id = UINT32_C(0x00004600);
    fixture.fake_product.state.identity.board_id = 1U;
    fixture.fake_product.state.identity.board_revision = 2U;
    fixture.product_config.ops = &product_config_ops;
    fixture.product_config.context = &fixture.fake_product;
    config = make_config(&fixture);

    assert(fw_update_manager_init(NULL, &config) == FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT);
    assert(fw_update_manager_init(&fixture.manager, NULL) == FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT);
    config.session_timeout_ms = 0U;
    assert(fw_update_manager_init(&fixture.manager, &config) == FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT);
    config = make_config(&fixture);
    config.boot_control = NULL;
    assert(fw_update_manager_init(&fixture.manager, &config) == FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT);
    config = make_config(&fixture);
    config.product_config = NULL;
    assert(fw_update_manager_init(&fixture.manager, &config) == FW_UPDATE_MANAGER_ERR_INVALID_ARGUMENT);
    config = make_config(&fixture);
    fixture.fake_product.get_result = FW_UPDATE_ERR_IO;
    assert(fw_update_manager_init(&fixture.manager, &config) == FW_UPDATE_MANAGER_ERR_STORAGE);
    fixture.fake_product.get_result = FW_UPDATE_OK;
    config = make_config(&fixture);
    fixture.fake_storage.fail_get_info_call = fixture.fake_storage.get_info_calls + 1U;
    assert(fw_update_manager_init(&fixture.manager, &config) == FW_UPDATE_MANAGER_ERR_STORAGE);
}

static void test_product_config_provisioning(void) {
    struct fixture fixture;
    struct response response;
    uint8_t payload[12] = {FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION, 0U};

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_GET, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && response.body_length == sizeof(payload));
    assert(response.body[0] == FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION && response.body[1] == 0U);
    assert(read_u16_le(&response.body[2]) == 2U);
    assert(read_u32_le(&response.body[4]) == UINT32_C(0x00004600));
    assert(read_u32_le(&response.body[8]) == 1U);

    write_u16_le(&payload[2], 4U);
    write_u32_le(&payload[4], UINT32_C(0x00004601));
    write_u32_le(&payload[8], 7U);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_SET, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && response.body[1] == 1U);
    assert(fixture.fake_product.set_calls == 1U);
    assert(read_u16_le(&response.body[2]) == 4U);
    assert(read_u32_le(&response.body[4]) == UINT32_C(0x00004601));
    assert(read_u32_le(&response.body[8]) == 7U);

    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_GET, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && response.body[1] == 1U);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_SET, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_STATE && fixture.fake_product.set_calls == 1U);

    fixture_init(&fixture, 4U);
    fixture.manager.config.product_config_writable = 0U;
    send_hello(&fixture);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_SET, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_STATE && fixture.fake_product.set_calls == 0U);
}

static void prepare_ready(struct fixture* fixture, uint8_t* image, uint32_t image_size) {
    fill_image(image, image_size);
    send_hello(fixture);
    assert(send_begin(fixture, image_size, fw_protocol_crc32(image, image_size)) == FW_PROTOCOL_STATUS_OK);
    transfer_image(fixture, image, image_size);
    assert(send_end(fixture) == FW_PROTOCOL_STATUS_OK);
    assert(fw_update_manager_get_state(&fixture->manager) == FW_UPDATE_MANAGER_STATE_READY_TO_COMMIT);
}

static void send_duplicate_encoded(struct fixture* fixture, const uint8_t* request, size_t request_size,
                                   struct response* response) {
    uint8_t first[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t second[FW_PROTOCOL_MAX_FRAME_SIZE];
    struct response replay;
    size_t first_size;
    size_t second_size;

    queue_encoded_request(fixture, request, request_size);
    first_size = capture_response(&fixture->manager, first, response);
    queue_encoded_request(fixture, request, request_size);
    second_size = capture_response(&fixture->manager, second, &replay);
    assert(first_size == second_size);
    assert(memcmp(first, second, first_size) == 0);
    assert(response->status == replay.status);
}

static void test_sequence_and_semantic_progression(void) {
    struct fixture fixture;
    struct response response;
    uint8_t payload[28];

    fixture_init(&fixture, 4U);
    send_hello(&fixture);

    send_request(&fixture, FW_PROTOCOL_COMMAND_DEVICE_INFO, 2U, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_SEQUENCE);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_DEVICE_INFO, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);

    send_request(&fixture, FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_SEQUENCE);
    make_begin_payload(&fixture, 8U, 0U, payload);
    send_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, 1U, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_SEQUENCE);

    make_begin_payload(&fixture, 0U, 0U, payload);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_ARGUMENT);

    make_begin_payload(&fixture, 8U, 0U, payload);
    send_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, 2U, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_SEQUENCE);
    send_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, fixture.next_sequence, payload, 27U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_BAD_FRAME);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_BEGIN, payload, sizeof(payload), &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    assert(fixture.fake_storage.erase_calls == 1U);
}

static void test_exact_duplicate_side_effects(void) {
    struct fixture fixture;
    struct response response;
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    uint8_t payload[FW_PROTOCOL_MAX_PAYLOAD];
    uint8_t image[8];
    size_t request_size;

    fixture_init(&fixture, 4U);
    fill_image(image, sizeof(image));

    request_size = encode_request(FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);

    memset(payload, 0, 12U);
    payload[0] = FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION;
    write_u16_le(&payload[2], 4U);
    write_u32_le(&payload[4], UINT32_C(0x00004601));
    write_u32_le(&payload[8], 7U);
    request_size = encode_request(FW_PROTOCOL_COMMAND_PRODUCT_CONFIG_SET, 1U, payload, 12U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && fixture.fake_product.set_calls == 1U);

    fixture_init(&fixture, 4U);
    request_size = encode_request(FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);

    make_begin_payload(&fixture, sizeof(image), fw_protocol_crc32(image, sizeof(image)), payload);
    request_size = encode_request(FW_PROTOCOL_COMMAND_BEGIN, 1U, payload, 28U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && fixture.fake_storage.erase_calls == 1U);

    write_u32_le(payload, 0U);
    memcpy(&payload[4], image, sizeof(image));
    request_size = encode_request(FW_PROTOCOL_COMMAND_DATA, 2U, payload, 12U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && fixture.fake_storage.write_calls == 1U);

    request_size = encode_request(FW_PROTOCOL_COMMAND_END, 3U, NULL, 0U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && fixture.fake_storage.read_calls == 1U);

    request_size = encode_request(FW_PROTOCOL_COMMAND_COMMIT, 4U, NULL, 0U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && fixture.fake_boot.request_calls == 1U);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_RESET);

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    request_size = encode_request(FW_PROTOCOL_COMMAND_ABORT, 1U, NULL, 0U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);
    send_hello(&fixture);
}

static void test_protocol_error_budget(void) {
    struct fixture fixture;
    struct response response;
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    struct fw_protocol_frame bad_flags = {
        .major = FW_PROTOCOL_VERSION_MAJOR,
        .minor = FW_PROTOCOL_VERSION_MINOR,
        .command = FW_PROTOCOL_COMMAND_DEVICE_INFO,
        .flags = FW_PROTOCOL_FLAG_RESPONSE,
        .sequence = 1U,
        .payload = NULL,
        .payload_length = 0U,
    };
    size_t request_size;
    size_t consumed = 0U;
    unsigned int index;

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    for (index = 0U; index < 3U; ++index) {
        send_request(&fixture, FW_PROTOCOL_COMMAND_DEVICE_INFO, 99U, NULL, 0U, &response);
        assert(response.status == FW_PROTOCOL_STATUS_BAD_SEQUENCE);
    }
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    request_size = encode_request(FW_PROTOCOL_COMMAND_DEVICE_INFO, 1U, NULL, 0U, request);
    request[request_size - 1U] ^= 1U;
    for (index = 0U; index < 3U; ++index) {
        assert(fw_update_manager_feed(&fixture.manager, request, request_size, fixture.now_ms++, &consumed)
               == FW_UPDATE_MANAGER_ERR_PROTOCOL);
        assert(consumed == request_size);
    }
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    assert(fw_protocol_encode(&bad_flags, request, sizeof(request), &request_size) == FW_PROTOCOL_OK);
    for (index = 0U; index < 3U; ++index) {
        send_encoded_request(&fixture, request, request_size, &response);
        assert(response.status == FW_PROTOCOL_STATUS_BAD_FRAME);
    }
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);
}

static void test_timeout_and_tick_wrap(void) {
    struct fixture fixture;
    struct response response;
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t request_size;

    fixture_init(&fixture, 4U);
    fixture.now_ms = 100U;
    send_hello(&fixture);
    assert(fw_update_manager_poll(&fixture.manager, 5099U) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_NEGOTIATING);
    assert(fw_update_manager_poll(&fixture.manager, 5100U) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);
    assert(fw_update_manager_get_last_status(&fixture.manager) == FW_PROTOCOL_STATUS_TIMEOUT);

    fixture_init(&fixture, 4U);
    fixture.now_ms = UINT32_MAX - 1000U;
    send_hello(&fixture);
    assert(fw_update_manager_poll(&fixture.manager, (uint32_t)(UINT32_MAX - 1000U + 4999U)) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_NEGOTIATING);
    assert(fw_update_manager_poll(&fixture.manager, (uint32_t)(UINT32_MAX - 1000U + 5000U)) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);

    fixture_init(&fixture, 4U);
    request_size = encode_request(FW_PROTOCOL_COMMAND_HELLO, 0U, NULL, 0U, request);
    queue_encoded_request(&fixture, request, request_size);
    assert(fw_update_manager_poll(&fixture.manager, 10000U) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_NEGOTIATING);
    read_response(&fixture.manager, &response);
    assert(fw_update_manager_poll(&fixture.manager, 10000U) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);
}

static void test_disconnect_lifecycle(void) {
    struct fixture fixture;
    struct response response;
    uint8_t image[8];
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t request_size;
    const uint8_t* tx;
    size_t tx_size;

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    assert(fw_update_manager_notify_disconnect(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    fill_image(image, sizeof(image));
    assert(send_begin(&fixture, sizeof(image), fw_protocol_crc32(image, sizeof(image))) == FW_PROTOCOL_STATUS_OK);
    assert(fw_update_manager_notify_disconnect(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);
    assert(fixture.fake_boot.request_calls == 0U);

    fixture_init(&fixture, 4U);
    prepare_ready(&fixture, image, sizeof(image));
    assert(fw_update_manager_notify_disconnect(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_IDLE);
    assert(fixture.fake_boot.request_calls == 0U);

    fixture_init(&fixture, 4U);
    prepare_ready(&fixture, image, sizeof(image));
    request_size = encode_request(FW_PROTOCOL_COMMAND_COMMIT, fixture.next_sequence, NULL, 0U, request);
    queue_encoded_request(&fixture, request, request_size);
    assert(fixture.fake_boot.request_calls == 1U);
    assert(fw_update_manager_notify_disconnect(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_COMPLETED);
    assert(fw_update_manager_tx_view(&fixture.manager, &tx, &tx_size) == FW_UPDATE_MANAGER_OK);
    assert(tx == NULL && tx_size == 0U);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);

    queue_encoded_request(&fixture, request, request_size);
    read_response(&fixture.manager, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK && fixture.fake_boot.request_calls == 1U);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_RESET);
}

static void test_commit_tx_idle_ordering_and_failure(void) {
    struct fixture fixture;
    struct response response;
    uint8_t image[8];
    uint8_t request[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t request_size;
    const uint8_t* tx;
    size_t tx_size;

    fixture_init(&fixture, 4U);
    prepare_ready(&fixture, image, sizeof(image));
    request_size = encode_request(FW_PROTOCOL_COMMAND_COMMIT, fixture.next_sequence, NULL, 0U, request);
    queue_encoded_request(&fixture, request, request_size);
    assert(fw_update_manager_tx_view(&fixture.manager, &tx, &tx_size) == FW_UPDATE_MANAGER_OK);
    assert(tx_size > 1U);
    assert(fw_update_manager_consume_tx(&fixture.manager, tx_size / 2U) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);
    assert(fw_update_manager_consume_tx(&fixture.manager, tx_size - tx_size / 2U) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_RESET);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);

    fixture_init(&fixture, 4U);
    prepare_ready(&fixture, image, sizeof(image));
    request_size = encode_request(FW_PROTOCOL_COMMAND_COMMIT, fixture.next_sequence, NULL, 0U, request);
    queue_encoded_request(&fixture, request, request_size);
    read_response(&fixture.manager, &response);
    assert(response.status == FW_PROTOCOL_STATUS_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_RESET);
    assert(fw_update_manager_poll(&fixture.manager, UINT32_MAX) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_COMPLETED);

    fixture_init(&fixture, 4U);
    send_hello(&fixture);
    send_expected_request(&fixture, FW_PROTOCOL_COMMAND_COMMIT, NULL, 0U, &response);
    assert(response.status == FW_PROTOCOL_STATUS_INVALID_STATE && fixture.fake_boot.request_calls == 0U);

    fixture_init(&fixture, 4U);
    prepare_ready(&fixture, image, sizeof(image));
    fixture.fake_boot.request_result = FW_UPDATE_ERR_BOOT_CONTROL;
    request_size = encode_request(FW_PROTOCOL_COMMAND_COMMIT, fixture.next_sequence, NULL, 0U, request);
    send_duplicate_encoded(&fixture, request, request_size, &response);
    assert(response.status == FW_PROTOCOL_STATUS_BOOT_CONTROL_ERROR);
    assert(fixture.fake_boot.request_calls == 1U);
    assert(fw_update_manager_get_state(&fixture.manager) == FW_UPDATE_MANAGER_STATE_ERROR);
    assert(fw_update_manager_notify_tx_idle(&fixture.manager) == FW_UPDATE_MANAGER_OK);
    assert(fw_update_manager_take_action(&fixture.manager) == FW_UPDATE_MANAGER_ACTION_NONE);
}

int main(void) {
    test_complete_lifecycle();
    test_product_config_provisioning();
    test_storage_alignments();
    test_metadata_and_range_rejections();
    test_alignment_above_buffer();
    test_storage_failures();
    test_protocol_error_and_tx_busy();
    test_ingress_version_and_state_rules();
    test_init_validation();
    test_sequence_and_semantic_progression();
    test_exact_duplicate_side_effects();
    test_protocol_error_budget();
    test_timeout_and_tick_wrap();
    test_disconnect_lifecycle();
    test_commit_tx_idle_ordering_and_failure();
    return 0;
}
