#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

#include "bsp_log_uart.h"

int _close(int file) {
    (void)file;
    return -1;
}
int _lseek(int file, int offset, int origin) {
    (void)file;
    (void)offset;
    (void)origin;
    return -1;
}
int _read(int file, char* buffer, int length) {
    (void)file;
    (void)buffer;
    (void)length;
    return 0;
}
int _write(int file, const char* buffer, int length) {
    (void)file;
    if (buffer == NULL || length <= 0)
        return 0;
    return (int)bsp_log_uart_write(buffer, (size_t)length);
}
int _fstat(int file, struct stat* status) {
    (void)file;
    status->st_mode = S_IFCHR;
    return 0;
}
int _isatty(int file) {
    (void)file;
    return 1;
}
int _getpid(void) {
    return 1;
}
int _kill(int pid, int signal) {
    (void)pid;
    (void)signal;
    return -1;
}

void* _sbrk(ptrdiff_t increment) {
    /* The linker reserves no heap; never let newlib grow into the stack. */
    (void)increment;
    errno = ENOMEM;
    return (void*)-1;
}

__attribute__((noreturn)) void _exit(int status) {
    (void)status;
    for (;;) {}
}
