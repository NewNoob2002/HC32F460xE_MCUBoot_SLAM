#include "fw_update/admin_auth_ecdsa.h"

#include <string.h>

#include "tinycrypt/constants.h"
#include "tinycrypt/ecc.h"
#include "tinycrypt/ecc_dsa.h"
#include "tinycrypt/sha256.h"

struct admin_auth_ecdsa_context {
    const uint8_t* public_key;
    fw_update_admin_random_fn random;
};

static struct admin_auth_ecdsa_context backend;

static enum fw_update_result generate_challenge(void* context, uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE]) {
    const struct admin_auth_ecdsa_context* ecdsa = context;
    return ecdsa->random(challenge, FW_UPDATE_ADMIN_CHALLENGE_SIZE) ? FW_UPDATE_OK : FW_UPDATE_ERR_IO;
}

static enum fw_update_result verify(void* context, const uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE],
                                    const uint8_t signature[FW_UPDATE_ADMIN_SIGNATURE_SIZE]) {
    static const char domain[] = FW_UPDATE_ADMIN_DOMAIN;
    const struct admin_auth_ecdsa_context* ecdsa = context;
    struct tc_sha256_state_struct sha;
    uint8_t digest[TC_SHA256_DIGEST_SIZE];
    int verified;

    if (tc_sha256_init(&sha) != TC_CRYPTO_SUCCESS
        || tc_sha256_update(&sha, (const uint8_t*)domain, sizeof(domain) - 1U) != TC_CRYPTO_SUCCESS
        || tc_sha256_update(&sha, challenge, FW_UPDATE_ADMIN_CHALLENGE_SIZE) != TC_CRYPTO_SUCCESS
        || tc_sha256_final(digest, &sha) != TC_CRYPTO_SUCCESS)
        return FW_UPDATE_ERR_IO;
    verified = uECC_verify(ecdsa->public_key, digest, sizeof(digest), signature, uECC_secp256r1());
    memset(digest, 0, sizeof(digest));
    return verified == TC_CRYPTO_SUCCESS ? FW_UPDATE_OK : FW_UPDATE_ERR_INVALID_ARGUMENT;
}

static const struct fw_update_admin_auth_ops ops = {
    .generate_challenge = generate_challenge,
    .verify = verify,
};

enum fw_update_result fw_update_admin_auth_ecdsa_init(struct fw_update_admin_auth* auth,
                                                      const uint8_t public_key[FW_UPDATE_ADMIN_PUBLIC_KEY_SIZE],
                                                      fw_update_admin_random_fn random) {
    if (auth == NULL || public_key == NULL || random == NULL)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    /* uECC_valid_public_key() returns 0 only for a point on P-256 that is not the identity. */
    if (uECC_valid_public_key(public_key, uECC_secp256r1()) != 0)
        return FW_UPDATE_ERR_INVALID_ARGUMENT;
    backend.public_key = public_key;
    backend.random = random;
    auth->ops = &ops;
    auth->context = &backend;
    return FW_UPDATE_OK;
}
