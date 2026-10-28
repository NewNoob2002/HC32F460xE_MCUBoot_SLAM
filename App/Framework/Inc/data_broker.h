#ifndef DATA_BROKER_H
#define DATA_BROKER_H

/*
 * Synchronous publish/subscribe data nodes, a C port of the X-TRACK DataBroker
 * used by GNSS_Panel. Nodes and the broker are caller-allocated and live for
 * the whole application; there is no heap, no node removal and no dispatch from
 * interrupts. All calls must run on the cooperative main loop.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DATA_BROKER_MAX_NODES 16U
#define DATA_NODE_MAX_LINKS   8U

enum data_node_event {
    DATA_NODE_EVENT_NONE = 0,
    DATA_NODE_EVENT_PUBLISH = 1U << 0, /* Publisher pushed a new value to its subscribers. */
    DATA_NODE_EVENT_PULL = 1U << 1,    /* Subscriber requests the publisher's current value. */
    DATA_NODE_EVENT_NOTIFY = 1U << 2,  /* Subscriber sends a command to the publisher. */
    DATA_NODE_EVENT_TIMER = 1U << 3,   /* Node timer expired; data points at the current uint32_t now_ms. */
    DATA_NODE_EVENT_ALL = 0x0FU,
};

enum data_node_result {
    DATA_NODE_OK = 0,
    DATA_NODE_ERR_UNKNOWN = -1,
    DATA_NODE_ERR_SIZE_MISMATCH = -2,
    DATA_NODE_ERR_UNSUPPORTED_REQUEST = -3,
    DATA_NODE_ERR_NO_CALLBACK = -4,
    DATA_NODE_ERR_NO_DATA = -5,
    DATA_NODE_ERR_NOT_FOUND = -6,
    DATA_NODE_ERR_PARAM = -7,
    DATA_NODE_STOP_PROCESS = -8, /* Returned by a subscriber to stop the remaining publish fan-out. */
    DATA_NODE_ERR_CAPACITY = -9,
    DATA_NODE_ERR_DUPLICATE = -10,
};

struct data_broker;
struct data_node;

struct data_node_event_param {
    struct data_node* tran; /* Sender. */
    struct data_node* recv; /* Receiver. */
    enum data_node_event event;
    void* data; /* Borrowed for the duration of the callback only. */
    size_t size;
};

typedef int (*data_node_callback_t)(struct data_node* node, struct data_node_event_param* param);

struct data_node {
    const char* id;
    struct data_broker* broker;
    void* user_data;
    data_node_callback_t callback;
    uint32_t event_filter;
    struct data_node* publishers[DATA_NODE_MAX_LINKS];
    struct data_node* subscribers[DATA_NODE_MAX_LINKS];
    uint32_t timer_period_ms;
    uint32_t timer_last_ms;
    bool timer_running;
};

struct data_broker {
    struct data_node* nodes[DATA_BROKER_MAX_NODES];
    bool handling_timers;
};

void data_broker_init(struct data_broker* broker);
struct data_node* data_broker_search(struct data_broker* broker, const char* id);
size_t data_broker_node_count(const struct data_broker* broker);
/* Dispatches due node timers and returns the milliseconds until the next one, or UINT32_MAX when none run. */
uint32_t data_broker_handle_timers(struct data_broker* broker, uint32_t now_ms);

/* Registers a node with a unique non-empty id; the id string must outlive the node. */
int data_node_init(struct data_node* node, struct data_broker* broker, const char* id, data_node_callback_t callback,
                   uint32_t event_filter, void* user_data);
int data_node_subscribe(struct data_node* node, const char* pub_id);
int data_node_unsubscribe(struct data_node* node, const char* pub_id);
size_t data_node_publisher_count(const struct data_node* node);
size_t data_node_subscriber_count(const struct data_node* node);
/* Returns DATA_NODE_OK, DATA_NODE_STOP_PROCESS, or the last subscriber error. */
int data_node_publish(struct data_node* node, const void* data, size_t size);
int data_node_pull(struct data_node* node, const char* pub_id, void* data, size_t size);
int data_node_notify(struct data_node* node, const char* pub_id, const void* data, size_t size);
/* Zero period stops the timer. The first expiry is one period after now_ms. */
void data_node_start_timer(struct data_node* node, uint32_t period_ms, uint32_t now_ms);
void data_node_stop_timer(struct data_node* node);
/* Answers a PULL by copying exactly size bytes of state into the requester's buffer. */
int data_node_reply_pull(const struct data_node_event_param* param, const void* state, size_t size);

#endif
