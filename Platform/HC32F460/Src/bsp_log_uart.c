#include "bsp_log_uart.h"

#include "bsp_board_config.h"
#include "bsp_uart_brr.h"
#include "hc32_ll.h"

#define BSP_LOG_UART_TIMEOUT 0x40000UL

static bool g_log_uart_ready;

/* USART_UART_Init() always links USART_SetBaudrate(), whose double-precision BRR math costs about 3 KiB in the
 * 64 KiB Boot. Program the same 8N1, 16x oversampling, no-flow-control UART directly; bsp_uart_brr() reproduces the
 * LL divider. USARTs run from PCLK1, as in hc32_ll_usart.c USART_GetBusClockFreq(), and PR stays at DIV1. */
bool bsp_log_uart_init(void) {
    uint32_t integer;
    uint32_t fraction;

    if (g_log_uart_ready)
        return true;

    FCG_Fcg1PeriphClockCmd(BSP_LOG_UART_CLOCK, ENABLE);
    GPIO_SetFunc(BSP_LOG_UART_TX_PORT, BSP_LOG_UART_TX_PIN, BSP_LOG_UART_TX_FUNCTION);

    (void)USART_DeInit(BSP_LOG_UART);
    const uint32_t clock = SystemCoreClock >> (READ_REG32_BIT(CM_CMU->SCFGR, CMU_SCFGR_PCLK1S) >> CMU_SCFGR_PCLK1S_POS);
    if (!bsp_uart_brr(clock, BSP_LOG_UART_BAUDRATE, &integer, &fraction))
        return false;
    WRITE_REG32(BSP_LOG_UART->CR1, USART_PARITY_NONE | USART_DATA_WIDTH_8BIT | USART_FIRST_BIT_LSB
                                       | USART_OVER_SAMPLE_16BIT | USART_START_BIT_FALLING);
    WRITE_REG32(BSP_LOG_UART->CR2, USART_CLK_SRC_INTERNCLK | USART_CK_OUTPUT_DISABLE | USART_STOPBIT_1BIT);
    WRITE_REG32(BSP_LOG_UART->CR3, 0UL);
    WRITE_REG32(BSP_LOG_UART->PR, USART_CLK_DIV1);
    MODIFY_REG32(BSP_LOG_UART->BRR, USART_BRR_DIV_INTEGER | USART_BRR_DIV_FRACTION,
                 (fraction & USART_BRR_DIV_FRACTION) | (integer << USART_BRR_DIV_INTEGER_POS));
    if (fraction != BSP_UART_BRR_NO_FRACTION)
        SET_REG32_BIT(BSP_LOG_UART->CR1, USART_CR1_FBME);
    USART_FuncCmd(BSP_LOG_UART, USART_TX, ENABLE);
    g_log_uart_ready = true;
    return true;
}

size_t bsp_log_uart_write(const char* data, size_t size) {
    if (!g_log_uart_ready || data == NULL || size == 0U || size > UINT32_MAX)
        return 0U;
    return USART_UART_Trans(BSP_LOG_UART, data, (uint32_t)size, BSP_LOG_UART_TIMEOUT) == LL_OK ? size : 0U;
}
