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
// 保持未修饰符号，供 J-Link 按名称读取完整运行快照。
extern "C" app::DiagnosticsSnapshot g_app_diagnostics;
