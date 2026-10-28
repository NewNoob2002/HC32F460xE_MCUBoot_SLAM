#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "data_broker.h"

struct probe {
    unsigned int publish_count;
    unsigned int pull_count;
    unsigned int notify_count;
    unsigned int timer_count;
    uint32_t last_value;
    uint32_t last_timer_ms;
    int publish_result;
    bool unsubscribe_on_publish;
};

static int on_event(struct data_node* node, struct data_node_event_param* param) {
    struct probe* probe = node->user_data;

    assert(param->recv == node);
    switch (param->event) {
        case DATA_NODE_EVENT_PUBLISH:
            ++probe->publish_count;
            memcpy(&probe->last_value, param->data, sizeof(probe->last_value));
            if (probe->unsubscribe_on_publish)
                assert(data_node_unsubscribe(node, param->tran->id) == DATA_NODE_OK);
            return probe->publish_result;
        case DATA_NODE_EVENT_PULL: {
            static const uint32_t value = 0xA5A5U;
            ++probe->pull_count;
            return data_node_reply_pull(param, &value, sizeof(value));
        }
        case DATA_NODE_EVENT_NOTIFY:
            ++probe->notify_count;
            memcpy(&probe->last_value, param->data, sizeof(probe->last_value));
            return DATA_NODE_OK;
        case DATA_NODE_EVENT_TIMER:
            ++probe->timer_count;
            memcpy(&probe->last_timer_ms, param->data, sizeof(probe->last_timer_ms));
            return DATA_NODE_OK;
        default:
            return DATA_NODE_ERR_UNKNOWN;
    }
}

static void test_registration(void) {
    struct data_broker broker;
    static struct data_node nodes[DATA_BROKER_MAX_NODES + 1U];
    static char ids[DATA_BROKER_MAX_NODES + 1U][4];
    struct data_node duplicate;

    data_broker_init(&broker);
    for (size_t i = 0U; i < DATA_BROKER_MAX_NODES; ++i) {
        ids[i][0] = 'n';
        ids[i][1] = (char)('a' + i);
        ids[i][2] = '\0';
        assert(data_node_init(&nodes[i], &broker, ids[i], on_event, DATA_NODE_EVENT_ALL, NULL) == DATA_NODE_OK);
    }
    assert(data_broker_node_count(&broker) == DATA_BROKER_MAX_NODES);
    assert(data_broker_search(&broker, "nb") == &nodes[1]);
    assert(data_broker_search(&broker, "missing") == NULL);
    assert(data_node_init(&duplicate, &broker, "na", on_event, DATA_NODE_EVENT_ALL, NULL) == DATA_NODE_ERR_DUPLICATE);
    assert(data_node_init(&nodes[DATA_BROKER_MAX_NODES], &broker, "full", on_event, DATA_NODE_EVENT_ALL, NULL)
           == DATA_NODE_ERR_CAPACITY);
    assert(data_node_subscribe(&nodes[DATA_BROKER_MAX_NODES], "na") == DATA_NODE_ERR_PARAM);
    assert(data_node_init(&duplicate, &broker, "", on_event, DATA_NODE_EVENT_ALL, NULL) == DATA_NODE_ERR_PARAM);
    assert(data_node_init(NULL, &broker, "x", on_event, DATA_NODE_EVENT_ALL, NULL) == DATA_NODE_ERR_PARAM);
}

static void test_publish_pull_notify(void) {
    struct data_broker broker;
    struct data_node pub;
    struct data_node sub_a;
    struct data_node sub_b;
    struct probe pub_probe = {0};
    struct probe probe_a = {0};
    struct probe probe_b = {0};
    uint32_t value = 7U;
    uint32_t pulled = 0U;
    uint16_t wrong_size = 0U;

    data_broker_init(&broker);
    assert(data_node_init(&pub, &broker, "pub", on_event, DATA_NODE_EVENT_PULL | DATA_NODE_EVENT_NOTIFY, &pub_probe)
           == DATA_NODE_OK);
    assert(data_node_init(&sub_a, &broker, "a", on_event, DATA_NODE_EVENT_PUBLISH, &probe_a) == DATA_NODE_OK);
    assert(data_node_init(&sub_b, &broker, "b", on_event, DATA_NODE_EVENT_PUBLISH, &probe_b) == DATA_NODE_OK);

    assert(data_node_subscribe(&sub_a, "pub") == DATA_NODE_OK);
    assert(data_node_subscribe(&sub_a, "pub") == DATA_NODE_ERR_DUPLICATE);
    assert(data_node_subscribe(&sub_a, "a") == DATA_NODE_ERR_PARAM);
    assert(data_node_subscribe(&sub_a, "missing") == DATA_NODE_ERR_NOT_FOUND);
    assert(data_node_subscribe(&sub_b, "pub") == DATA_NODE_OK);
    assert(data_node_subscriber_count(&pub) == 2U);
    assert(data_node_publisher_count(&sub_a) == 1U);

    assert(data_node_publish(&pub, &value, sizeof(value)) == DATA_NODE_OK);
    assert(probe_a.publish_count == 1U && probe_a.last_value == 7U);
    assert(probe_b.publish_count == 1U && probe_b.last_value == 7U);
    assert(data_node_publish(&pub, NULL, sizeof(value)) == DATA_NODE_ERR_PARAM);

    /* STOP_PROCESS ends the fan-out in subscription order. */
    probe_a.publish_result = DATA_NODE_STOP_PROCESS;
    value = 8U;
    assert(data_node_publish(&pub, &value, sizeof(value)) == DATA_NODE_STOP_PROCESS);
    assert(probe_a.publish_count == 2U && probe_b.publish_count == 1U);
    probe_a.publish_result = DATA_NODE_OK;

    /* A subscriber error is reported without stopping later subscribers. */
    probe_a.publish_result = DATA_NODE_ERR_NO_DATA;
    assert(data_node_publish(&pub, &value, sizeof(value)) == DATA_NODE_ERR_NO_DATA);
    assert(probe_b.publish_count == 2U);
    probe_a.publish_result = DATA_NODE_OK;

    assert(data_node_pull(&sub_a, "pub", &pulled, sizeof(pulled)) == DATA_NODE_OK);
    assert(pulled == 0xA5A5U && pub_probe.pull_count == 1U);
    assert(data_node_pull(&sub_a, "pub", &wrong_size, sizeof(wrong_size)) == DATA_NODE_ERR_SIZE_MISMATCH);
    assert(data_node_pull(&pub, "a", &pulled, sizeof(pulled)) == DATA_NODE_ERR_NOT_FOUND);
    /* Event filters reject requests a node did not opt into. */
    assert(data_node_subscribe(&pub, "a") == DATA_NODE_OK);
    assert(data_node_pull(&pub, "a", &pulled, sizeof(pulled)) == DATA_NODE_ERR_UNSUPPORTED_REQUEST);

    value = 42U;
    assert(data_node_notify(&sub_b, "pub", &value, sizeof(value)) == DATA_NODE_OK);
    assert(pub_probe.notify_count == 1U && pub_probe.last_value == 42U);

    /* Unsubscribing during fan-out does not skip or repeat the remaining subscribers. */
    probe_a.unsubscribe_on_publish = true;
    assert(data_node_publish(&pub, &value, sizeof(value)) == DATA_NODE_OK);
    assert(probe_a.publish_count == 4U && probe_b.publish_count == 3U);
    assert(data_node_subscriber_count(&pub) == 1U);
    assert(data_node_pull(&sub_a, "pub", &pulled, sizeof(pulled)) == DATA_NODE_ERR_NOT_FOUND);
    assert(data_node_unsubscribe(&sub_a, "pub") == DATA_NODE_ERR_NOT_FOUND);
    assert(data_node_publish(&pub, &value, sizeof(value)) == DATA_NODE_OK);
    assert(probe_a.publish_count == 4U && probe_b.publish_count == 4U);
}

static void test_link_capacity(void) {
    struct data_broker broker;
    static struct data_node pubs[DATA_NODE_MAX_LINKS + 1U];
    static char ids[DATA_NODE_MAX_LINKS + 1U][4];
    struct data_node sub;

    data_broker_init(&broker);
    assert(data_node_init(&sub, &broker, "sub", on_event, DATA_NODE_EVENT_ALL, NULL) == DATA_NODE_OK);
    for (size_t i = 0U; i <= DATA_NODE_MAX_LINKS; ++i) {
        ids[i][0] = 'p';
        ids[i][1] = (char)('a' + i);
        ids[i][2] = '\0';
        assert(data_node_init(&pubs[i], &broker, ids[i], on_event, DATA_NODE_EVENT_ALL, NULL) == DATA_NODE_OK);
        assert(data_node_subscribe(&sub, ids[i]) == (i < DATA_NODE_MAX_LINKS ? DATA_NODE_OK : DATA_NODE_ERR_CAPACITY));
    }
    assert(data_node_publisher_count(&sub) == DATA_NODE_MAX_LINKS);
    assert(data_node_subscriber_count(&pubs[DATA_NODE_MAX_LINKS]) == 0U);
}

static void test_timers(void) {
    struct data_broker broker;
    struct data_node fast;
    struct data_node slow;
    struct probe fast_probe = {0};
    struct probe slow_probe = {0};

    data_broker_init(&broker);
    assert(data_broker_handle_timers(&broker, 0U) == UINT32_MAX);
    assert(data_node_init(&fast, &broker, "fast", on_event, DATA_NODE_EVENT_TIMER, &fast_probe) == DATA_NODE_OK);
    assert(data_node_init(&slow, &broker, "slow", on_event, DATA_NODE_EVENT_TIMER, &slow_probe) == DATA_NODE_OK);
    data_node_start_timer(&fast, 100U, 1000U);
    data_node_start_timer(&slow, 250U, 1000U);

    assert(data_broker_handle_timers(&broker, 1050U) == 50U);
    assert(fast_probe.timer_count == 0U);
    assert(data_broker_handle_timers(&broker, 1100U) == 100U);
    assert(fast_probe.timer_count == 1U && fast_probe.last_timer_ms == 1100U);
    assert(data_broker_handle_timers(&broker, 1260U) == 100U);
    assert(fast_probe.timer_count == 2U && slow_probe.timer_count == 1U);

    data_node_stop_timer(&fast);
    assert(data_broker_handle_timers(&broker, 1500U) == 10U);
    assert(slow_probe.timer_count == 1U);
    assert(data_broker_handle_timers(&broker, 1510U) == 250U);
    assert(fast_probe.timer_count == 2U && slow_probe.timer_count == 2U);
    data_node_start_timer(&slow, 0U, 1500U);
    assert(data_broker_handle_timers(&broker, 5000U) == UINT32_MAX);

    /* Unsigned elapsed time survives millisecond counter wrap. */
    data_node_start_timer(&fast, 100U, UINT32_MAX - 49U);
    assert(data_broker_handle_timers(&broker, 49U) == 1U);
    assert(data_broker_handle_timers(&broker, 50U) == 100U);
    assert(fast_probe.timer_count == 3U);
}

int main(void) {
    test_registration();
    test_publish_pull_notify();
    test_link_capacity();
    test_timers();
    return 0;
}
