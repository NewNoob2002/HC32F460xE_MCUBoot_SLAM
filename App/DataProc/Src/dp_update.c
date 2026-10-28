#include <stddef.h>

#include "data_proc.h"

static struct data_node g_node;
static struct dp_update_state g_state;

static bool publishable_change(const struct usb_fw_update_status* next) {
    /* last_result changes per packet; pull it instead of publishing every transfer step. */
    return next->configured != g_state.usb.configured || next->manager_state != g_state.usb.manager_state
           || next->errors != g_state.usb.errors;
}

static int on_event(struct data_node* node, struct data_node_event_param* param) {
    (void)node;
    return data_node_reply_pull(param, &g_state, sizeof(g_state));
}

int dp_update_init(struct data_broker* broker) {
    g_state = (struct dp_update_state){0};
    (void)data_node_init(&g_node, broker, DP_ID_UPDATE, on_event, DATA_NODE_EVENT_PULL, NULL);
    g_state.init_result = usb_fw_update_init();
    usb_fw_update_get_status(&g_state.usb);
    return g_state.init_result;
}

enum usb_fw_update_action dp_update_poll(uint32_t now_ms) {
    struct usb_fw_update_status next;
    const enum usb_fw_update_action action = usb_fw_update_poll(now_ms);

    usb_fw_update_get_status(&next);
    const bool changed = publishable_change(&next);
    g_state.usb = next;
    if (changed)
        (void)data_node_publish(&g_node, &g_state, sizeof(g_state));
    return action;
}
