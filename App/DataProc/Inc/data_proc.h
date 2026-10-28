#ifndef DATA_PROC_H
#define DATA_PROC_H

/*
 * Application domain nodes. Each node owns one state type, answers PULL with a
 * copy of exactly that type and publishes only when the value changes.
 *
 *   Power   -> struct power_devices_status   (startup I2C probe, published once)
 *   Charger -> struct dp_charger_state       (100 ms GPIO sample, publish on change)
 *   Update  -> struct dp_update_state        (USB updater, publish on state change)
 *   System  -> struct dp_system_state        (watchdog, confirmation, main-loop count; pull only)
 */

#include <stdbool.h>
#include <stdint.h>

#include "data_broker.h"
#include "power_devices.h"
#include "usb_fw_update.h"

#define DP_ID_POWER   "Power"
#define DP_ID_CHARGER "Charger"
#define DP_ID_UPDATE  "Update"
#define DP_ID_SYSTEM  "System"

#define DP_CHARGER_POLL_MS 100U

struct dp_charger_state {
    bool valid; /* False until the GPIOs are configured and sampled once. */
    bool interrupt_asserted;
    bool power_good;
    uint32_t change_count;
};

struct dp_update_state {
    int init_result;
    struct usb_fw_update_status usb;
};

struct dp_system_state {
    int confirm_result;
    uint32_t loop_count;
    bool watchdog_ready;
    bool watchdog_pulse_active;
};

/* Missing devices are degraded status, never a startup failure. */
void dp_power_init(struct data_broker* broker);
void dp_charger_init(struct data_broker* broker, uint32_t now_ms);
/* Starts the external watchdog; false is a fatal startup failure. */
bool dp_system_init(struct data_broker* broker, uint32_t now_ms);
/* Feeds the external watchdog directly so its deadline never depends on node timers. */
void dp_system_poll(uint32_t now_ms);
int dp_system_confirm(bool auto_confirm);
/* Returns the usb_fw_update_init() result; nonzero is a fatal startup failure. */
int dp_update_init(struct data_broker* broker);
/* Runs every main-loop pass so USB transfers are never throttled by a node timer period. */
enum usb_fw_update_action dp_update_poll(uint32_t now_ms);

static inline int dp_pull_power(struct data_node* sub, struct power_devices_status* state) {
    return data_node_pull(sub, DP_ID_POWER, state, sizeof(*state));
}

static inline int dp_pull_charger(struct data_node* sub, struct dp_charger_state* state) {
    return data_node_pull(sub, DP_ID_CHARGER, state, sizeof(*state));
}

static inline int dp_pull_update(struct data_node* sub, struct dp_update_state* state) {
    return data_node_pull(sub, DP_ID_UPDATE, state, sizeof(*state));
}

static inline int dp_pull_system(struct data_node* sub, struct dp_system_state* state) {
    return data_node_pull(sub, DP_ID_SYSTEM, state, sizeof(*state));
}

#endif
