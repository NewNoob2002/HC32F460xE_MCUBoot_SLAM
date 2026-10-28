#include <stddef.h>

#include "app_confirm.h"
#include "bsp_external_watchdog.h"
#include "data_proc.h"

static struct data_node g_node;
static struct dp_system_state g_state;

static int on_event(struct data_node* node, struct data_node_event_param* param) {
    (void)node;
    g_state.watchdog_ready = bsp_external_watchdog_is_ready();
    g_state.watchdog_pulse_active = bsp_external_watchdog_is_pulse_active();
    return data_node_reply_pull(param, &g_state, sizeof(g_state));
}

bool dp_system_init(struct data_broker* broker, uint32_t now_ms) {
    g_state = (struct dp_system_state){0};
    (void)data_node_init(&g_node, broker, DP_ID_SYSTEM, on_event, DATA_NODE_EVENT_PULL, NULL);
    return bsp_external_watchdog_init(now_ms);
}

void dp_system_poll(uint32_t now_ms) {
    ++g_state.loop_count;
    bsp_external_watchdog_poll(now_ms);
}

int dp_system_confirm(bool auto_confirm) {
    g_state.confirm_result = app_confirm_running_image(auto_confirm);
    return g_state.confirm_result;
}
