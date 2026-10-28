#include <stddef.h>

#include "data_proc.h"

static struct data_node g_node;
static struct power_devices_status g_state;

static int on_event(struct data_node* node, struct data_node_event_param* param) {
    (void)node;
    return data_node_reply_pull(param, &g_state, sizeof(g_state));
}

void dp_power_init(struct data_broker* broker) {
    power_devices_init(&g_state);
    (void)data_node_init(&g_node, broker, DP_ID_POWER, on_event, DATA_NODE_EVENT_PULL, NULL);
}
