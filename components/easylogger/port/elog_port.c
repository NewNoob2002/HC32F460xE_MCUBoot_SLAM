#include <stdio.h>
#include "SEGGER_RTT.h"
#include "boot_timebase.h"
#include "elog.h"
static char time_buf[32];

#if defined(INC_FREERTOS_H)
static bool elog_port_can_use_rtos_lock(void) {
    if (output_lock == NULL) {
        return false;
    }
    if (__get_IPSR() != 0U) {
        return false;
    }
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}
#endif

/**
 * EasyLogger port initialize
 *
 * @return result
 */
ElogErrCode elog_port_init(void) {
#if defined(_WIN32)
    output_lock = CreateMutex(NULL, FALSE, NULL);
#else
    /* add your code here */
    SEGGER_RTT_Init();
#endif //_WIN32
    return ELOG_NO_ERR;
}

/**
 * EasyLogger port deinitialize
 *
 */
void elog_port_deinit(void) {
    /* add your code here */
#if defined(_WIN32)
    CloseHandle(output_lock);
#else
#endif //_WIN32
}

/**
 * output log port interface
 *
 * @param log output of log
 * @param size log size
 */
void elog_port_output(const char* log, const size_t size) {
    /* add your code here */
    SEGGER_RTT_Write(0, log, (unsigned)size);
}

/**
 * output lock
 */
void elog_port_output_lock(void) {
    /* add your code here */
#if defined(_WIN32)
    WaitForSingleObject(output_lock, INFINITE);
#else
#endif //_WIN32
}

/**
 * output unlock
 */
void elog_port_output_unlock(void) {
    /* add your code here */
#if defined(_WIN32)
    ReleaseMutex(output_lock);
#else
#endif //_WIN32
}

/**
 * get current time interface
 *
 * @return current time
 */
const char* elog_port_get_time(void) {
    /* add your code here */
#if defined(_WIN32)
    static char cur_system_time[24] = {0};
    static SYSTEMTIME currTime;

    GetLocalTime(&currTime);
    snprintf(cur_system_time, 24, "%02d-%02d %02d:%02d:%02d.%03d", currTime.wMonth, currTime.wDay, currTime.wHour,
             currTime.wMinute, currTime.wSecond, currTime.wMilliseconds);

    return cur_system_time;
#else
    snprintf(time_buf, sizeof(time_buf), "%lu", (unsigned long)boot_time_ms());
    return time_buf;
#endif //_WIN32
}

/**
 * get current process name interface
 *
 * @return current process name
 */
const char* elog_port_get_p_info(void) {
    /* add your code here */
#if defined(_WIN32)
    static char cur_process_info[10] = {0};
    snprintf(cur_process_info, 10, "pid:%04ld", GetCurrentProcessId());

    return cur_process_info;

#else
    return "";
#endif
}

/**
 * get current thread name interface
 *
 * @return current thread name
 */
const char* elog_port_get_t_info(void) {
    /* add your code here */
#if defined(_WIN32)
    static char cur_thread_info[10] = {0};

    snprintf(cur_thread_info, 10, "tid:%04ld", GetCurrentThreadId());

    return cur_thread_info;

#else
    return "";
#endif
}
