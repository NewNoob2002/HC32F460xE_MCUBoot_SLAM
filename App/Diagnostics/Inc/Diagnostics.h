#pragma once
#include "DataProc.h"

namespace app {
inline constexpr uint32_t DiagnosticsPeriodMs = 5000U;
struct DiagnosticsSnapshot {
    uint32_t uptime_ms = 0U;
    uint32_t report_count = 0U;
    dp::SystemState system{};
    dp::UpdateState update{};
    dp::ChargerState charger{};
    dp::PowerState power{};
};
class Diagnostics final : public DataNode {
  public:
    explicit constexpr Diagnostics(DiagnosticsSnapshot& snapshot) : snapshot_(snapshot) {}
    bool init(DataBroker& broker, uint32_t now_ms);
    int onEvent(EventParam* event) override;

  private:
    DiagnosticsSnapshot& snapshot_;
    uint32_t started_ms_ = 0U;
    void pullAll();
    void report(uint32_t now_ms);
};
} // namespace app
// Keep an unmangled symbol so J-Link can inspect the full runtime snapshot by name.
extern "C" app::DiagnosticsSnapshot g_app_diagnostics;
