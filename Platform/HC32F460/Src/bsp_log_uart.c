#include "bsp_log_uart.h"

#include "bsp_board_config.h"
#include "hc32_ll.h"

#define BSP_LOG_UART_TIMEOUT 0x40000UL

static bool g_log_uart_ready;

bool bsp_log_uart_init(void) {
    stc_usart_uart_init_t config;
    float32_t baudrate_error;

    if (g_log_uart_ready)
        return true;

    FCG_Fcg1PeriphClockCmd(BSP_LOG_UART_CLOCK, ENABLE);
    GPIO_SetFunc(BSP_LOG_UART_TX_PORT, BSP_LOG_UART_TX_PIN, BSP_LOG_UART_TX_FUNCTION);

    (void)USART_DeInit(BSP_LOG_UART);
    if (USART_UART_StructInit(&config) != LL_OK)
        return false;
    config.u32Baudrate = BSP_LOG_UART_BAUDRATE;
    config.u32HWFlowControl = 0UL;
    if (USART_UART_Init(BSP_LOG_UART, &config, &baudrate_error) != LL_OK)
        return false;
    USART_FuncCmd(BSP_LOG_UART, USART_TX, ENABLE);
    g_log_uart_ready = true;
    return true;
}

size_t bsp_log_uart_write(const char* data, size_t size) {
    if (!g_log_uart_ready || data == NULL || size == 0U || size > UINT32_MAX)
        return 0U;
    return USART_UART_Trans(BSP_LOG_UART, data, (uint32_t)size, BSP_LOG_UART_TIMEOUT) == LL_OK ? size : 0U;
}
