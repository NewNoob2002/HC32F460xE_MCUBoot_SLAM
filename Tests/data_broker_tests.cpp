#include <assert.h>
#include <optional>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "DataBroker.h"

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

static int on_event(DataNode* node, DataNode::EventParam* param) {
    struct probe* probe = static_cast<struct probe*>(node->getUserData());

    assert(param->recv == node);
    switch (param->event) {
        case DataNode::EVENT_PUBLISH:
            ++probe->publish_count;
            memcpy(&probe->last_value, param->data, sizeof(probe->last_value));
            if (probe->unsubscribe_on_publish)
                assert(node->unsubscribe(param->tran->getID()) == DataNode::RES_OK);
            return probe->publish_result;
        case DataNode::EVENT_PULL: {
            static const uint32_t value = 0xA5A5U;
            ++probe->pull_count;
            return DataNode::replyPull(param, &value, sizeof(value));
        }
        case DataNode::EVENT_NOTIFY:
            ++probe->notify_count;
            memcpy(&probe->last_value, param->data, sizeof(probe->last_value));
            return DataNode::RES_OK;
        case DataNode::EVENT_TIMER:
            ++probe->timer_count;
            memcpy(&probe->last_timer_ms, param->data, sizeof(probe->last_timer_ms));
            return DataNode::RES_OK;
        default:
            return DataNode::RES_UNKNOWN;
    }
}

static void test_registration(void) {
    DataBroker broker;
    DataNode nodes[DataBroker::MaxNodes + 1U];
    static char ids[DataBroker::MaxNodes + 1U][4];
    DataNode duplicate;

    for (size_t i = 0U; i < DataBroker::MaxNodes; ++i) {
        ids[i][0] = 'n';
        ids[i][1] = (char)('a' + i);
        ids[i][2] = '\0';
        assert(nodes[i].init(broker, ids[i], DataNode::EVENT_ALL, on_event, nullptr) == DataNode::RES_OK);
    }
    assert(broker.nodeCount() == DataBroker::MaxNodes);
    assert(broker.search("nb") == &nodes[1]);
    assert(broker.search("missing") == nullptr);
    assert(duplicate.init(broker, "na", DataNode::EVENT_ALL, on_event, nullptr) == DataNode::RES_DUPLICATE);
    assert(nodes[DataBroker::MaxNodes].init(broker, "full", DataNode::EVENT_ALL, on_event, nullptr)
           == DataNode::RES_CAPACITY);
    assert(nodes[DataBroker::MaxNodes].subscribe("na") == DataNode::RES_PARAM_ERROR);
    assert(duplicate.init(broker, "", DataNode::EVENT_ALL, on_event, nullptr) == DataNode::RES_PARAM_ERROR);
}

static void test_publish_pull_notify(void) {
    DataBroker broker;
    DataNode pub;
    DataNode sub_a;
    DataNode sub_b;
    struct probe pub_probe = {};
    struct probe probe_a = {};
    struct probe probe_b = {};
    uint32_t value = 7U;
    uint32_t pulled = 0U;
    uint16_t wrong_size = 0U;

    assert(pub.init(broker, "pub", DataNode::EVENT_PULL | DataNode::EVENT_NOTIFY, on_event, &pub_probe)
           == DataNode::RES_OK);
    assert(sub_a.init(broker, "a", DataNode::EVENT_PUBLISH, on_event, &probe_a) == DataNode::RES_OK);
    assert(sub_b.init(broker, "b", DataNode::EVENT_PUBLISH, on_event, &probe_b) == DataNode::RES_OK);

    assert(sub_a.subscribe("pub") == DataNode::RES_OK);
    assert(sub_a.subscribe("pub") == DataNode::RES_DUPLICATE);
    assert(sub_a.subscribe("a") == DataNode::RES_PARAM_ERROR);
    assert(sub_a.subscribe("missing") == DataNode::RES_NOT_FOUND);
    assert(sub_b.subscribe("pub") == DataNode::RES_OK);
    assert(pub.subscriberCount() == 2U);
    assert(sub_a.publisherCount() == 1U);

    assert(pub.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(probe_a.publish_count == 1U && probe_a.last_value == 7U);
    assert(probe_b.publish_count == 1U && probe_b.last_value == 7U);
    assert(pub.publish(nullptr, sizeof(value)) == DataNode::RES_PARAM_ERROR);

    /* STOP_PROCESS ends the fan-out in subscription order. */
    probe_a.publish_result = DataNode::RES_STOP_PROCESS;
    value = 8U;
    assert(pub.publish(&value, sizeof(value)) == DataNode::RES_STOP_PROCESS);
    assert(probe_a.publish_count == 2U && probe_b.publish_count == 1U);
    probe_a.publish_result = DataNode::RES_OK;

    /* A subscriber error is reported without stopping later subscribers. */
    probe_a.publish_result = DataNode::RES_NO_DATA;
    assert(pub.publish(&value, sizeof(value)) == DataNode::RES_NO_DATA);
    assert(probe_b.publish_count == 2U);
    probe_a.publish_result = DataNode::RES_OK;

    assert(sub_a.pull("pub", &pulled, sizeof(pulled)) == DataNode::RES_OK);
    assert(pulled == 0xA5A5U && pub_probe.pull_count == 1U);
    assert(sub_a.pull("pub", &wrong_size, sizeof(wrong_size)) == DataNode::RES_SIZE_MISMATCH);
    assert(pub.pull("a", &pulled, sizeof(pulled)) == DataNode::RES_NOT_FOUND);
    /* Event filters reject requests a node did not opt into. */
    assert(pub.subscribe("a") == DataNode::RES_OK);
    assert(pub.pull("a", &pulled, sizeof(pulled)) == DataNode::RES_UNSUPPORTED_REQUEST);

    value = 42U;
    assert(sub_b.notify("pub", &value, sizeof(value)) == DataNode::RES_OK);
    assert(pub_probe.notify_count == 1U && pub_probe.last_value == 42U);

    /* Unsubscribing during fan-out does not skip or repeat the remaining subscribers. */
    probe_a.unsubscribe_on_publish = true;
    assert(pub.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(probe_a.publish_count == 4U && probe_b.publish_count == 3U);
    assert(pub.subscriberCount() == 1U);
    assert(sub_a.pull("pub", &pulled, sizeof(pulled)) == DataNode::RES_NOT_FOUND);
    assert(sub_a.unsubscribe("pub") == DataNode::RES_NOT_FOUND);
    assert(pub.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(probe_a.publish_count == 4U && probe_b.publish_count == 4U);
}

static void test_link_capacity(void) {
    DataBroker broker;
    DataNode pubs[DataNode::MaxLinks + 1U];
    static char ids[DataNode::MaxLinks + 1U][4];
    DataNode sub;

    assert(sub.init(broker, "sub", DataNode::EVENT_ALL, on_event, nullptr) == DataNode::RES_OK);
    for (size_t i = 0U; i <= DataNode::MaxLinks; ++i) {
        ids[i][0] = 'p';
        ids[i][1] = (char)('a' + i);
        ids[i][2] = '\0';
        assert(pubs[i].init(broker, ids[i], DataNode::EVENT_ALL, on_event, nullptr) == DataNode::RES_OK);
        assert(sub.subscribe(ids[i]) == (i < DataNode::MaxLinks ? DataNode::RES_OK : DataNode::RES_CAPACITY));
    }
    assert(sub.publisherCount() == DataNode::MaxLinks);
    assert(pubs[DataNode::MaxLinks].subscriberCount() == 0U);
}

static void test_timers(void) {
    DataBroker broker;
    DataNode fast;
    DataNode slow;
    struct probe fast_probe = {};
    struct probe slow_probe = {};

    assert(broker.handleTimers(0U) == UINT32_MAX);
    assert(fast.init(broker, "fast", DataNode::EVENT_TIMER, on_event, &fast_probe) == DataNode::RES_OK);
    assert(slow.init(broker, "slow", DataNode::EVENT_TIMER, on_event, &slow_probe) == DataNode::RES_OK);
    fast.startTimer(100U, 1000U);
    slow.startTimer(250U, 1000U);

    assert(broker.handleTimers(1050U) == 50U);
    assert(fast_probe.timer_count == 0U);
    assert(broker.handleTimers(1100U) == 100U);
    assert(fast_probe.timer_count == 1U && fast_probe.last_timer_ms == 1100U);
    assert(broker.handleTimers(1260U) == 100U);
    assert(fast_probe.timer_count == 2U && slow_probe.timer_count == 1U);

    fast.stopTimer();
    assert(broker.handleTimers(1500U) == 10U);
    assert(slow_probe.timer_count == 1U);
    assert(broker.handleTimers(1510U) == 250U);
    assert(fast_probe.timer_count == 2U && slow_probe.timer_count == 2U);
    slow.startTimer(0U, 1500U);
    assert(broker.handleTimers(5000U) == UINT32_MAX);

    /* Unsigned elapsed time survives millisecond counter wrap. */
    fast.startTimer(100U, UINT32_MAX - 49U);
    assert(broker.handleTimers(49U) == 1U);
    assert(broker.handleTimers(50U) == 100U);
    assert(fast_probe.timer_count == 3U);
}

// 回调中改变关系时按快照分发；新订阅不能插入本轮。
static void test_snapshot_and_errors() {
    DataBroker broker;
    DataNode publisher;
    struct Mutator final : DataNode {
        DataNode* remove = nullptr;
        DataNode* add = nullptr;
        int onEvent(EventParam* event) override {
            if (remove) {
                assert(remove->unsubscribe(event->tran->getID()) == RES_OK);
                remove = nullptr;
            }
            if (add) {
                assert(add->subscribe(event->tran->getID()) == RES_OK);
                add = nullptr;
            }
            return RES_OK;
        }
    } mutator;
    DataNode removed, added, filtered;
    probe removed_probe{}, added_probe{};
    assert(publisher.init(broker, "pub") == DataNode::RES_OK);
    assert(mutator.init(broker, "mutator") == DataNode::RES_OK);
    assert(removed.init(broker, "removed", DataNode::EVENT_ALL, on_event, &removed_probe) == DataNode::RES_OK);
    assert(added.init(broker, "added", DataNode::EVENT_ALL, on_event, &added_probe) == DataNode::RES_OK);
    assert(filtered.init(broker, "filtered", DataNode::EVENT_PULL) == DataNode::RES_OK);
    assert(mutator.subscribe("pub") == DataNode::RES_OK);
    assert(removed.subscribe("pub") == DataNode::RES_OK);
    assert(filtered.subscribe("pub") == DataNode::RES_OK);
    mutator.remove = &removed;
    mutator.add = &added;
    uint32_t value = 1U;
    assert(publisher.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(removed_probe.publish_count == 0U && added_probe.publish_count == 0U);
    assert(publisher.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(added_probe.publish_count == 1U);
    removed_probe.publish_result = DataNode::RES_NO_DATA;
    added_probe.publish_result = DataNode::RES_UNKNOWN;
    assert(removed.subscribe("pub") == DataNode::RES_OK);
    assert(publisher.publish(&value, sizeof(value)) == DataNode::RES_NO_DATA);
    // 过滤标志与回调返回 UNSUPPORTED_REQUEST 具有不同含义。
    removed_probe.publish_result = DataNode::RES_UNSUPPORTED_REQUEST;
    assert(publisher.publish(&value, sizeof(value)) == DataNode::RES_UNSUPPORTED_REQUEST);
    assert(publisher.notify("removed", &value, sizeof(value)) == DataNode::RES_NOT_FOUND);
    assert(removed.notify("filtered", &value, sizeof(value)) == DataNode::RES_NOT_FOUND);
    assert(removed.subscribe("filtered") == DataNode::RES_OK);
    assert(removed.notify("filtered", &value, sizeof(value)) == DataNode::RES_UNSUPPORTED_REQUEST);
}
static void test_subscriber_capacity_and_lifetime() {
    DataBroker broker;
    DataNode publisher;
    DataNode subscribers[DataNode::MaxLinks + 1U];
    char ids[DataNode::MaxLinks + 1U][2]{};
    assert(publisher.init(broker, "pub") == DataNode::RES_OK);
    for (size_t i = 0U; i <= DataNode::MaxLinks; ++i) {
        ids[i][0] = static_cast<char>('a' + i);
        assert(subscribers[i].init(broker, ids[i]) == DataNode::RES_OK);
        assert(subscribers[i].subscribe("pub") == (i < DataNode::MaxLinks ? DataNode::RES_OK : DataNode::RES_CAPACITY));
    }
    assert(subscribers[DataNode::MaxLinks].publisherCount() == 0U);
    {
        DataNode transient;
        assert(transient.init(broker, "transient") == DataNode::RES_OK);
        assert(subscribers[0].subscribe("transient") == DataNode::RES_OK);
    }
    assert(broker.search("transient") == nullptr && subscribers[0].publisherCount() == 1U);
    DataNode no_callback;
    assert(no_callback.init(broker, "empty") == DataNode::RES_OK);
    assert(subscribers[0].subscribe("empty") == DataNode::RES_OK);
    uint32_t value = 0U;
    assert(subscribers[0].pull("empty", &value, sizeof(value)) == DataNode::RES_NO_CALLBACK);
    assert(no_callback.init(broker, "changed") == DataNode::RES_DUPLICATE);
    assert(broker.search("empty") == &no_callback && broker.search("changed") == nullptr);
    assert(DataNode::replyPull(nullptr, &value, sizeof(value)) == DataNode::RES_PARAM_ERROR);
}
static void test_nested_timers() {
    DataBroker broker;
    struct Nested final : DataNode {
        DataBroker& broker;
        unsigned calls = 0U;
        explicit Nested(DataBroker& owner) : broker(owner) {}
        int onEvent(EventParam* event) override {
            assert(event->event == EVENT_TIMER && event->size == sizeof(uint32_t));
            uint32_t now = 0U;
            memcpy(&now, event->data, sizeof(now));
            assert(broker.handleTimers(now) == 0U);
            ++calls;
            stopTimer();
            return RES_OK;
        }
    } nested(broker);
    assert(nested.init(broker, "nested", DataNode::EVENT_TIMER) == DataNode::RES_OK);
    nested.startTimer(10U, 0U);
    assert(broker.handleTimers(10U) == UINT32_MAX && nested.calls == 1U);
    assert(broker.handleTimers(100U) == UINT32_MAX && nested.calls == 1U);
}

// 同地址重建的节点不能接收旧快照中的发布或 TIMER。
static void test_recreated_node_snapshots() {
    DataBroker broker;
    DataNode publisher;
    std::optional<DataNode> replacement;
    probe replacement_probe{};
    struct Recreator final : DataNode {
        DataBroker& owner;
        std::optional<DataNode>& replacement;
        probe& payload;
        bool replace = true;
        bool timer_mode = false;
        Recreator(DataBroker& broker, std::optional<DataNode>& target, probe& state)
            : owner(broker), replacement(target), payload(state) {}
        int onEvent(EventParam* event) override {
            if (!replace)
                return RES_OK;
            replace = false;
            replacement.reset();
            replacement.emplace();
            assert(replacement->init(owner, "replacement", EVENT_ALL, on_event, &payload) == RES_OK);
            if (timer_mode) {
                replacement->startTimer(1U, 0U);
                stopTimer();
            } else {
                assert(replacement->subscribe(event->tran->getID()) == RES_OK);
            }
            return RES_OK;
        }
    } recreator(broker, replacement, replacement_probe);
    assert(publisher.init(broker, "pub") == DataNode::RES_OK);
    assert(recreator.init(broker, "recreator") == DataNode::RES_OK);
    replacement.emplace();
    assert(replacement->init(broker, "replacement", DataNode::EVENT_ALL, on_event, &replacement_probe)
           == DataNode::RES_OK);
    assert(recreator.subscribe("pub") == DataNode::RES_OK);
    assert(replacement->subscribe("pub") == DataNode::RES_OK);
    uint32_t value = 1U;
    assert(publisher.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(replacement_probe.publish_count == 0U);
    assert(publisher.publish(&value, sizeof(value)) == DataNode::RES_OK);
    assert(replacement_probe.publish_count == 1U);
    recreator.replace = true;
    recreator.timer_mode = true;
    recreator.startTimer(1U, 0U);
    replacement->startTimer(1U, 0U);
    assert(broker.handleTimers(1U) == 0U);
    assert(replacement_probe.timer_count == 0U);
    assert(broker.handleTimers(1U) == 1U);
    assert(replacement_probe.timer_count == 1U);
}

int main(void) {
    test_registration();
    test_publish_pull_notify();
    test_link_capacity();
    test_timers();
    test_snapshot_and_errors();
    test_subscriber_capacity_and_lifetime();
    test_nested_timers();
    test_recreated_node_snapshots();
    return 0;
}
