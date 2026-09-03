#include "app_diagnostics.h"

#include <stddef.h>

#include "bsp_external_watchdog.h"
#include "core_debug.h"

void app_diagnostics_init(struct app_diagnostics* diagnostics, uint32_t now_ms,
                          const struct power_devices_status* power, int usb_init_result, int confirm_result) {
    if (diagnostics == NULL)
        return;

    *diagnostics = (struct app_diagnostics){
        .started_ms = now_ms,
        .next_report_ms = now_ms + APP_DIAGNOSTICS_REPORT_INTERVAL_MS,
        .snapshot =
            {
                .usb_init_result = usb_init_result,
                .confirm_result = confirm_result,
            },
    };
    if (power != NULL)
        diagnostics->snapshot.power = *power;

    CORE_DEBUG_PRINTF("startup usb=%d confirm=%d", usb_init_result, confirm_result);
}

bool app_diagnostics_poll(struct app_diagnostics* diagnostics, uint32_t now_ms) {
    uint32_t late_ms;
    uint32_t periods;

    if (diagnostics == NULL)
        return false;

    ++diagnostics->snapshot.loop_count;
    diagnostics->snapshot.uptime_ms = now_ms - diagnostics->started_ms;
    diagnostics->snapshot.watchdog_ready = bsp_external_watchdog_is_ready();
    diagnostics->snapshot.watchdog_pulse_active = bsp_external_watchdog_is_pulse_active();
    usb_fw_update_get_status(&diagnostics->snapshot.usb);

    if ((int32_t)(now_ms - diagnostics->next_report_ms) < 0)
        return false;

    late_ms = now_ms - diagnostics->next_report_ms;
    periods = (late_ms / APP_DIAGNOSTICS_REPORT_INTERVAL_MS) + 1U;
    diagnostics->next_report_ms += periods * APP_DIAGNOSTICS_REPORT_INTERVAL_MS;
    ++diagnostics->snapshot.report_count;
    CORE_DEBUG_PRINTF("diag uptime=%lu loops=%lu wdog=%u pulse=%u usb_cfg=%u usb_state=%d usb_err=%lu usb_last=%d",
                      (unsigned long)diagnostics->snapshot.uptime_ms, (unsigned long)diagnostics->snapshot.loop_count,
                      diagnostics->snapshot.watchdog_ready ? 1U : 0U,
                      diagnostics->snapshot.watchdog_pulse_active ? 1U : 0U,
                      (unsigned int)diagnostics->snapshot.usb.configured, diagnostics->snapshot.usb.manager_state,
                      (unsigned long)diagnostics->snapshot.usb.errors, diagnostics->snapshot.usb.last_result);
    return true;
}
