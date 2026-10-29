#include "fw_update/admin_auth.h"

#include <stddef.h>

enum fw_update_result fw_update_admin_auth_generate_challenge(const struct fw_update_admin_auth* auth,
                                                              uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE]) {
    if (auth == NULL || auth->ops == NULL || auth->ops->generate_challenge == NULL || challenge == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return auth->ops->generate_challenge(auth->context, challenge);
}

enum fw_update_result fw_update_admin_auth_verify(const struct fw_update_admin_auth* auth,
                                                  const uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE],
                                                  const uint8_t signature[FW_UPDATE_ADMIN_SIGNATURE_SIZE]) {
    if (auth == NULL || auth->ops == NULL || auth->ops->verify == NULL || challenge == NULL || signature == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    return auth->ops->verify(auth->context, challenge, signature);
}
