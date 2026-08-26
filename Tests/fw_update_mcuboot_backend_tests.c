#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_map_backend/flash_map_backend.h"
#include "fw_update/boot_control_mcuboot.h"
#include "fw_update/storage_mcuboot.h"

static const struct flash_area secondary = {FLASH_AREA_ID_SECONDARY, FLASH_DEVICE_INTERNAL_FLASH, 0U,
                                            SECONDARY_SLOT_BASE, SECONDARY_SLOT_SIZE};
static uint8_t opened_id;
static uint32_t io_offset;
static uint32_t io_length;
static int open_result;
static int flash_result;
static int boot_result;
static int pending_image;
static int pending_permanent;
static int confirmed_image;

int flash_area_open(uint8_t id, const struct flash_area** area) {
    opened_id = id;
    if (open_result != 0)
        return open_result;
    if (id != FLASH_AREA_ID_SECONDARY || area == NULL)
        return -1;
    *area = &secondary;
    return 0;
}

void flash_area_close(const struct flash_area* area) {
    assert(area == &secondary);
}

int flash_area_read(const struct flash_area* area, uint32_t offset, void* data, uint32_t length) {
    assert(area == &secondary);
    assert(data != NULL);
    io_offset = offset;
    io_length = length;
    return flash_result;
}

int flash_area_write(const struct flash_area* area, uint32_t offset, const void* data, uint32_t length) {
    assert(area == &secondary);
    assert(data != NULL);
    io_offset = offset;
    io_length = length;
    return flash_result;
}

int flash_area_erase(const struct flash_area* area, uint32_t offset, uint32_t length) {
    assert(area == &secondary);
    io_offset = offset;
    io_length = length;
    return flash_result;
}

uint32_t flash_area_align(const struct flash_area* area) {
    assert(area == &secondary);
    return FLASH_WRITE_ALIGN;
}

uint8_t flash_area_erased_val(const struct flash_area* area) {
    assert(area == &secondary);
    return 0xFFU;
}

int flash_area_get_sector(const struct flash_area* area, uint32_t offset, struct flash_sector* sector) {
    assert(area == &secondary);
    if (offset >= area->fa_size || sector == NULL)
        return -1;
    sector->fs_off = offset - (offset % FLASH_SECTOR_SIZE);
    sector->fs_size = FLASH_SECTOR_SIZE;
    return 0;
}

int boot_set_pending_multi(int image_index, int permanent) {
    pending_image = image_index;
    pending_permanent = permanent;
    return boot_result;
}

int boot_set_confirmed_multi(int image_index) {
    confirmed_image = image_index;
    return boot_result;
}

static void test_storage_backend(void) {
    struct fw_update_storage storage;
    struct fw_update_storage_info info;
    uint32_t word = 0U;

    fw_update_storage_mcuboot_init(&storage);
    assert(fw_update_storage_get_info(&storage, &info) == FW_UPDATE_OK);
    assert(opened_id == FLASH_AREA_ID_SECONDARY);
    assert(info.capacity == SECONDARY_SLOT_SIZE - MCUBOOT_TRAILER_RESERVE);
    assert(info.write_alignment == FLASH_WRITE_ALIGN);
    assert(info.erase_alignment == FLASH_SECTOR_SIZE);
    assert(info.erased_value == 0xFFU);
    const uint32_t last_word_offset = info.capacity - (uint32_t)sizeof(word);

    assert(fw_update_storage_erase_all(&storage) == FW_UPDATE_OK);
    assert(opened_id == FLASH_AREA_ID_SECONDARY);
    assert(io_offset == 0U && io_length == SECONDARY_SLOT_SIZE);

    assert(fw_update_storage_write(&storage, 4U, &word, sizeof(word)) == FW_UPDATE_OK);
    assert(io_offset == 4U && io_length == sizeof(word));
    assert(fw_update_storage_write(&storage, last_word_offset, &word, sizeof(word)) == FW_UPDATE_OK);
    assert(io_offset == last_word_offset);
    assert(fw_update_storage_write(&storage, info.capacity, &word, sizeof(word)) == FW_UPDATE_ERR_BOUNDS);
    assert(fw_update_storage_read(&storage, 7U, &word, sizeof(word)) == FW_UPDATE_OK);
    assert(io_offset == 7U && io_length == sizeof(word));

    flash_result = -7;
    assert(fw_update_storage_erase_all(&storage) == FW_UPDATE_ERR_IO);
    assert(fw_update_storage_write(&storage, 0U, &word, sizeof(word)) == FW_UPDATE_ERR_IO);

    flash_result = 0;
    open_result = -3;
    assert(fw_update_storage_get_info(&storage, &info) == FW_UPDATE_ERR_IO);
}

static void test_boot_control_backend(void) {
    struct fw_update_boot_control control;

    fw_update_boot_control_mcuboot_init(&control);
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_OK);
    assert(pending_image == 0 && pending_permanent == 0);
    assert(fw_update_boot_control_confirm_running_image(&control) == FW_UPDATE_OK);
    assert(confirmed_image == 0);

    boot_result = -9;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    assert(fw_update_boot_control_confirm_running_image(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
}

int main(void) {
    test_storage_backend();
    test_boot_control_backend();
    return 0;
}
