#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "app_diagnostics.h"

static bool g_watchdog_ready;
static bool g_watchdog_pulse_active;
static struct usb_fw_update_status g_usb_status;

bool bsp_external_watchdog_is_ready(void) {
    return g_watchdog_ready;
}

bool bsp_external_watchdog_is_pulse_active(void) {
    return g_watchdog_pulse_active;
}

void usb_fw_update_get_status(struct usb_fw_update_status* status) {
    assert(status != NULL);
    *status = g_usb_status;
}

int main(void) {
    struct power_devices_status power = {.i2c_init_result = 0, .bq_default_probe_result = 0};
    struct app_diagnostics diagnostics;

    app_diagnostics_init(&diagnostics, 1000U, &power, 0, 0);
    assert(diagnostics.snapshot.power.bq_default_probe_result == 0);
    assert(!app_diagnostics_poll(&diagnostics, 5999U));
    assert(diagnostics.snapshot.loop_count == 1U);

    g_watchdog_ready = true;
    g_watchdog_pulse_active = true;
    g_usb_status = (struct usb_fw_update_status){.errors = 2U, .last_result = -1, .manager_state = 3, .configured = 1U};
    assert(app_diagnostics_poll(&diagnostics, 6000U));
    assert(diagnostics.snapshot.uptime_ms == 5000U);
    assert(diagnostics.snapshot.report_count == 1U);
    assert(diagnostics.snapshot.watchdog_ready);
    assert(diagnostics.snapshot.watchdog_pulse_active);
    assert(diagnostics.snapshot.usb.errors == 2U);

    assert(app_diagnostics_poll(&diagnostics, 17000U));
    assert(diagnostics.next_report_ms == 21000U);
    assert(!app_diagnostics_poll(NULL, 0U));
    app_diagnostics_init(NULL, 0U, NULL, 0, 0);
    return 0;
}
