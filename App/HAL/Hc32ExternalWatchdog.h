#ifndef APP_HAL_HC32EXTERNALWATCHDOG_H
#define APP_HAL_HC32EXTERNALWATCHDOG_H

#include "ExternalWatchdog.h"

namespace hal {
class Hc32ExternalWatchdog final : public ExternalWatchdog {
  public:
    bool init(uint32_t now_ms) override;
    void poll(uint32_t now_ms) override;
    bool isReady() const override;
    bool isPulseActive() const override;
};
} // namespace hal

#endif
