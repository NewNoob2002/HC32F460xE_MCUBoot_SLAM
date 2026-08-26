#include "fw_update/storage.h"

#include <stddef.h>

static int valid_range(const struct fw_update_storage_info* info, uint32_t offset, uint32_t length) {
    return length > 0U && offset < info->capacity && length <= info->capacity - offset;
}

enum fw_update_result fw_update_storage_get_info(const struct fw_update_storage* storage,
                                                 struct fw_update_storage_info* info) {
    if (storage == NULL || storage->ops == NULL || storage->ops->get_info == NULL || info == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;

    const enum fw_update_result result = storage->ops->get_info(storage->context, info);
    if (result != FW_UPDATE_OK)
        return result;
    if (info->capacity == 0U || info->write_alignment == 0U || info->erase_alignment == 0U)
        return FW_UPDATE_ERR_IO;
    return FW_UPDATE_OK;
}

enum fw_update_result fw_update_storage_erase_all(const struct fw_update_storage* storage) {
    if (storage == NULL || storage->ops == NULL || storage->ops->erase_all == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return storage->ops->erase_all(storage->context);
}

enum fw_update_result fw_update_storage_write(const struct fw_update_storage* storage, uint32_t offset,
                                              const void* data, uint32_t length) {
    if (storage == NULL || storage->ops == NULL || storage->ops->write == NULL || data == NULL || length == 0U)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;

    struct fw_update_storage_info info;
    enum fw_update_result result = fw_update_storage_get_info(storage, &info);
    if (result != FW_UPDATE_OK)
        return result;
    if (!valid_range(&info, offset, length))
        return FW_UPDATE_ERR_BOUNDS;
    if ((offset % info.write_alignment) != 0U || (length % info.write_alignment) != 0U)
        return FW_UPDATE_ERR_ALIGNMENT;
    return storage->ops->write(storage->context, offset, data, length);
}

enum fw_update_result fw_update_storage_read(const struct fw_update_storage* storage, uint32_t offset, void* data,
                                             uint32_t length) {
    if (storage == NULL || storage->ops == NULL || storage->ops->read == NULL || data == NULL || length == 0U)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;

    struct fw_update_storage_info info;
    enum fw_update_result result = fw_update_storage_get_info(storage, &info);
    if (result != FW_UPDATE_OK)
        return result;
    if (!valid_range(&info, offset, length))
        return FW_UPDATE_ERR_BOUNDS;
    return storage->ops->read(storage->context, offset, data, length);
}
