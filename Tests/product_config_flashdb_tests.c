#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fal.h>

#include "boot_memory_map.h"
#include "fw_update/product_config_flashdb.h"

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition))                                                                                              \
            abort();                                                                                                   \
    } while (0)

static uint8_t reserved_flash[RESERVED_SIZE];

static int fake_init(void) {
    return 0;
}

static int fake_read(long offset, uint8_t* buffer, size_t size) {
    if (offset < 0L || buffer == NULL || size == 0U || (size_t)offset > sizeof(reserved_flash)
        || size > sizeof(reserved_flash) - (size_t)offset)
        return -1;
    memcpy(buffer, &reserved_flash[offset], size);
    return (int)size;
}

static int fake_write(long offset, const uint8_t* buffer, size_t size) {
    size_t index;
    if (offset < 0L || buffer == NULL || size == 0U || ((size_t)offset % FLASH_WRITE_ALIGN) != 0U
        || (size % FLASH_WRITE_ALIGN) != 0U || (size_t)offset > sizeof(reserved_flash)
        || size > sizeof(reserved_flash) - (size_t)offset)
        return -1;
    for (index = 0U; index < size; ++index) {
        if ((reserved_flash[(size_t)offset + index] & buffer[index]) != buffer[index])
            return -1;
    }
    for (index = 0U; index < size; ++index)
        reserved_flash[(size_t)offset + index] &= buffer[index];
    return (int)size;
}

static int fake_erase(long offset, size_t size) {
    if (offset < 0L || size == 0U || ((size_t)offset % FLASH_SECTOR_SIZE) != 0U || (size % FLASH_SECTOR_SIZE) != 0U
        || (size_t)offset > sizeof(reserved_flash) || size > sizeof(reserved_flash) - (size_t)offset)
        return -1;
    memset(&reserved_flash[offset], 0xFF, size);
    return (int)size;
}

const struct fal_flash_dev hc32_product_config_flash = {
    .name = "hc32_config",
    .addr = RESERVED_BASE,
    .len = RESERVED_SIZE,
    .blk_size = FLASH_SECTOR_SIZE,
    .ops = {fake_init, fake_read, fake_write, fake_erase},
    .write_gran = FLASH_WRITE_ALIGN * 8U,
};

static void assert_identity(const struct fw_update_product_config_state* state, uint32_t hardware_id, uint32_t board_id,
                            uint16_t board_revision, uint8_t provisioned) {
    CHECK(state->identity.hardware_id == hardware_id);
    CHECK(state->identity.board_id == board_id);
    CHECK(state->identity.board_revision == board_revision);
    CHECK(state->provisioned == provisioned);
}

int main(void) {
    const struct fw_update_product_identity defaults = {
        .hardware_id = UINT32_C(0x00004600),
        .board_id = 1U,
        .board_revision = 2U,
    };
    const struct fw_update_product_identity provisioned = {
        .hardware_id = UINT32_C(0x00004601),
        .board_id = 7U,
        .board_revision = 4U,
    };
    struct fw_update_product_config config;
    struct fw_update_product_config restored;
    struct fw_update_product_config_state state;

    memset(reserved_flash, 0xFF, sizeof(reserved_flash));
    CHECK(fw_update_product_config_flashdb_init(&config, &defaults) == FW_UPDATE_OK);
    CHECK(fw_update_product_config_get(&config, &state) == FW_UPDATE_OK);
    assert_identity(&state, defaults.hardware_id, defaults.board_id, defaults.board_revision, 0U);

    CHECK(fw_update_product_config_set(&config, &provisioned) == FW_UPDATE_OK);
    CHECK(fw_update_product_config_get(&config, &state) == FW_UPDATE_OK);
    assert_identity(&state, provisioned.hardware_id, provisioned.board_id, provisioned.board_revision, 1U);
    CHECK(fw_update_product_config_set(&config, &defaults) == FW_UPDATE_ERR_LOCKED);

    CHECK(fw_update_product_config_flashdb_init(&restored, &defaults) == FW_UPDATE_OK);
    CHECK(fw_update_product_config_get(&restored, &state) == FW_UPDATE_OK);
    assert_identity(&state, provisioned.hardware_id, provisioned.board_id, provisioned.board_revision, 1U);
    return 0;
}
