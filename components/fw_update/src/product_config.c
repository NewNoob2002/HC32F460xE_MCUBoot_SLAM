#include "fw_update/product_config.h"

#include <stddef.h>

enum fw_update_result fw_update_product_config_get(const struct fw_update_product_config* config,
                                                   struct fw_update_product_config_state* state) {
    if (config == NULL || config->ops == NULL || config->ops->get == NULL || state == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return config->ops->get(config->context, state);
}

enum fw_update_result fw_update_product_config_set(const struct fw_update_product_config* config,
                                                   const struct fw_update_product_identity* identity) {
    if (config == NULL || config->ops == NULL || config->ops->set == NULL || identity == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return config->ops->set(config->context, identity);
}
