#include "fw_update/product_config_flashdb.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <flashdb.h>

#define PRODUCT_CONFIG_KEY       "product.identity"
#define PRODUCT_CONFIG_BLOB_SIZE 16U

static const uint8_t product_config_magic[4] = {'H', 'C', 'P', 'I'};

struct flashdb_product_config_context {
    struct fdb_kvdb db;
    struct fw_update_product_identity default_identity;
};

static struct flashdb_product_config_context backend;

static uint16_t read_u16_le(const uint8_t* input) {
    return (uint16_t)input[0] | (uint16_t)((uint16_t)input[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t* input) {
    return (uint32_t)input[0] | ((uint32_t)input[1] << 8U) | ((uint32_t)input[2] << 16U) | ((uint32_t)input[3] << 24U);
}

static void write_u16_le(uint8_t* output, uint16_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void write_u32_le(uint8_t* output, uint32_t value) {
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void encode_identity(const struct fw_update_product_identity* identity, uint8_t* output) {
    memcpy(output, product_config_magic, sizeof(product_config_magic));
    output[4] = FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION;
    output[5] = 0U;
    write_u16_le(&output[6], identity->board_revision);
    write_u32_le(&output[8], identity->hardware_id);
    write_u32_le(&output[12], identity->board_id);
}

static enum fw_update_result decode_identity(const uint8_t* input, struct fw_update_product_identity* identity) {
    if (memcmp(input, product_config_magic, sizeof(product_config_magic)) != 0
        || input[4] != FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION || input[5] != 0U)
        return FW_UPDATE_ERR_IO;
    identity->board_revision = read_u16_le(&input[6]);
    identity->hardware_id = read_u32_le(&input[8]);
    identity->board_id = read_u32_le(&input[12]);
    return FW_UPDATE_OK;
}

static enum fw_update_result get_config(void* context, struct fw_update_product_config_state* state) {
    struct flashdb_product_config_context* flashdb = context;
    struct fdb_blob blob = {0};
    uint8_t encoded[PRODUCT_CONFIG_BLOB_SIZE] = {0};
    size_t length = fdb_kv_get_blob(&flashdb->db, PRODUCT_CONFIG_KEY, fdb_blob_make(&blob, encoded, sizeof(encoded)));

    if (length == 0U && blob.saved.len == 0U) {
        state->identity = flashdb->default_identity;
        state->provisioned = 0U;
        return FW_UPDATE_OK;
    }
    if (length != sizeof(encoded) || blob.saved.len != sizeof(encoded)
        || decode_identity(encoded, &state->identity) != FW_UPDATE_OK)
        return FW_UPDATE_ERR_IO;
    state->provisioned = 1U;
    return FW_UPDATE_OK;
}

static enum fw_update_result set_config(void* context, const struct fw_update_product_identity* identity) {
    struct flashdb_product_config_context* flashdb = context;
    struct fw_update_product_config_state current;
    struct fdb_blob blob;
    uint8_t encoded[PRODUCT_CONFIG_BLOB_SIZE];

    enum fw_update_result result = get_config(context, &current);
    if (result != FW_UPDATE_OK)
        return result;
    if (current.provisioned != 0U)
        return FW_UPDATE_ERR_LOCKED;

    encode_identity(identity, encoded);
    if (fdb_kv_set_blob(&flashdb->db, PRODUCT_CONFIG_KEY, fdb_blob_make(&blob, encoded, sizeof(encoded))) != FDB_NO_ERR)
        return FW_UPDATE_ERR_IO;

    result = get_config(context, &current);
    return result == FW_UPDATE_OK && current.provisioned != 0U && current.identity.hardware_id == identity->hardware_id
                   && current.identity.board_id == identity->board_id
                   && current.identity.board_revision == identity->board_revision
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
    if (fdb_kvdb_init(&backend.db, "product", "product_kv", NULL, NULL) != FDB_NO_ERR)
        return FW_UPDATE_ERR_IO;

    config->ops = &ops;
    config->context = &backend;
    return FW_UPDATE_OK;
}
