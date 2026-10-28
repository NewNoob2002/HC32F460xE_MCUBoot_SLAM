#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "app_diagnostics.h"

static struct data_node g_power_node;
static struct data_node g_charger_node;
static struct data_node g_update_node;
static struct data_node g_system_node;
static struct power_devices_status g_power;
static struct dp_charger_state g_charger;
static struct dp_update_state g_update;
static struct dp_system_state g_system;

static int on_pull(struct data_node* node, struct data_node_event_param* param) {
    if (node == &g_power_node)
        return data_node_reply_pull(param, &g_power, sizeof(g_power));
    if (node == &g_charger_node)
        return data_node_reply_pull(param, &g_charger, sizeof(g_charger));
    if (node == &g_update_node)
        return data_node_reply_pull(param, &g_update, sizeof(g_update));
    return data_node_reply_pull(param, &g_system, sizeof(g_system));
}

static void register_publishers(struct data_broker* broker) {
    assert(data_node_init(&g_power_node, broker, DP_ID_POWER, on_pull, DATA_NODE_EVENT_PULL, NULL) == DATA_NODE_OK);
    assert(data_node_init(&g_charger_node, broker, DP_ID_CHARGER, on_pull, DATA_NODE_EVENT_PULL, NULL) == DATA_NODE_OK);
    assert(data_node_init(&g_update_node, broker, DP_ID_UPDATE, on_pull, DATA_NODE_EVENT_PULL, NULL) == DATA_NODE_OK);
    assert(data_node_init(&g_system_node, broker, DP_ID_SYSTEM, on_pull, DATA_NODE_EVENT_PULL, NULL) == DATA_NODE_OK);
}

int main(void) {
    struct data_broker broker;
    struct app_diagnostics diagnostics;
    struct dp_charger_state charger_change = {.valid = true, .power_good = true, .change_count = 2U};
    uint32_t wrong_size = 0U;

    data_broker_init(&broker);
    register_publishers(&broker);
    g_power = (struct power_devices_status){.i2c_init_result = 0, .bq_default_probe_result = -8};
    g_update = (struct dp_update_state){.init_result = 0, .usb = {.manager_state = 1}};
    g_system = (struct dp_system_state){.confirm_result = 0, .loop_count = 3U, .watchdog_ready = true};
    g_charger = (struct dp_charger_state){.valid = true, .interrupt_asserted = true, .change_count = 1U};

    assert(app_diagnostics_init(&diagnostics, &broker, 1000U));
    assert(data_node_publisher_count(&diagnostics.node) == 4U);
    assert(diagnostics.snapshot.power.bq_default_probe_result == -8);
    assert(diagnostics.snapshot.system.loop_count == 3U);
    assert(diagnostics.snapshot.charger.interrupt_asserted);
    assert(diagnostics.snapshot.update.usb.manager_state == 1);
    assert(!app_diagnostics_init(&diagnostics, &broker, 1000U));

    assert(data_broker_handle_timers(&broker, 5999U) == 1U);
    assert(diagnostics.snapshot.report_count == 0U);

    g_system.loop_count = 99U;
    g_system.watchdog_pulse_active = true;
    g_update.usb = (struct usb_fw_update_status){.errors = 2U, .last_result = -1, .manager_state = 3, .configured = 1U};
    assert(data_broker_handle_timers(&broker, 6000U) == APP_DIAGNOSTICS_REPORT_INTERVAL_MS);
    assert(diagnostics.snapshot.report_count == 1U);
    assert(diagnostics.snapshot.uptime_ms == 5000U);
    assert(diagnostics.snapshot.system.loop_count == 99U);
    assert(diagnostics.snapshot.system.watchdog_pulse_active);
    assert(diagnostics.snapshot.update.usb.errors == 2U);

    /* Charger publications update the snapshot between reports. */
    assert(data_node_publish(&g_charger_node, &charger_change, sizeof(charger_change)) == DATA_NODE_OK);
    assert(diagnostics.snapshot.charger.power_good && !diagnostics.snapshot.charger.interrupt_asserted);
    assert(diagnostics.snapshot.charger.change_count == 2U);
    assert(data_node_publish(&g_charger_node, &wrong_size, sizeof(wrong_size)) == DATA_NODE_ERR_SIZE_MISMATCH);
    assert(diagnostics.snapshot.charger.change_count == 2U);

    assert(!app_diagnostics_init(NULL, &broker, 0U));
    return 0;
}
