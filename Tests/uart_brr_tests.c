#include "bsp_uart_brr.h"

#include <stdio.h>

/* Literal 64-bit transcription of UART_CalculateBrr() in hc32_ll_usart.c with OVER8 = 0. The driver's UL suffixes
 * are 32-bit on the Cortex-M4 target, so they are written as U here to keep that width on 64-bit hosts. */
static bool reference_brr(uint32_t C, uint32_t B, uint32_t* integer, uint32_t* fraction) {
    uint32_t div_integer = (C / (B * 16U)) - 1U;
    bool fraction_flag = true;
    *fraction = BSP_UART_BRR_NO_FRACTION;
    if (div_integer <= 0xFFUL) {
        *integer = div_integer;
        if ((C % (B * 16U)) == 0U)
            return true;
        const uint64_t temp0 = ((uint64_t)16UL * ((uint64_t)div_integer + 1U) * (uint64_t)B) << 8U;
        const uint32_t div_fraction = (uint32_t)((temp0 + ((uint64_t)C >> 1)) / C) - 128U;
        if (div_fraction <= 0x7FUL) {
            *fraction = div_fraction;
            return true;
        }
        fraction_flag = false;
    }
    if (!fraction_flag) {
        div_integer = ((((C * 10U) / (B * 16U)) + 5U) / 10U) - 1U;
        if (div_integer <= 0xFFUL) {
            *integer = div_integer;
            return true;
        }
    }
    return false;
}

static int failures;

static void check(uint32_t clock, uint32_t baudrate) {
    uint32_t expected_integer = 0U, expected_fraction = 0U, integer = 0U, fraction = 0U;
    const bool expected = reference_brr(clock, baudrate, &expected_integer, &expected_fraction);
    const bool actual = bsp_uart_brr(clock, baudrate, &integer, &fraction);
    if (expected != actual || (expected && (expected_integer != integer || expected_fraction != fraction))) {
        if (++failures <= 10)
            printf("mismatch clock=%u baud=%u ref=%d/%u/%u got=%d/%u/%u\n", clock, baudrate, expected, expected_integer,
                   expected_fraction, actual, integer, fraction);
    }
}

int main(void) {
    uint32_t integer = 0U, fraction = 0U;
    /* Values observed on hardware with the LL driver: PCLK1 100 MHz, 115200 baud -> BRR 0x000035FF. */
    if (!bsp_uart_brr(100000000U, 115200U, &integer, &fraction) || integer != 53U || fraction != 127U) {
        printf("100 MHz/115200 gave %u/%u\n", integer, fraction);
        return 1;
    }
    if (bsp_uart_brr(100000000U, 0U, &integer, &fraction) || bsp_uart_brr(1000U, 115200U, &integer, &fraction))
        return 1;

    /* Clocks stay at or below 400 MHz, where the reference's 32-bit C * 10 does not wrap. */
    static const uint32_t clocks[] = {1000000U,  4000000U,  8000000U,   12500000U,  16000000U,  25000000U,
                                      33333333U, 50000000U, 100000000U, 120000000U, 200000000U, 400000000U};
    for (size_t i = 0U; i < sizeof clocks / sizeof clocks[0]; ++i) {
        for (uint32_t baud = 1U; baud <= 200000U; ++baud)
            check(clocks[i], baud);
        for (uint32_t baud = 200000U; baud <= 30000000U; baud += 997U)
            check(clocks[i], baud);
    }
    uint32_t state = 0x12345678U;
    for (unsigned n = 0U; n < 2000000U; ++n) {
        state = state * 1664525U + 1013904223U;
        const uint32_t clock = 1000000U + state % 399000001U;
        state = state * 1664525U + 1013904223U;
        check(clock, 1U + state % (clock / 16U + 1U));
    }
    if (failures != 0) {
        printf("%d mismatches\n", failures);
        return 1;
    }
    return 0;
}
