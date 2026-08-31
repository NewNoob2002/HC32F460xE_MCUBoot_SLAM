#include "elog.h"
#include "hc32_debug_log.h"

#include <stdio.h>

#include "bsp_log_uart.h"
#include "bsp_timebase.h"

static char g_time_buffer[16];

ElogErrCode elog_port_init(void) {
    return ELOG_NO_ERR;
}

ElogErrCode elog_port_deinit(void) {
    return ELOG_NO_ERR;
}

void elog_port_output(const char* log, size_t size) {
    (void)bsp_log_uart_write(log, size);
}

void elog_port_output_lock(void) {}

void elog_port_output_unlock(void) {}

const char* elog_port_get_time(void) {
    (void)snprintf(g_time_buffer, sizeof(g_time_buffer), "%lu", (unsigned long)bsp_millis());
    return g_time_buffer;
}

const char* elog_port_get_p_info(void) {
    return "";
}

const char* elog_port_get_t_info(void) {
    return "";
}

bool hc32_debug_log_init(void) {
    if (!bsp_log_uart_init())
        return false;
    if (elog_init() != ELOG_NO_ERR)
        return false;

    elog_set_fmt(ELOG_LVL_ASSERT, ELOG_FMT_ALL);
    elog_set_fmt(ELOG_LVL_ERROR, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_WARN, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_INFO, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_set_fmt(ELOG_LVL_DEBUG, ELOG_FMT_LVL | ELOG_FMT_TAG | ELOG_FMT_TIME);
    elog_start();
    return true;
}
