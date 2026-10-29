#ifndef FW_UPDATE_ADMIN_AUTH_H
#define FW_UPDATE_ADMIN_AUTH_H

#include <stdint.h>

#include "fw_update/error.h"

#define FW_UPDATE_ADMIN_CHALLENGE_SIZE 32U
#define FW_UPDATE_ADMIN_SIGNATURE_SIZE 64U

/**
 * Administrator challenge-response backend. The host signs
 * SHA-256(FW_UPDATE_ADMIN_DOMAIN || challenge) with the administrator private key; verify() must check that
 * raw r||s signature against the administrator public key, never against the image-signing key.
 */
struct fw_update_admin_auth_ops {
    enum fw_update_result (*generate_challenge)(void* context, uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE]);
    enum fw_update_result (*verify)(void* context, const uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE],
                                    const uint8_t signature[FW_UPDATE_ADMIN_SIGNATURE_SIZE]);
};

struct fw_update_admin_auth {
    const struct fw_update_admin_auth_ops* ops;
    void* context;
};

/** Domain separator prepended to the challenge so the administrator key signs nothing else by accident. */
#define FW_UPDATE_ADMIN_DOMAIN "HC32-FWUP-ADMIN-V1"

enum fw_update_result fw_update_admin_auth_generate_challenge(const struct fw_update_admin_auth* auth,
                                                              uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE]);
enum fw_update_result fw_update_admin_auth_verify(const struct fw_update_admin_auth* auth,
                                                  const uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE],
                                                  const uint8_t signature[FW_UPDATE_ADMIN_SIGNATURE_SIZE]);

#endif
