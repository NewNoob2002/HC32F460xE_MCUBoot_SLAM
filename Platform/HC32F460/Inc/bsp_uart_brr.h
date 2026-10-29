#ifndef BSP_UART_BRR_H
#define BSP_UART_BRR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_UART_BRR_NO_FRACTION UINT32_C(0xFF)

/**
 * @brief Compute the HC32F460 USART BRR fields for UART mode with 16x oversampling.
 *
 * Reproduces UART_CalculateBrr() in hc32_ll_usart.c, including its rounding and integer-only fallback, using
 * 32-bit integer arithmetic only. The LL path converts through double precision and 64-bit division, which links
 * about 3 KiB of soft-float and libgcc helpers into the 64 KiB Boot.
 *
 * @param clock    USART kernel clock in Hz (bus clock divided by PR).
 * @param baudrate Requested baud rate in bit/s.
 * @param integer  DIV_Integer field.
 * @param fraction DIV_Fraction field, or BSP_UART_BRR_NO_FRACTION when FBME must stay disabled.
 * @return false when the baud rate cannot be represented.
 */
static inline bool bsp_uart_brr(uint32_t clock, uint32_t baudrate, uint32_t* integer, uint32_t* fraction) {
    if (baudrate == 0U || baudrate > UINT32_MAX / 16U || integer == NULL || fraction == NULL)
        return false;
    const uint32_t divisor = 16U * baudrate;
    if (clock < divisor)
        return false;
    *integer = clock / divisor - 1U;
    *fraction = BSP_UART_BRR_NO_FRACTION;
    if (*integer > 0xFFU)
        return false;

    const uint32_t remainder = clock % divisor;
    if (remainder == 0U)
        return true;
    /* LL: fraction = (256 * M + C / 2) / C - 128 with M = divisor * (integer + 1) = C - remainder, which equals
     * 128 - ceil((256 * remainder - C / 2) / C). Long-divide 256 * remainder by C (remainder < C) to stay in 32 bits;
     * a non-positive numerator yields 128 or more, outside DIV_Fraction. */
    const uint32_t half = clock / 2U;
    uint32_t quotient = 0U;
    uint32_t rest = remainder;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        const bool carry = (rest >> 31) != 0U;
        rest <<= 1;
        quotient <<= 1;
        if (carry || rest >= clock) {
            rest -= clock;
            quotient |= 1U;
        }
    }
    if (quotient != 0U || rest > half) {
        const uint32_t below = quotient + (rest > half ? 1U : 0U);
        if (below <= 128U) {
            *fraction = 128U - below;
            return true;
        }
    }

    /* Fraction unusable: LL rounds the integer divider instead (it requires clock * 10 to fit in 32 bits). */
    if (clock > UINT32_MAX / 10U)
        return false;
    const uint32_t rounded = (clock * 10U / divisor + 5U) / 10U;
    if (rounded == 0U || rounded - 1U > 0xFFU)
        return false;
    *integer = rounded - 1U;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif
