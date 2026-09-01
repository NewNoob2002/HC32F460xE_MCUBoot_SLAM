#include "fw_update/product_config_flashdb.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <flashdb.h>

#define PRODUCT_CONFIG_KEY              "product.identity"
#define PRODUCT_CONFIG_BLOB_HEADER_SIZE 10U
#define PRODUCT_CONFIG_MAX_BLOB_SIZE                                                                                   \
    (PRODUCT_CONFIG_BLOB_HEADER_SIZE + FW_UPDATE_DEVICE_SERIAL_MAX_LENGTH + FW_UPDATE_HARDWARE_VERSION_MAX_LENGTH)

static const uint8_t product_config_magic[4] = {'H', 'C', 'P', 'I'};

struct flashdb_product_config_context {
    struct fdb_kvdb db;
    struct fw_update_product_identity default_identity;
};

static struct flashdb_product_config_context backend;

static uint16_t read_u16_le(const uint8_t* input) {
    return (uint16_t)input[0] | (uint16_t)((uint16_t)input[1] << 8U);
}

static void write_u16_le(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static size_t encode_identity(const struct fw_update_product_identity* identity, uint8_t* output) {
    size_t serial_length = strlen(identity->device_serial);
    size_t hardware_version_length = strlen(identity->hardware_version);

    memcpy(output, product_config_magic, sizeof(product_config_magic));
    output[4] = FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION;
    output[5] = 0U;
    output[6] = (uint8_t)serial_length;
    output[7] = (uint8_t)hardware_version_length;
    write_u16_le(&output[8], identity->application_pid);
    memcpy(&output[PRODUCT_CONFIG_BLOB_HEADER_SIZE], identity->device_serial, serial_length);
    memcpy(&output[PRODUCT_CONFIG_BLOB_HEADER_SIZE + serial_length], identity->hardware_version,
           hardware_version_length);
    return PRODUCT_CONFIG_BLOB_HEADER_SIZE + serial_length + hardware_version_length;
}

static enum fw_update_result decode_identity(const uint8_t* input, size_t length,
                                             const struct fw_update_product_identity* defaults,
                                             struct fw_update_product_identity* identity) {
    size_t serial_length;
    size_t hardware_version_length;

    if (length < PRODUCT_CONFIG_BLOB_HEADER_SIZE
        || memcmp(input, product_config_magic, sizeof(product_config_magic)) != 0
        || input[4] != FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION || input[5] != 0U)
        return FW_UPDATE_ERR_IO;

    serial_length = input[6];
    hardware_version_length = input[7];
    if (serial_length == 0U || serial_length > FW_UPDATE_DEVICE_SERIAL_MAX_LENGTH || hardware_version_length == 0U
        || hardware_version_length > FW_UPDATE_HARDWARE_VERSION_MAX_LENGTH
        || length != PRODUCT_CONFIG_BLOB_HEADER_SIZE + serial_length + hardware_version_length)
        return FW_UPDATE_ERR_IO;

    *identity = *defaults;
    identity->application_pid = read_u16_le(&input[8]);
    memcpy(identity->device_serial, &input[PRODUCT_CONFIG_BLOB_HEADER_SIZE], serial_length);
    identity->device_serial[serial_length] = 0;
    memcpy(identity->hardware_version, &input[PRODUCT_CONFIG_BLOB_HEADER_SIZE + serial_length],
           hardware_version_length);
    identity->hardware_version[hardware_version_length] = 0;
    return fw_update_product_config_valid_device_serial(identity->device_serial)
                   && fw_update_product_config_valid_hardware_version(identity->hardware_version)
               ? FW_UPDATE_OK
               : FW_UPDATE_ERR_IO;
}

static enum fw_update_result get_config(void* context, struct fw_update_product_config_state* state) {
    struct flashdb_product_config_context* flashdb = context;
    struct fdb_blob blob = {0};
    uint8_t encoded[PRODUCT_CONFIG_MAX_BLOB_SIZE] = {0};
    size_t length = fdb_kv_get_blob(&flashdb->db, PRODUCT_CONFIG_KEY, fdb_blob_make(&blob, encoded, sizeof(encoded)));

    if (length == 0U && blob.saved.len == 0U) {
        state->identity = flashdb->default_identity;
        state->provisioned = 0U;
        return FW_UPDATE_OK;
    }
    if (length != blob.saved.len
        || decode_identity(encoded, length, &flashdb->default_identity, &state->identity) != FW_UPDATE_OK)
        return FW_UPDATE_ERR_IO;
    state->provisioned = 1U;
    return FW_UPDATE_OK;
}

static enum fw_update_result set_config(void* context, const struct fw_update_product_identity* identity) {
    struct flashdb_product_config_context* flashdb = context;
    struct fw_update_product_config_state current;
    struct fdb_blob blob;
    uint8_t encoded[PRODUCT_CONFIG_MAX_BLOB_SIZE];
    size_t encoded_length;

    enum fw_update_result result = get_config(context, &current);
    if (result != FW_UPDATE_OK)
        return result;
    if (current.provisioned != 0U)
        return FW_UPDATE_ERR_LOCKED;
    if (!fw_update_product_config_valid_device_serial(identity->device_serial)
        || !fw_update_product_config_valid_hardware_version(identity->hardware_version))
        return FW_UPDATE_ERR_INVALID_ARGUMENT;

    encoded_length = encode_identity(identity, encoded);
    if (fdb_kv_set_blob(&flashdb->db, PRODUCT_CONFIG_KEY, fdb_blob_make(&blob, encoded, encoded_length)) != FDB_NO_ERR)
        return FW_UPDATE_ERR_IO;

    result = get_config(context, &current);
    return result == FW_UPDATE_OK && current.provisioned != 0U
                   && strcmp(current.identity.device_serial, identity->device_serial) == 0
                   && strcmp(current.identity.hardware_version, identity->hardware_version) == 0
                   && current.identity.application_pid == identity->application_pid
               ? FW_UPDATE_OK
               : FW_UPDATE_ERR_IO;
}

static const struct fw_update_product_config_ops ops = {
    .get = get_config,
    .set = set_config,
};

enum fw_update_result fw_update_product_config_flashdb_init(struct fw_update_product_config* config,
                                                            const struct fw_update_product_identity* default_identity) {
    if (config == NULL || default_identity == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;

    memset(&backend, 0, sizeof(backend));
    backend.default_identity = *default_identity;
    backend.default_identity.device_serial[0] = 0;
    backend.default_identity.hardware_version[0] = 0;
    if (fdb_kvdb_init(&backend.db, "product", "product_kv", NULL, NULL) != FDB_NO_ERR)
        return FW_UPDATE_ERR_IO;

    config->ops = &ops;
    config->context = &backend;
    return FW_UPDATE_OK;
}
