#include <stddef.h>

#include "bsp_charger_signals.h"
#include "data_proc.h"

static struct data_node g_node;
static struct dp_charger_state g_state;

static void sample(void) {
    struct bsp_charger_signals signals;

    if (!bsp_charger_signals_read(&signals))
        return;
    if (g_state.valid && signals.interrupt_asserted == g_state.interrupt_asserted
        && signals.power_good == g_state.power_good)
        return;
    g_state.valid = true;
    g_state.interrupt_asserted = signals.interrupt_asserted;
    g_state.power_good = signals.power_good;
    ++g_state.change_count;
    (void)data_node_publish(&g_node, &g_state, sizeof(g_state));
}

static int on_event(struct data_node* node, struct data_node_event_param* param) {
    (void)node;
    if (param->event == DATA_NODE_EVENT_TIMER) {
        sample();
        return DATA_NODE_OK;
    }
    return data_node_reply_pull(param, &g_state, sizeof(g_state));
}

void dp_charger_init(struct data_broker* broker, uint32_t now_ms) {
    g_state = (struct dp_charger_state){0};
    if (data_node_init(&g_node, broker, DP_ID_CHARGER, on_event, DATA_NODE_EVENT_PULL | DATA_NODE_EVENT_TIMER, NULL)
        != DATA_NODE_OK)
        return;
    /* Unconfigured GPIOs leave the node valid=false instead of reporting floating levels. */
    if (!bsp_charger_signals_init())
        return;
    sample();
    data_node_start_timer(&g_node, DP_CHARGER_POLL_MS, now_ms);
}
