#ifndef APP_DIAGNOSTICS_H
#define APP_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "data_broker.h"
#include "data_proc.h"

#define APP_DIAGNOSTICS_ID                 "Diagnostics"
#define APP_DIAGNOSTICS_REPORT_INTERVAL_MS 5000U

struct app_diagnostics_snapshot {
    uint32_t uptime_ms;
    uint32_t report_count;
    struct dp_system_state system;
    struct dp_update_state update;
    struct dp_charger_state charger;
    struct power_devices_status power;
};

struct app_diagnostics {
    struct data_node node;
    uint32_t started_ms;
    struct app_diagnostics_snapshot snapshot;
};

/* Subscribes to every DataProc node; call after those nodes are initialized. */
bool app_diagnostics_init(struct app_diagnostics* diagnostics, struct data_broker* broker, uint32_t now_ms);

#endif
