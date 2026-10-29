#ifndef FW_UPDATE_ADMIN_AUTH_ECDSA_H
#define FW_UPDATE_ADMIN_AUTH_ECDSA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "fw_update/admin_auth.h"

#define FW_UPDATE_ADMIN_PUBLIC_KEY_SIZE 64U

/** Caller-supplied entropy source; returns false when it cannot produce length bytes. */
typedef bool (*fw_update_admin_random_fn)(uint8_t* output, size_t length);

/**
 * Binds an ECDSA-P256 administrator backend to a raw uncompressed public key (X||Y, without the 0x04 prefix).
 * The key must stay valid for the lifetime of auth. Returns FW_UPDATE_ERR_INVALID_ARGUMENT for an off-curve key.
 */
enum fw_update_result fw_update_admin_auth_ecdsa_init(struct fw_update_admin_auth* auth,
                                                      const uint8_t public_key[FW_UPDATE_ADMIN_PUBLIC_KEY_SIZE],
                                                      fw_update_admin_random_fn random);

#endif
