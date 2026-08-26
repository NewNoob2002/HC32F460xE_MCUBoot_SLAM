#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_update/boot_control.h"
#include "fw_update/storage.h"

struct fake_storage {
    struct fw_update_storage_info info;
    enum fw_update_result result;
    uint32_t offset;
    uint32_t length;
    unsigned int calls;
};

struct fake_boot_control {
    enum fw_update_result result;
    unsigned int request_calls;
    unsigned int confirm_calls;
};

static enum fw_update_result fake_get_info(void* context, struct fw_update_storage_info* info) {
    struct fake_storage* fake = context;
    ++fake->calls;
    if (fake->result == FW_UPDATE_OK)
        *info = fake->info;
    return fake->result;
}

static enum fw_update_result fake_erase_all(void* context) {
    struct fake_storage* fake = context;
    ++fake->calls;
    return fake->result;
}

static enum fw_update_result fake_write(void* context, uint32_t offset, const void* data, uint32_t length) {
    struct fake_storage* fake = context;
    assert(data != NULL);
    ++fake->calls;
    fake->offset = offset;
    fake->length = length;
    return fake->result;
}

static enum fw_update_result fake_read(void* context, uint32_t offset, void* data, uint32_t length) {
    struct fake_storage* fake = context;
    assert(data != NULL);
    ++fake->calls;
    fake->offset = offset;
    fake->length = length;
    return fake->result;
}

static enum fw_update_result fake_request(void* context) {
    struct fake_boot_control* fake = context;
    ++fake->request_calls;
    return fake->result;
}

static enum fw_update_result fake_confirm(void* context) {
    struct fake_boot_control* fake = context;
    ++fake->confirm_calls;
    return fake->result;
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

static void test_storage_contract(void) {
    struct fake_storage fake = {
        .info =
            {
                .capacity = 0x32000U,
                .write_alignment = 4U,
                .erase_alignment = 0x2000U,
                .erased_value = 0xFFU,
            },
        .result = FW_UPDATE_OK,
    };
    struct fw_update_storage storage = {.ops = &storage_ops, .context = &fake};
    struct fw_update_storage_info info;
    uint32_t word = 0U;

    assert(fw_update_storage_get_info(NULL, &info) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_storage_get_info(&storage, NULL) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_storage_get_info(&storage, &info) == FW_UPDATE_OK);
    assert(info.capacity == 0x32000U);

    fake.calls = 0U;
    assert(fw_update_storage_erase_all(&storage) == FW_UPDATE_OK);
    assert(fake.calls == 1U);

    assert(fw_update_storage_write(&storage, 4U, &word, sizeof(word)) == FW_UPDATE_OK);
    assert(fake.offset == 4U && fake.length == sizeof(word));
    assert(fw_update_storage_write(&storage, 1U, &word, sizeof(word)) == FW_UPDATE_ERR_ALIGNMENT);
    assert(fw_update_storage_write(&storage, 0U, &word, 2U) == FW_UPDATE_ERR_ALIGNMENT);
    assert(fw_update_storage_write(&storage, 0U, NULL, sizeof(word)) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_storage_write(&storage, 0U, &word, 0U) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_storage_write(&storage, 0x31FFCU, &word, sizeof(word)) == FW_UPDATE_OK);
    assert(fw_update_storage_write(&storage, 0x32000U, &word, sizeof(word)) == FW_UPDATE_ERR_BOUNDS);

    assert(fw_update_storage_read(&storage, 3U, &word, sizeof(word)) == FW_UPDATE_OK);
    assert(fake.offset == 3U && fake.length == sizeof(word));
    assert(fw_update_storage_read(&storage, 0x31FFDU, &word, sizeof(word)) == FW_UPDATE_ERR_BOUNDS);
    assert(fw_update_storage_read(&storage, 0U, NULL, sizeof(word)) == FW_UPDATE_ERR_INVALID_ARGUMENT);

    fake.result = FW_UPDATE_ERR_IO;
    assert(fw_update_storage_erase_all(&storage) == FW_UPDATE_ERR_IO);
    assert(fw_update_storage_read(&storage, 0U, &word, sizeof(word)) == FW_UPDATE_ERR_IO);

    fake.result = FW_UPDATE_OK;
    fake.info.write_alignment = 0U;
    assert(fw_update_storage_write(&storage, 0U, &word, sizeof(word)) == FW_UPDATE_ERR_IO);
}

static void test_boot_control_contract(void) {
    struct fake_boot_control fake = {.result = FW_UPDATE_OK};
    struct fw_update_boot_control control = {.ops = &boot_control_ops, .context = &fake};

    assert(fw_update_boot_control_request_test_upgrade(NULL) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_OK);
    assert(fake.request_calls == 1U);
    assert(fw_update_boot_control_confirm_running_image(&control) == FW_UPDATE_OK);
    assert(fake.confirm_calls == 1U);

    fake.result = FW_UPDATE_ERR_BOOT_CONTROL;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    assert(fw_update_boot_control_confirm_running_image(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
}

int main(void) {
    test_storage_contract();
    test_boot_control_contract();
    return 0;
}
