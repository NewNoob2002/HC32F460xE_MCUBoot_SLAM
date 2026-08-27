#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fw_update/manager.h"

#define FAKE_CAPACITY 2048U

struct fake_storage {
    struct fw_update_storage_info info;
    uint8_t bytes[FAKE_CAPACITY];
    unsigned int erase_calls;
    unsigned int request_calls;
};

struct fixture {
    struct fake_storage fake;
    struct fw_update_storage storage;
    struct fw_update_boot_control boot_control;
    struct fw_update_manager manager;
    uint32_t now_ms;
};

static enum fw_update_result fake_get_info(void* context, struct fw_update_storage_info* info) {
    const struct fake_storage* fake = context;
    *info = fake->info;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_erase_all(void* context) {
    struct fake_storage* fake = context;
    ++fake->erase_calls;
    memset(fake->bytes, fake->info.erased_value, sizeof(fake->bytes));
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_write(void* context, uint32_t offset, const void* data, uint32_t length) {
    struct fake_storage* fake = context;
    if (offset > fake->info.capacity || length > fake->info.capacity - offset)
        return FW_UPDATE_ERR_BOUNDS;
    memcpy(&fake->bytes[offset], data, length);
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_read(void* context, uint32_t offset, void* data, uint32_t length) {
    const struct fake_storage* fake = context;
    if (offset > fake->info.capacity || length > fake->info.capacity - offset)
        return FW_UPDATE_ERR_BOUNDS;
    memcpy(data, &fake->bytes[offset], length);
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_request(void* context) {
    struct fake_storage* fake = context;
    ++fake->request_calls;
    return FW_UPDATE_OK;
}

static enum fw_update_result fake_confirm(void* context) {
    (void)context;
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

static int fixture_init(struct fixture* fixture) {
    const struct fw_update_manager_config config = {
        .storage = &fixture->storage,
        .boot_control = &fixture->boot_control,
        .hardware_id = UINT32_C(0x00004600),
        .board_id = 1U,
        .board_revision = 2U,
        .application_version = {.major = 1U, .minor = 0U, .revision = 0U, .build = 0U},
        .bootloader_version = {.major = 1U, .minor = 0U, .revision = 0U, .build = 0U},
        .session_timeout_ms = 5000U,
    };

    memset(fixture, 0, sizeof(*fixture));
    fixture->fake.info.capacity = FAKE_CAPACITY;
    fixture->fake.info.write_alignment = 4U;
    fixture->fake.info.erase_alignment = 256U;
    fixture->fake.info.erased_value = 0xFFU;
    fixture->storage.ops = &storage_ops;
    fixture->storage.context = &fixture->fake;
    fixture->boot_control.ops = &boot_control_ops;
    fixture->boot_control.context = &fixture->fake;
    fixture->now_ms = 100U;
    return fw_update_manager_init(&fixture->manager, &config) == FW_UPDATE_MANAGER_OK ? 0 : -1;
}

static int read_input(uint8_t* buffer, size_t capacity, size_t* count) {
#ifdef _WIN32
    int result = _read(0, buffer, (unsigned int)capacity);
#else
    ssize_t result = read(STDIN_FILENO, buffer, capacity);
#endif
    if (result < 0)
        return -1;
    *count = (size_t)result;
    return 0;
}

static int write_output(const uint8_t* data, size_t length) {
    size_t offset = 0U;
    while (offset < length) {
#ifdef _WIN32
        int result = _write(1, &data[offset], (unsigned int)(length - offset));
#else
        ssize_t result = write(STDOUT_FILENO, &data[offset], length - offset);
#endif
        if (result <= 0)
            return -1;
        offset += (size_t)result;
    }
    return 0;
}

static int read_expected(const char* path, uint8_t* output, size_t capacity, size_t* length) {
    FILE* file = fopen(path, "rb");
    if (file == NULL)
        return -1;
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size < 0L || (unsigned long)size > capacity || fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    *length = (size_t)size;
    int result = fread(output, 1U, *length, file) == *length ? 0 : -1;
    fclose(file);
    return result;
}

static int validate_result(const struct fixture* fixture, const uint8_t* expected, size_t expected_length) {
    size_t padded_length = (expected_length + 3U) & ~(size_t)3U;
    if (fixture->fake.erase_calls != 1U || fixture->fake.request_calls != 1U
        || memcmp(fixture->fake.bytes, expected, expected_length) != 0)
        return -1;
    while (expected_length < padded_length) {
        if (fixture->fake.bytes[expected_length++] != 0xFFU)
            return -1;
    }
    return 0;
}

int main(int argc, char** argv) {
    struct fixture fixture;
    uint8_t expected[FAKE_CAPACITY];
    uint8_t input[FW_PROTOCOL_MAX_FRAME_SIZE];
    size_t expected_length = 0U;

    if (argc != 2 || read_expected(argv[1], expected, sizeof(expected), &expected_length) != 0
        || fixture_init(&fixture) != 0)
        return 2;
#ifdef _WIN32
    (void)_setmode(0, _O_BINARY);
    (void)_setmode(1, _O_BINARY);
#endif

    for (;;) {
        size_t input_length = 0U;
        if (read_input(input, sizeof(input), &input_length) != 0)
            return 3;
        if (input_length == 0U)
            return 4;

        size_t offset = 0U;
        while (offset < input_length) {
            size_t consumed = 0U;
            enum fw_update_manager_result result = fw_update_manager_feed(
                &fixture.manager, &input[offset], input_length - offset, fixture.now_ms++, &consumed);
            offset += consumed;
            if (result == FW_UPDATE_MANAGER_NEED_MORE) {
                if (consumed == 0U)
                    return 9;
                continue;
            }
            if (result != FW_UPDATE_MANAGER_RESPONSE_READY)
                return 5;

            const uint8_t* tx = NULL;
            size_t tx_size = 0U;
            if (fw_update_manager_tx_view(&fixture.manager, &tx, &tx_size) != FW_UPDATE_MANAGER_OK || tx == NULL
                || write_output(tx, tx_size) != 0
                || fw_update_manager_consume_tx(&fixture.manager, tx_size) != FW_UPDATE_MANAGER_OK
                || fw_update_manager_notify_tx_idle(&fixture.manager) != FW_UPDATE_MANAGER_OK)
                return 6;

            enum fw_update_manager_action action = fw_update_manager_take_action(&fixture.manager);
            if (action == FW_UPDATE_MANAGER_ACTION_RESET)
                return validate_result(&fixture, expected, expected_length) == 0 ? 0 : 7;
            if (action != FW_UPDATE_MANAGER_ACTION_NONE)
                return 8;
        }
    }
}
