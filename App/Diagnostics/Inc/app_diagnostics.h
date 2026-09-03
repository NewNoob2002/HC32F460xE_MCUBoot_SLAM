#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "power_devices.h"
#include "usb_fw_update.h"

#define APP_DIAGNOSTICS_REPORT_INTERVAL_MS 5000U

struct app_diagnostics_snapshot {
    uint32_t uptime_ms;
    uint32_t loop_count;
    uint32_t report_count;
    bool watchdog_ready;
    bool watchdog_pulse_active;
    int usb_init_result;
    int confirm_result;
    struct usb_fw_update_status usb;
    struct power_devices_status power;
};

struct app_diagnostics {
    uint32_t started_ms;
    uint32_t next_report_ms;
    struct app_diagnostics_snapshot snapshot;
};

void app_diagnostics_init(struct app_diagnostics* diagnostics, uint32_t now_ms,
                          const struct power_devices_status* power, int usb_init_result, int confirm_result);
bool app_diagnostics_poll(struct app_diagnostics* diagnostics, uint32_t now_ms);

#endif
