#include "bsp_log_uart.h"

#include "bsp_board_config.h"
#include "bsp_timebase.h"
#include "bsp_write_protection.h"
#include "hc32_ll.h"

#define LOG_UART_TIMEOUT_MS  10UL

static bool g_log_uart_ready;

bool bsp_log_uart_init(void) {
    stc_usart_uart_init_t config;
    float32_t baudrate_error;

    bsp_write_protection_unlock();
    FCG_Fcg1PeriphClockCmd(BSP_LOG_UART_CLOCK, ENABLE);
    GPIO_SetFunc(BSP_LOG_UART_TX_PORT, BSP_LOG_UART_TX_PIN, BSP_LOG_UART_TX_FUNCTION);
    bsp_write_protection_restore();

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
    if (!g_log_uart_ready || data == NULL)
        return 0U;

    size_t written = 0U;
    while (written < size) {
        const uint32_t started = bsp_millis();
        while (USART_GetStatus(BSP_LOG_UART, USART_FLAG_TX_EMPTY) == RESET) {
            if ((uint32_t)(bsp_millis() - started) >= LOG_UART_TIMEOUT_MS)
                return written;
        }
        USART_WriteData(BSP_LOG_UART, (uint8_t)data[written]);
        ++written;
    }
    return written;
}
