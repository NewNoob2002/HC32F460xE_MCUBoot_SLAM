#include <sys/stat.h>

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
    (void)buffer;
    return length;
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
