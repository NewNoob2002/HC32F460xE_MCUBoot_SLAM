#include "bsp_panic.h"

#include <string.h>

#include "bsp_log_uart.h"

_Noreturn void bsp_panic(const char* reason) {
    static const char prefix[] = "[panic] ";
    static const char newline[] = "\r\n";

    (void)bsp_log_uart_init();
    (void)bsp_log_uart_write(prefix, sizeof(prefix) - 1U);
    (void)bsp_log_uart_write(reason, strlen(reason));
    (void)bsp_log_uart_write(newline, sizeof(newline) - 1U);
    for (;;) {}
}
