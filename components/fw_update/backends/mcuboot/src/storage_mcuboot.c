#include "fw_update/storage_mcuboot.h"

#include <stddef.h>

#include "flash_map_backend/flash_map_backend.h"

static enum fw_update_result open_secondary(const struct flash_area** area) {
    return flash_area_open(FLASH_AREA_IMAGE_SECONDARY(0), area) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_IO;
}

static enum fw_update_result get_info(void* context, struct fw_update_storage_info* info) {
    (void)context;
    _Static_assert(MCUBOOT_TRAILER_RESERVE <= UINT32_MAX, "MCUboot trailer reserve exceeds uint32_t");
    const uint32_t trailer_reserve = (uint32_t)MCUBOOT_TRAILER_RESERVE;
    const struct flash_area* area;
    enum fw_update_result result = open_secondary(&area);
    if (result != FW_UPDATE_OK)
        return result;

    struct flash_sector sector;
    const uint32_t write_alignment = flash_area_align(area);
    if (flash_area_get_sector(area, 0U, &sector) != 0 || write_alignment == 0U || sector.fs_size == 0U) {
        flash_area_close(area);
        return FW_UPDATE_ERR_IO;
    }

    const uint32_t slot_size = flash_area_get_size(area);
    if (slot_size <= trailer_reserve || (trailer_reserve % sector.fs_size) != 0U) {
        flash_area_close(area);
        return FW_UPDATE_ERR_IO;
    }

    info->capacity = slot_size - trailer_reserve;
    info->write_alignment = write_alignment;
    info->erase_alignment = sector.fs_size;
    info->erased_value = flash_area_erased_val(area);
    flash_area_close(area);
    return info->capacity == 0U ? FW_UPDATE_ERR_IO : FW_UPDATE_OK;
}

static enum fw_update_result erase_all(void* context) {
    (void)context;
    const struct flash_area* area;
    enum fw_update_result result = open_secondary(&area);
    if (result != FW_UPDATE_OK)
        return result;
    result = flash_area_erase(area, 0U, flash_area_get_size(area)) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_IO;
    flash_area_close(area);
    return result;
}

static enum fw_update_result write(void* context, uint32_t offset, const void* data, uint32_t length) {
    (void)context;
    const struct flash_area* area;
    enum fw_update_result result = open_secondary(&area);
    if (result != FW_UPDATE_OK)
        return result;
    result = flash_area_write(area, offset, data, length) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_IO;
    flash_area_close(area);
    return result;
}

static enum fw_update_result read(void* context, uint32_t offset, void* data, uint32_t length) {
    (void)context;
    const struct flash_area* area;
    enum fw_update_result result = open_secondary(&area);
    if (result != FW_UPDATE_OK)
        return result;
    result = flash_area_read(area, offset, data, length) == 0 ? FW_UPDATE_OK : FW_UPDATE_ERR_IO;
    flash_area_close(area);
    return result;
}

static const struct fw_update_storage_ops ops = {
    .get_info = get_info,
    .erase_all = erase_all,
    .write = write,
    .read = read,
};

void fw_update_storage_mcuboot_init(struct fw_update_storage* storage) {
    if (storage == NULL)
        return;
    storage->ops = &ops;
    storage->context = NULL;
}
