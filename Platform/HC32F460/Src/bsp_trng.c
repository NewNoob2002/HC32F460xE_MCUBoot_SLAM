#include "bsp_trng.h"

#include <string.h>

#include "hc32_ll.h"

/* Per-sample busy-wait bound, the vendor TRNG_TIMEOUT. TRNG_Init()/TRNG_GenerateRandom() are not used: the vendor
 * loop never exits after a timeout (its counter wraps and the sample count never advances), so a stalled TRNG would
 * hang Boot before USB recovery starts instead of just disabling administrator mode. */
#define TRNG_SAMPLE_TIMEOUT 20000UL
/* The vendor init discards the first 10 captures (20 words); keep the same warm-up. */
#define TRNG_WARMUP_SAMPLES 10U

static bool trng_ready;

static bool sample(uint32_t words[2]) {
    TRNG_Start();
    for (uint32_t i = 0U; i < TRNG_SAMPLE_TIMEOUT; ++i) {
        if (READ_REG32(bCM_TRNG->CR_b.RUN) == 0U)
            return TRNG_GetRandom(words, 2U) == LL_OK;
    }
    return false;
}

bool bsp_trng_init(void) {
    uint32_t words[2];

    trng_ready = false;
    FCG_Fcg0PeriphClockCmd(FCG0_PERIPH_TRNG, ENABLE);
    /* 64 shifts per capture and reload of the initial value follow the vendor example (TRNG_Init()). */
    MODIFY_REG32(CM_TRNG->MR, TRNG_MR_CNT | TRNG_MR_LOAD, TRNG_SHIFT_CNT64 | TRNG_RELOAD_INIT_VAL_ENABLE);
    TRNG_Cmd(ENABLE);
    for (uint32_t i = 0U; i < TRNG_WARMUP_SAMPLES; ++i) {
        if (!sample(words)) {
            TRNG_Cmd(DISABLE);
            return false;
        }
    }
    memset(words, 0, sizeof(words));
    trng_ready = true;
    return true;
}

bool bsp_trng_read(uint8_t* output, size_t length) {
    uint32_t words[2];

    if (!trng_ready || (output == NULL && length != 0U))
        return false;
    while (length != 0U) {
        const size_t chunk = length < sizeof(words) ? length : sizeof(words);
        if (!sample(words)) {
            memset(words, 0, sizeof(words));
            return false;
        }
        memcpy(output, words, chunk);
        output += chunk;
        length -= chunk;
    }
    memset(words, 0, sizeof(words));
    return true;
}
