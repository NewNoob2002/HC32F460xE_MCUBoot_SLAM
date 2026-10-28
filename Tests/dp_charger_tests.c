#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "bsp_charger_signals.h"
#include "data_proc.h"

static bool g_init_ok;
static struct bsp_charger_signals g_signals;
static unsigned int g_publish_count;
static struct dp_charger_state g_published;

bool bsp_charger_signals_init(void) {
    return g_init_ok;
}

bool bsp_charger_signals_read(struct bsp_charger_signals* signals) {
    if (!g_init_ok)
        return false;
    *signals = g_signals;
    return true;
}

static int on_publish(struct data_node* node, struct data_node_event_param* param) {
    (void)node;
    assert(param->event == DATA_NODE_EVENT_PUBLISH);
    assert(param->size == sizeof(g_published));
    memcpy(&g_published, param->data, sizeof(g_published));
    ++g_publish_count;
    return DATA_NODE_OK;
}

int main(void) {
    struct data_broker broker;
    struct data_node sub;
    struct dp_charger_state pulled;

    g_init_ok = true;
    g_signals = (struct bsp_charger_signals){.interrupt_asserted = false, .power_good = true};
    data_broker_init(&broker);
    dp_charger_init(&broker, 0U);
    assert(data_node_init(&sub, &broker, "sub", on_publish, DATA_NODE_EVENT_PUBLISH, NULL) == DATA_NODE_OK);
    assert(data_node_subscribe(&sub, DP_ID_CHARGER) == DATA_NODE_OK);

    /* The first sample happens during init, before any subscriber exists; pull returns it. */
    assert(dp_pull_charger(&sub, &pulled) == DATA_NODE_OK);
    assert(pulled.valid && pulled.power_good && !pulled.interrupt_asserted && pulled.change_count == 1U);

    (void)data_broker_handle_timers(&broker, DP_CHARGER_POLL_MS);
    assert(g_publish_count == 0U);

    g_signals.interrupt_asserted = true;
    (void)data_broker_handle_timers(&broker, DP_CHARGER_POLL_MS * 2U);
    assert(g_publish_count == 1U);
    assert(g_published.interrupt_asserted && g_published.change_count == 2U);

    g_signals.power_good = false;
    (void)data_broker_handle_timers(&broker, DP_CHARGER_POLL_MS * 2U + 1U);
    assert(g_publish_count == 1U);
    (void)data_broker_handle_timers(&broker, DP_CHARGER_POLL_MS * 3U);
    assert(g_publish_count == 2U && !g_published.power_good);

    /* GPIO setup failure leaves the node registered but invalid and untimed. */
    g_init_ok = false;
    data_broker_init(&broker);
    dp_charger_init(&broker, 0U);
    assert(data_node_init(&sub, &broker, "sub", on_publish, DATA_NODE_EVENT_PUBLISH, NULL) == DATA_NODE_OK);
    assert(data_node_subscribe(&sub, DP_ID_CHARGER) == DATA_NODE_OK);
    assert(dp_pull_charger(&sub, &pulled) == DATA_NODE_OK);
    assert(!pulled.valid && pulled.change_count == 0U);
    assert(data_broker_handle_timers(&broker, 1000U) == UINT32_MAX);
    return 0;
}
