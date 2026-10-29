#include "bsp_panic.h"

#include <stddef.h>
#include <string.h>

#include "bsp_log_uart.h"
#include "bsp_status_led.h"

_Noreturn void bsp_panic(const char* reason) {
    static const char prefix[] = "[panic] ";
    static const char newline[] = "\r\n";

    (void)bsp_status_led_init();
    bsp_status_led_set_mode(BSP_STATUS_LED_MODE_ERROR);
    (void)bsp_log_uart_init();
    (void)bsp_log_uart_write(prefix, sizeof(prefix) - 1U);
    (void)bsp_log_uart_write(reason, strlen(reason));
    (void)bsp_log_uart_write(newline, sizeof(newline) - 1U);
    for (;;) {}
}

/* Replaces newlib's __assert_func(), which reports through fiprintf(stderr) and links about 2 KiB of stdio into
 * Boot; MCUboot ASSERT() maps to assert(). Report "assert <file>:<line>" through the same UART/LED panic path. */
_Noreturn void __assert_func(const char* file, int line, const char* func, const char* expression) {
    static char reason[96];
    static const char prefix[] = "assert ";
    char digits[10];
    size_t length = sizeof(prefix) - 1U;
    size_t count = 0U;
    unsigned value = line > 0 ? (unsigned)line : 0U;

    (void)func;
    (void)expression;
    memcpy(reason, prefix, length);
    if (file != NULL) {
        const size_t file_length = strnlen(file, sizeof(reason) - length - sizeof(digits) - 2U);
        memcpy(&reason[length], file, file_length);
        length += file_length;
    }
    reason[length++] = ':';
    do {
        digits[count++] = (char)('0' + value % 10U);
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count != 0U)
        reason[length++] = digits[--count];
    reason[length] = '\0';
    bsp_panic(reason);
}
