#pragma once
#include "DataProc.h"
#include "Diagnostics.h"

/** 只负责静态资源组装、启动顺序与协作循环；领域行为由节点持有。 */
class App final {
  public:
    [[noreturn]] void run();

  private:
#if APP_PHASE2_HIL_MODE == 0
    // 成员按声明顺序构造、逆序析构；broker 比所有节点长寿。
    DataBroker broker_{};
    hal::Hc32I2cBus bus_{};
    hal::Hc32ChargerSignals signals_{};
    dp::PowerNode power_{bus_};
    dp::ChargerNode charger_{signals_};
    dp::SystemNode system_{};
    dp::UpdateNode update_{};
    app::Diagnostics diagnostics_{g_app_diagnostics};
    [[noreturn]] void runApplication();
#else
    [[noreturn]] void runPhase2Hil();
#endif
};
