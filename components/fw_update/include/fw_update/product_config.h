#ifndef FW_UPDATE_PRODUCT_CONFIG_H
#define FW_UPDATE_PRODUCT_CONFIG_H

#include <stdint.h>

#include "fw_update/error.h"

#define FW_UPDATE_PRODUCT_CONFIG_FORMAT_VERSION 1U

struct fw_update_product_identity {
    uint32_t hardware_id;
    uint32_t board_id;
    uint16_t board_revision;
};

struct fw_update_product_config_state {
    struct fw_update_product_identity identity;
    uint8_t provisioned;
};

struct fw_update_product_config_ops {
    enum fw_update_result (*get)(void* context, struct fw_update_product_config_state* state);
    enum fw_update_result (*set)(void* context, const struct fw_update_product_identity* identity);
};

struct fw_update_product_config {
    const struct fw_update_product_config_ops* ops;
    void* context;
};

enum fw_update_result fw_update_product_config_get(const struct fw_update_product_config* config,
                                                   struct fw_update_product_config_state* state);
enum fw_update_result fw_update_product_config_set(const struct fw_update_product_config* config,
                                                   const struct fw_update_product_identity* identity);

#endif
