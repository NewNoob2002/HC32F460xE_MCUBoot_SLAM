#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bootutil/fault_injection_hardening.h"
#include "bootutil/image.h"
#include "bootutil_priv.h"
#include "flash_map_backend/flash_map_backend.h"
#include "fw_update/boot_control_mcuboot.h"
#include "fw_update/storage_mcuboot.h"
#include "product_identity.h"

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
static int pending_calls;
static fih_ret validation_result;
static int tlv_begin_result;
static unsigned tlv_count;
static unsigned tlv_next_calls;
static uint16_t tlv_length;
static uint8_t compatibility[HC32_COMPATIBILITY_PAYLOAD_SIZE];
static int loader_open_result;
static int sector_result;
static int loader_close_calls;
static struct fw_update_product_config_state product_config_state;

fih_ret FIH_SUCCESS = FIH_POSITIVE_VALUE;
fih_ret FIH_FAILURE = FIH_NEGATIVE_VALUE;

static enum fw_update_result product_config_get(void* context, struct fw_update_product_config_state* state) {
    (void)context;
    *state = product_config_state;
    return FW_UPDATE_OK;
}

static enum fw_update_result product_config_set(void* context, const struct fw_update_product_identity* identity) {
    (void)context;
    (void)identity;
    return FW_UPDATE_ERR_LOCKED;
}

static const struct fw_update_product_config_ops product_config_ops = {
    .get = product_config_get,
    .set = product_config_set,
};

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
    memset(data, 0, length);
    if (offset == 64U && length == sizeof(compatibility))
        memcpy(data, compatibility, sizeof(compatibility));
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
    ++pending_calls;
    pending_image = image_index;
    pending_permanent = permanent;
    return boot_result;
}

fih_ret bootutil_img_validate(struct boot_loader_state* state, struct image_header* header,
                              const struct flash_area* area, uint8_t* temporary, uint32_t temporary_size, uint8_t* seed,
                              int seed_length, uint8_t* output_hash) {
    assert(state != NULL);
    (void)header;
    (void)area;
    (void)temporary;
    (void)temporary_size;
    (void)seed;
    (void)seed_length;
    (void)output_hash;
    return validation_result;
}

void boot_state_init(struct boot_loader_state* state) {
    assert(state != NULL);
    memset(state, 0, sizeof(*state));
}

int boot_open_all_flash_areas(struct boot_loader_state* state) {
    assert(state != NULL);
    if (loader_open_result != 0)
        return loader_open_result;
    BOOT_IMG_AREA(state, BOOT_SLOT_SECONDARY) = &secondary;
    return 0;
}

int boot_read_sectors(struct boot_loader_state* state, struct boot_sector_buffer* sectors) {
    assert(state != NULL && sectors == NULL);
    return sector_result;
}

void boot_close_all_flash_areas(struct boot_loader_state* state) {
    assert(state != NULL);
    ++loader_close_calls;
}

int bootutil_tlv_iter_begin(struct image_tlv_iter* iterator, const struct image_header* header,
                            const struct flash_area* area, uint16_t type, bool protected) {
    assert(iterator != NULL && header != NULL && area == &secondary);
    assert(type == HC32_COMPATIBILITY_TLV_TYPE && protected);
    tlv_next_calls = 0U;
    return tlv_begin_result;
}

int bootutil_tlv_iter_next(struct image_tlv_iter* iterator, uint32_t* offset, uint16_t* length, uint16_t* type) {
    assert(iterator != NULL && offset != NULL && length != NULL && type == NULL);
    if (tlv_next_calls++ >= tlv_count)
        return 1;
    *offset = 64U;
    *length = tlv_length;
    return 0;
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
    const struct fw_update_product_config product_config = {
        .ops = &product_config_ops,
        .context = NULL,
    };

    open_result = 0;
    flash_result = 0;
    boot_result = 0;
    validation_result = FIH_SUCCESS;
    tlv_begin_result = 0;
    tlv_count = 1U;
    tlv_length = sizeof(compatibility);
    loader_open_result = 0;
    sector_result = 0;
    loader_close_calls = 0;
    compatibility[0] = HC32_COMPATIBILITY_FORMAT_VERSION;
    compatibility[1] = 0U;
    compatibility[2] = (uint8_t)HC32_PRODUCT_BOARD_REVISION;
    compatibility[3] = (uint8_t)(HC32_PRODUCT_BOARD_REVISION >> 8U);
    compatibility[4] = (uint8_t)HC32_PRODUCT_HARDWARE_ID;
    compatibility[5] = (uint8_t)(HC32_PRODUCT_HARDWARE_ID >> 8U);
    compatibility[6] = (uint8_t)(HC32_PRODUCT_HARDWARE_ID >> 16U);
    compatibility[7] = (uint8_t)(HC32_PRODUCT_HARDWARE_ID >> 24U);
    compatibility[8] = (uint8_t)HC32_PRODUCT_BOARD_ID;
    compatibility[9] = (uint8_t)(HC32_PRODUCT_BOARD_ID >> 8U);
    compatibility[10] = (uint8_t)(HC32_PRODUCT_BOARD_ID >> 16U);
    compatibility[11] = (uint8_t)(HC32_PRODUCT_BOARD_ID >> 24U);
    product_config_state.identity.hardware_id = HC32_PRODUCT_HARDWARE_ID;
    product_config_state.identity.board_id = HC32_PRODUCT_BOARD_ID;
    product_config_state.identity.board_revision = HC32_PRODUCT_BOARD_REVISION;
    product_config_state.provisioned = 0U;

    fw_update_boot_control_mcuboot_init(&control, &product_config);
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_OK);
    assert(pending_image == 0 && pending_permanent == 0);
    assert(loader_close_calls == 1);
    assert(fw_update_boot_control_confirm_running_image(&control) == FW_UPDATE_OK);
    assert(confirmed_image == 0);

    boot_result = -9;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    assert(fw_update_boot_control_confirm_running_image(&control) == FW_UPDATE_ERR_BOOT_CONTROL);

    boot_result = 0;
    const int calls_before_rejection = pending_calls;
    validation_result = FIH_FAILURE;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    validation_result = FIH_SUCCESS;
    compatibility[4] ^= 1U;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    compatibility[4] ^= 1U;
    tlv_count = 2U;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    tlv_count = 1U;
    loader_open_result = -1;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    loader_open_result = 0;
    sector_result = -1;
    assert(fw_update_boot_control_request_test_upgrade(&control) == FW_UPDATE_ERR_BOOT_CONTROL);
    assert(pending_calls == calls_before_rejection);
}

int main(void) {
    test_storage_backend();
    test_boot_control_backend();
    return 0;
}
