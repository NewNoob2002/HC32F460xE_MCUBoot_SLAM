#ifndef APP_HAL_EXTERNALWATCHDOG_H
#define APP_HAL_EXTERNALWATCHDOG_H

#include <cstdint>

namespace hal {
/** Main-loop-only external watchdog contract; callers own the implementation. */
class ExternalWatchdog {
  public:
    virtual bool init(uint32_t now_ms) = 0;
    virtual void poll(uint32_t now_ms) = 0;
    virtual bool isReady() const = 0;
    virtual bool isPulseActive() const = 0;

  protected:
    ~ExternalWatchdog() = default;
};
} // namespace hal

#endif
