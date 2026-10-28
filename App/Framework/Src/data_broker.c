#include "data_broker.h"

#include <string.h>

static struct data_node* search_list(struct data_node* const* list, size_t capacity, const char* id) {
    if (id == NULL || id[0] == '\0')
        return NULL;
    for (size_t i = 0U; i < capacity; ++i) {
        if (list[i] != NULL && strcmp(list[i]->id, id) == 0)
            return list[i];
    }
    return NULL;
}

static bool list_contains(struct data_node* const* list, size_t capacity, const struct data_node* node) {
    for (size_t i = 0U; i < capacity; ++i) {
        if (list[i] == node)
            return true;
    }
    return false;
}

static bool list_append(struct data_node** list, size_t capacity, struct data_node* node) {
    for (size_t i = 0U; i < capacity; ++i) {
        if (list[i] == NULL) {
            list[i] = node;
            return true;
        }
    }
    return false;
}

/* Keeps entries packed so iteration order stays the subscription order. */
static bool list_remove(struct data_node** list, size_t capacity, const struct data_node* node) {
    for (size_t i = 0U; i < capacity; ++i) {
        if (list[i] == node) {
            memmove(&list[i], &list[i + 1U], (capacity - i - 1U) * sizeof(list[0]));
            list[capacity - 1U] = NULL;
            return true;
        }
    }
    return false;
}

static size_t list_count(struct data_node* const* list, size_t capacity) {
    size_t count = 0U;
    for (size_t i = 0U; i < capacity; ++i) {
        if (list[i] != NULL)
            ++count;
    }
    return count;
}

static bool is_registered(const struct data_node* node) {
    return node != NULL && node->broker != NULL;
}

static int send_event(struct data_node* tran, struct data_node* recv, enum data_node_event event, void* data,
                      size_t size) {
    struct data_node_event_param param = {
        .tran = tran,
        .recv = recv,
        .event = event,
        .data = data,
        .size = size,
    };

    if ((recv->event_filter & (uint32_t)event) == 0U)
        return DATA_NODE_ERR_UNSUPPORTED_REQUEST;
    if (recv->callback == NULL)
        return DATA_NODE_ERR_NO_CALLBACK;
    return recv->callback(recv, &param);
}

static struct data_node* subscribed_publisher(struct data_node* node, const char* pub_id) {
    return search_list(node->publishers, DATA_NODE_MAX_LINKS, pub_id);
}

void data_broker_init(struct data_broker* broker) {
    if (broker != NULL)
        *broker = (struct data_broker){0};
}

struct data_node* data_broker_search(struct data_broker* broker, const char* id) {
    return broker == NULL ? NULL : search_list(broker->nodes, DATA_BROKER_MAX_NODES, id);
}

size_t data_broker_node_count(const struct data_broker* broker) {
    return broker == NULL ? 0U : list_count(broker->nodes, DATA_BROKER_MAX_NODES);
}

uint32_t data_broker_handle_timers(struct data_broker* broker, uint32_t now_ms) {
    struct data_node* snapshot[DATA_BROKER_MAX_NODES];
    uint32_t next = UINT32_MAX;

    if (broker == NULL)
        return UINT32_MAX;
    /* A callback that re-enters the handler is deferred to the next main-loop pass. */
    if (broker->handling_timers)
        return 0U;
    broker->handling_timers = true;
    memcpy(snapshot, broker->nodes, sizeof(snapshot));
    for (size_t i = 0U; i < DATA_BROKER_MAX_NODES; ++i) {
        struct data_node* node = snapshot[i];
        if (node == NULL || !node->timer_running || now_ms - node->timer_last_ms < node->timer_period_ms)
            continue;
        node->timer_last_ms = now_ms;
        uint32_t event_now_ms = now_ms;
        (void)send_event(node, node, DATA_NODE_EVENT_TIMER, &event_now_ms, sizeof(event_now_ms));
    }
    for (size_t i = 0U; i < DATA_BROKER_MAX_NODES; ++i) {
        const struct data_node* node = broker->nodes[i];
        if (node == NULL || !node->timer_running)
            continue;
        const uint32_t elapsed = now_ms - node->timer_last_ms;
        const uint32_t remain = elapsed >= node->timer_period_ms ? 0U : node->timer_period_ms - elapsed;
        if (remain < next)
            next = remain;
    }
    broker->handling_timers = false;
    return next;
}

int data_node_init(struct data_node* node, struct data_broker* broker, const char* id, data_node_callback_t callback,
                   uint32_t event_filter, void* user_data) {
    if (node == NULL || broker == NULL || id == NULL || id[0] == '\0')
        return DATA_NODE_ERR_PARAM;
    if (data_broker_search(broker, id) != NULL || list_contains(broker->nodes, DATA_BROKER_MAX_NODES, node))
        return DATA_NODE_ERR_DUPLICATE;

    *node = (struct data_node){
        .id = id,
        .callback = callback,
        .event_filter = event_filter,
        .user_data = user_data,
    };
    if (!list_append(broker->nodes, DATA_BROKER_MAX_NODES, node))
        return DATA_NODE_ERR_CAPACITY;
    node->broker = broker;
    return DATA_NODE_OK;
}

int data_node_subscribe(struct data_node* node, const char* pub_id) {
    struct data_node* pub;

    if (!is_registered(node) || pub_id == NULL || pub_id[0] == '\0')
        return DATA_NODE_ERR_PARAM;
    if (strcmp(pub_id, node->id) == 0)
        return DATA_NODE_ERR_PARAM;
    if (subscribed_publisher(node, pub_id) != NULL)
        return DATA_NODE_ERR_DUPLICATE;
    pub = data_broker_search(node->broker, pub_id);
    if (pub == NULL)
        return DATA_NODE_ERR_NOT_FOUND;
    if (!list_append(node->publishers, DATA_NODE_MAX_LINKS, pub))
        return DATA_NODE_ERR_CAPACITY;
    if (!list_append(pub->subscribers, DATA_NODE_MAX_LINKS, node)) {
        (void)list_remove(node->publishers, DATA_NODE_MAX_LINKS, pub);
        return DATA_NODE_ERR_CAPACITY;
    }
    return DATA_NODE_OK;
}

int data_node_unsubscribe(struct data_node* node, const char* pub_id) {
    struct data_node* pub;

    if (!is_registered(node) || pub_id == NULL || pub_id[0] == '\0')
        return DATA_NODE_ERR_PARAM;
    pub = subscribed_publisher(node, pub_id);
    if (pub == NULL)
        return DATA_NODE_ERR_NOT_FOUND;
    (void)list_remove(node->publishers, DATA_NODE_MAX_LINKS, pub);
    (void)list_remove(pub->subscribers, DATA_NODE_MAX_LINKS, node);
    return DATA_NODE_OK;
}

size_t data_node_publisher_count(const struct data_node* node) {
    return node == NULL ? 0U : list_count(node->publishers, DATA_NODE_MAX_LINKS);
}

size_t data_node_subscriber_count(const struct data_node* node) {
    return node == NULL ? 0U : list_count(node->subscribers, DATA_NODE_MAX_LINKS);
}

int data_node_publish(struct data_node* node, const void* data, size_t size) {
    struct data_node* snapshot[DATA_NODE_MAX_LINKS];
    int result = DATA_NODE_OK;

    if (!is_registered(node) || data == NULL || size == 0U)
        return DATA_NODE_ERR_PARAM;
    /* Subscribers may unsubscribe during fan-out; skip any that left after the snapshot. */
    memcpy(snapshot, node->subscribers, sizeof(snapshot));
    for (size_t i = 0U; i < DATA_NODE_MAX_LINKS; ++i) {
        struct data_node* sub = snapshot[i];
        if (sub == NULL || !list_contains(node->subscribers, DATA_NODE_MAX_LINKS, sub))
            continue;
        const int sub_result = send_event(node, sub, DATA_NODE_EVENT_PUBLISH, (void*)(uintptr_t)data, size);
        if (sub_result == DATA_NODE_STOP_PROCESS)
            return sub_result;
        /* A subscriber that filters out PUBLISH only uses pull; that is not a delivery failure. */
        if (sub_result != DATA_NODE_OK && sub_result != DATA_NODE_ERR_UNSUPPORTED_REQUEST)
            result = sub_result;
    }
    return result;
}

int data_node_pull(struct data_node* node, const char* pub_id, void* data, size_t size) {
    struct data_node* pub;

    if (!is_registered(node) || data == NULL || size == 0U)
        return DATA_NODE_ERR_PARAM;
    pub = subscribed_publisher(node, pub_id);
    if (pub == NULL)
        return DATA_NODE_ERR_NOT_FOUND;
    return send_event(node, pub, DATA_NODE_EVENT_PULL, data, size);
}

int data_node_notify(struct data_node* node, const char* pub_id, const void* data, size_t size) {
    struct data_node* pub;

    if (!is_registered(node) || data == NULL || size == 0U)
        return DATA_NODE_ERR_PARAM;
    pub = subscribed_publisher(node, pub_id);
    if (pub == NULL)
        return DATA_NODE_ERR_NOT_FOUND;
    return send_event(node, pub, DATA_NODE_EVENT_NOTIFY, (void*)(uintptr_t)data, size);
}

void data_node_start_timer(struct data_node* node, uint32_t period_ms, uint32_t now_ms) {
    if (!is_registered(node))
        return;
    if (period_ms == 0U) {
        data_node_stop_timer(node);
        return;
    }
    node->timer_period_ms = period_ms;
    node->timer_last_ms = now_ms;
    node->timer_running = true;
}

void data_node_stop_timer(struct data_node* node) {
    if (node != NULL)
        node->timer_running = false;
}

int data_node_reply_pull(const struct data_node_event_param* param, const void* state, size_t size) {
    if (param == NULL || param->event != DATA_NODE_EVENT_PULL || param->data == NULL || state == NULL)
        return DATA_NODE_ERR_PARAM;
    if (param->size != size)
        return DATA_NODE_ERR_SIZE_MISMATCH;
    memcpy(param->data, state, size);
    return DATA_NODE_OK;
}
