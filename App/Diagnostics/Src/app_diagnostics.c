#include "app_diagnostics.h"

#include <stddef.h>
#include <string.h>

#include "core_debug.h"

static void log_charger(const struct dp_charger_state* charger) {
    (void)charger; /* CORE_DEBUG_PRINTF compiles out in Release. */
    CORE_DEBUG_PRINTF("charger valid=%u int=%u pg=%u changes=%lu", charger->valid ? 1U : 0U,
                      charger->interrupt_asserted ? 1U : 0U, charger->power_good ? 1U : 0U,
                      (unsigned long)charger->change_count);
}

static void report(struct app_diagnostics* diagnostics, uint32_t now_ms) {
    struct app_diagnostics_snapshot* snapshot = &diagnostics->snapshot;

    /* Failed pulls keep the last good value; startup already logged missing nodes. */
    (void)dp_pull_system(&diagnostics->node, &snapshot->system);
    (void)dp_pull_update(&diagnostics->node, &snapshot->update);
    (void)dp_pull_charger(&diagnostics->node, &snapshot->charger);
    snapshot->uptime_ms = now_ms - diagnostics->started_ms;
    ++snapshot->report_count;
    CORE_DEBUG_PRINTF("diag uptime=%lu loops=%lu wdog=%u pulse=%u usb_cfg=%u usb_state=%d usb_err=%lu usb_last=%d "
                      "chg_valid=%u chg_int=%u chg_pg=%u",
                      (unsigned long)snapshot->uptime_ms, (unsigned long)snapshot->system.loop_count,
                      snapshot->system.watchdog_ready ? 1U : 0U, snapshot->system.watchdog_pulse_active ? 1U : 0U,
                      (unsigned int)snapshot->update.usb.configured, snapshot->update.usb.manager_state,
                      (unsigned long)snapshot->update.usb.errors, snapshot->update.usb.last_result,
                      snapshot->charger.valid ? 1U : 0U, snapshot->charger.interrupt_asserted ? 1U : 0U,
                      snapshot->charger.power_good ? 1U : 0U);
}

static int on_event(struct data_node* node, struct data_node_event_param* param) {
    struct app_diagnostics* diagnostics = node->user_data;

    switch (param->event) {
        case DATA_NODE_EVENT_TIMER:
            report(diagnostics, *(const uint32_t*)param->data);
            return DATA_NODE_OK;
        case DATA_NODE_EVENT_PUBLISH:
            if (strcmp(param->tran->id, DP_ID_CHARGER) == 0 && param->size == sizeof(diagnostics->snapshot.charger)) {
                memcpy(&diagnostics->snapshot.charger, param->data, param->size);
                log_charger(&diagnostics->snapshot.charger);
                return DATA_NODE_OK;
            }
            if (strcmp(param->tran->id, DP_ID_UPDATE) == 0 && param->size == sizeof(diagnostics->snapshot.update)) {
                memcpy(&diagnostics->snapshot.update, param->data, param->size);
                return DATA_NODE_OK;
            }
            return DATA_NODE_ERR_SIZE_MISMATCH;
        default:
            return DATA_NODE_ERR_UNSUPPORTED_REQUEST;
    }
}

bool app_diagnostics_init(struct app_diagnostics* diagnostics, struct data_broker* broker, uint32_t now_ms) {
    static const char* const publishers[] = {DP_ID_POWER, DP_ID_CHARGER, DP_ID_UPDATE, DP_ID_SYSTEM};

    if (diagnostics == NULL)
        return false;
    /* Initialize the node first: a duplicate registration must not wipe the live node's links. */
    if (data_node_init(&diagnostics->node, broker, APP_DIAGNOSTICS_ID, on_event,
                       DATA_NODE_EVENT_PUBLISH | DATA_NODE_EVENT_TIMER, diagnostics)
        != DATA_NODE_OK)
        return false;
    diagnostics->started_ms = now_ms;
    diagnostics->snapshot = (struct app_diagnostics_snapshot){0};
    for (size_t i = 0U; i < sizeof(publishers) / sizeof(publishers[0]); ++i) {
        const int result = data_node_subscribe(&diagnostics->node, publishers[i]);
        (void)result;
        if (result != DATA_NODE_OK)
            CORE_DEBUG_PRINTF("diag subscribe %s failed: %d", publishers[i], result);
    }

    (void)dp_pull_power(&diagnostics->node, &diagnostics->snapshot.power);
    (void)dp_pull_system(&diagnostics->node, &diagnostics->snapshot.system);
    (void)dp_pull_update(&diagnostics->node, &diagnostics->snapshot.update);
    (void)dp_pull_charger(&diagnostics->node, &diagnostics->snapshot.charger);
    CORE_DEBUG_PRINTF("startup usb=%d confirm=%d", diagnostics->snapshot.update.init_result,
                      diagnostics->snapshot.system.confirm_result);
    log_charger(&diagnostics->snapshot.charger);
    data_node_start_timer(&diagnostics->node, APP_DIAGNOSTICS_REPORT_INTERVAL_MS, now_ms);
    return true;
}
