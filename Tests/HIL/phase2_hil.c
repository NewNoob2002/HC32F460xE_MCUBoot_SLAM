#include "phase2_hil.h"

#include <stdint.h>

#include "fw_update/boot_control_mcuboot.h"
#include "fw_update/storage_mcuboot.h"

enum {
    PHASE2_HIL_MODE_STORAGE = 1,
    PHASE2_HIL_MODE_PENDING = 2,
    PHASE2_HIL_STAGE_DONE = 0x600D0000U,
};

volatile uint32_t g_phase2_hil_stage;
volatile uint32_t g_phase2_hil_capacity;
volatile uint32_t g_phase2_hil_first_read;
volatile uint32_t g_phase2_hil_last_read;
volatile int g_phase2_hil_result;

static int run_storage(void) {
    static const uint32_t first_pattern = 0x13579BDFU;
    static const uint32_t last_pattern = 0x2468ACE0U;
    struct fw_update_storage storage;
    struct fw_update_storage_info info;
    uint32_t value = 0U;

    fw_update_storage_mcuboot_init(&storage);
    g_phase2_hil_stage = 1U;
    enum fw_update_result result = fw_update_storage_get_info(&storage, &info);
    if (result != FW_UPDATE_OK)
        return result;

    g_phase2_hil_capacity = info.capacity;
    if (info.capacity < sizeof(uint32_t) || info.write_alignment > sizeof(uint32_t))
        return FW_UPDATE_ERR_ALIGNMENT;

    g_phase2_hil_stage = 2U;
    result = fw_update_storage_erase_all(&storage);
    if (result != FW_UPDATE_OK)
        return result;

    g_phase2_hil_stage = 3U;
    result = fw_update_storage_write(&storage, 0U, &first_pattern, sizeof(first_pattern));
    if (result != FW_UPDATE_OK)
        return result;
    result = fw_update_storage_read(&storage, 0U, &value, sizeof(value));
    if (result != FW_UPDATE_OK || value != first_pattern)
        return result == FW_UPDATE_OK ? FW_UPDATE_ERR_IO : result;
    g_phase2_hil_first_read = value;

    const uint32_t last_offset = info.capacity - (uint32_t)sizeof(last_pattern);
    g_phase2_hil_stage = 4U;
    result = fw_update_storage_write(&storage, last_offset, &last_pattern, sizeof(last_pattern));
    if (result != FW_UPDATE_OK)
        return result;
    result = fw_update_storage_read(&storage, last_offset, &value, sizeof(value));
    if (result != FW_UPDATE_OK || value != last_pattern)
        return result == FW_UPDATE_OK ? FW_UPDATE_ERR_IO : result;
    g_phase2_hil_last_read = value;
    return FW_UPDATE_OK;
}

static int run_pending(void) {
    struct fw_update_boot_control control;
    fw_update_boot_control_mcuboot_init(&control);
    g_phase2_hil_stage = 5U;
    return fw_update_boot_control_request_test_upgrade(&control);
}

int phase2_hil_run(int mode) {
    g_phase2_hil_stage = 0U;
    g_phase2_hil_capacity = 0U;
    g_phase2_hil_first_read = 0U;
    g_phase2_hil_last_read = 0U;

    int result;
    if (mode == PHASE2_HIL_MODE_STORAGE)
        result = run_storage();
    else if (mode == PHASE2_HIL_MODE_PENDING)
        result = run_pending();
    else
        result = FW_UPDATE_ERR_INVALID_ARGUMENT;

    g_phase2_hil_result = result;
    g_phase2_hil_stage = PHASE2_HIL_STAGE_DONE | ((uint32_t)(uint8_t)(-result));
    return result;
}
