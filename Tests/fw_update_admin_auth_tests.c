#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "fw_update/admin_auth_ecdsa.h"

/* Test-only key (private scalar 0x01..0x20, see Tools/updater/src/admin.rs). The signature is what the Rust updater
 * produces for challenge 0x40..0x5f, so this checks host signing against device verification. */
static const uint8_t public_key[64] = {
    0x51, 0x5c, 0x3d, 0x6e, 0xb9, 0xe3, 0x96, 0xb9, 0x04, 0xd3, 0xfe, 0xca, 0x7f, 0x54, 0xfd, 0xcd,
    0x0c, 0xc1, 0xe9, 0x97, 0xbf, 0x37, 0x5d, 0xca, 0x51, 0x5a, 0xd0, 0xa6, 0xc3, 0xb4, 0x03, 0x5f,
    0x45, 0x36, 0xbe, 0x3a, 0x50, 0xf3, 0x18, 0xfb, 0xf9, 0xa5, 0x47, 0x59, 0x02, 0xa2, 0x21, 0x50,
    0x2b, 0xef, 0x0d, 0x57, 0xe0, 0x8c, 0x53, 0xb2, 0xcc, 0x0a, 0x56, 0xf1, 0x7d, 0x9f, 0x93, 0x54,
};
static const uint8_t signature[64] = {
    0x53, 0x3c, 0xd3, 0xc9, 0x2f, 0xa8, 0xa6, 0xc9, 0x25, 0xaf, 0xa4, 0xb4, 0x44, 0x23, 0x68, 0xda,
    0x67, 0xc6, 0xb9, 0x94, 0xd2, 0x32, 0x20, 0xc7, 0xc6, 0x62, 0x61, 0xad, 0x9f, 0x71, 0xba, 0x3d,
    0x3c, 0x8e, 0x38, 0xb8, 0xd9, 0x3a, 0xa3, 0x44, 0x3d, 0xcf, 0xc0, 0x15, 0x4d, 0x47, 0xda, 0xdb,
    0xb8, 0x7f, 0x4a, 0x03, 0x67, 0xe2, 0xb3, 0x6c, 0xc2, 0xdf, 0xef, 0x54, 0x17, 0x38, 0x16, 0xee,
};

static unsigned int random_calls;
static bool random_result = true;

static bool fake_random(uint8_t* output, size_t length) {
    ++random_calls;
    memset(output, 0x5A, length);
    return random_result;
}

int main(void) {
    struct fw_update_admin_auth auth = {0};
    uint8_t challenge[FW_UPDATE_ADMIN_CHALLENGE_SIZE];
    uint8_t tampered[FW_UPDATE_ADMIN_SIGNATURE_SIZE];
    uint8_t off_curve[FW_UPDATE_ADMIN_PUBLIC_KEY_SIZE];

    memcpy(off_curve, public_key, sizeof(off_curve));
    off_curve[63] ^= 1U;
    assert(fw_update_admin_auth_ecdsa_init(&auth, off_curve, fake_random) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_admin_auth_ecdsa_init(&auth, public_key, NULL) == FW_UPDATE_ERR_INVALID_ARGUMENT);
    assert(fw_update_admin_auth_ecdsa_init(&auth, public_key, fake_random) == FW_UPDATE_OK);

    assert(fw_update_admin_auth_generate_challenge(&auth, challenge) == FW_UPDATE_OK && random_calls == 1U);
    random_result = false;
    assert(fw_update_admin_auth_generate_challenge(&auth, challenge) == FW_UPDATE_ERR_IO);

    for (size_t index = 0U; index < sizeof(challenge); ++index)
        challenge[index] = (uint8_t)(0x40U + index);
    assert(fw_update_admin_auth_verify(&auth, challenge, signature) == FW_UPDATE_OK);

    memcpy(tampered, signature, sizeof(tampered));
    tampered[40] ^= 0x80U;
    assert(fw_update_admin_auth_verify(&auth, challenge, tampered) != FW_UPDATE_OK);
    challenge[0] ^= 1U;
    assert(fw_update_admin_auth_verify(&auth, challenge, signature) != FW_UPDATE_OK);
    return 0;
}
