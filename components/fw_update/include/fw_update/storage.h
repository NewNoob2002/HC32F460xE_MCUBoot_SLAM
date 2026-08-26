#ifndef FW_UPDATE_STORAGE_H
#define FW_UPDATE_STORAGE_H

#include <stdint.h>

#include "fw_update/error.h"

/** Logical candidate-image storage geometry. No physical address is exposed. */
struct fw_update_storage_info {
    uint32_t capacity;
    uint32_t write_alignment;
    uint32_t erase_alignment;
    uint8_t erased_value;
};

/** Backend operations for one candidate-image storage area. */
struct fw_update_storage_ops {
    enum fw_update_result (*get_info)(void* context, struct fw_update_storage_info* info);
    enum fw_update_result (*erase_all)(void* context);
    enum fw_update_result (*write)(void* context, uint32_t offset, const void* data, uint32_t length);
    enum fw_update_result (*read)(void* context, uint32_t offset, void* data, uint32_t length);
};

/** A statically configured candidate-image storage instance. */
struct fw_update_storage {
    const struct fw_update_storage_ops* ops;
    void* context;
};

/** Returns logical storage geometry after validating backend values. */
enum fw_update_result fw_update_storage_get_info(const struct fw_update_storage* storage,
                                                 struct fw_update_storage_info* info);

/** Erases the complete backend-managed candidate slot, including stale boot metadata. */
enum fw_update_result fw_update_storage_erase_all(const struct fw_update_storage* storage);

/** Writes an aligned logical range inside candidate-image storage. */
enum fw_update_result fw_update_storage_write(const struct fw_update_storage* storage, uint32_t offset,
                                              const void* data, uint32_t length);

/** Reads a logical range inside candidate-image storage. */
enum fw_update_result fw_update_storage_read(const struct fw_update_storage* storage, uint32_t offset, void* data,
                                             uint32_t length);

#endif
