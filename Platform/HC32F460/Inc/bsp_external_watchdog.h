#ifndef BSP_EXTERNAL_WATCHDOG_H
#define BSP_EXTERNAL_WATCHDOG_H
#include <stdbool.h>
#include <stdint.h>
typedef void (*watchdog_gpio_write_t)(bool level, void* context);
typedef enum { BSP_WDOG_STATE_IDLE = 0, BSP_WDOG_STATE_PULSE_HIGH } bsp_watchdog_state_t;
typedef struct {
    bsp_watchdog_state_t state;
    uint32_t interval_ms;
    uint32_t pulse_ms;
    uint32_t next_feed_ms;
    uint32_t pulse_start_ms;
    bool active_level;
    bool enabled;
    watchdog_gpio_write_t write;
    void* write_context;
} watchdog_scheduler_t;
void watchdog_scheduler_init(watchdog_scheduler_t* scheduler, uint32_t now_ms, uint32_t interval_ms, uint32_t pulse_ms,
                             bool active_level, watchdog_gpio_write_t write, void* context);
void watchdog_scheduler_poll(watchdog_scheduler_t* scheduler, uint32_t now_ms);
bool watchdog_scheduler_force(watchdog_scheduler_t* scheduler, uint32_t now_ms);
void watchdog_scheduler_stop_low(watchdog_scheduler_t* scheduler);
bool bsp_external_watchdog_init(uint32_t now_ms);
void bsp_external_watchdog_poll(uint32_t now_ms);
bool bsp_external_watchdog_force_feed(uint32_t now_ms);
bool bsp_external_watchdog_is_ready(void);
bool bsp_external_watchdog_is_pulse_active(void);
void bsp_external_watchdog_prepare_handover(void);
#endif
