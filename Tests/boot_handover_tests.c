#include <assert.h>

#include "boot_handover.h"
#include "boot_memory_map.h"
#include "flash_map_backend/flash_map_backend.h"

#define HC32_MAIN_SRAM_BASE  UINT32_C(0x1FFF8000)
#define HC32_MAIN_SRAM_LIMIT UINT32_C(0x20027000)

static void test_image_contract(void) {
    assert(boot_handover_image_is_valid(FLASH_DEVICE_INTERNAL_FLASH, PRIMARY_SLOT_BASE, (uint16_t)MCUBOOT_HEADER_SIZE));
    assert(!boot_handover_image_is_valid(1U, PRIMARY_SLOT_BASE, (uint16_t)MCUBOOT_HEADER_SIZE));
    assert(
        !boot_handover_image_is_valid(FLASH_DEVICE_INTERNAL_FLASH, SECONDARY_SLOT_BASE, (uint16_t)MCUBOOT_HEADER_SIZE));
    assert(!boot_handover_image_is_valid(FLASH_DEVICE_INTERNAL_FLASH, PRIMARY_SLOT_BASE, 0U));
}

static void test_vector_contract(void) {
    const uint32_t app_end = APP_LINK_ORIGIN + APP_LINK_SIZE;

    assert(boot_handover_vectors_are_valid(HC32_MAIN_SRAM_BASE + 8U, APP_LINK_ORIGIN | 1U));
    assert(boot_handover_vectors_are_valid(HC32_MAIN_SRAM_LIMIT, app_end - 1U));

    assert(!boot_handover_vectors_are_valid(HC32_MAIN_SRAM_BASE, APP_LINK_ORIGIN | 1U));
    assert(!boot_handover_vectors_are_valid(HC32_MAIN_SRAM_LIMIT + 8U, APP_LINK_ORIGIN | 1U));
    assert(!boot_handover_vectors_are_valid(HC32_MAIN_SRAM_BASE + 4U, APP_LINK_ORIGIN | 1U));
    assert(!boot_handover_vectors_are_valid(HC32_MAIN_SRAM_BASE + 8U, APP_LINK_ORIGIN));
    assert(!boot_handover_vectors_are_valid(HC32_MAIN_SRAM_BASE + 8U, (APP_LINK_ORIGIN - 2U) | 1U));
    assert(!boot_handover_vectors_are_valid(HC32_MAIN_SRAM_BASE + 8U, app_end | 1U));
}

int main(void) {
    test_image_contract();
    test_vector_contract();
    return 0;
}
