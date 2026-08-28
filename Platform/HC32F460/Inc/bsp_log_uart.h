#ifndef BSP_LOG_UART_H
#define BSP_LOG_UART_H

#include <stdbool.h>
#include <stddef.h>

bool bsp_log_uart_init(void);
size_t bsp_log_uart_write(const char* data, size_t size);

#endif
