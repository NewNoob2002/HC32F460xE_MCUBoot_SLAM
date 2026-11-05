#include "Hc32ExternalWatchdog.h"

#include "bsp_external_watchdog.h"

namespace hal {
bool Hc32ExternalWatchdog::init(uint32_t now_ms) {
    return bsp_external_watchdog_init(now_ms);
}

void Hc32ExternalWatchdog::poll(uint32_t now_ms) {
    bsp_external_watchdog_poll(now_ms);
}

bool Hc32ExternalWatchdog::isReady() const {
    return bsp_external_watchdog_is_ready();
}

bool Hc32ExternalWatchdog::isPulseActive() const {
    return bsp_external_watchdog_is_pulse_active();
}
} // namespace hal
