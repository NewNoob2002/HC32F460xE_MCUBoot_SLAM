#pragma once
#include "DataProc.h"
#include "Diagnostics.h"

/** Assembles static resources and sequences startup and the cooperative loop; nodes own domain behavior. */
class App final {
  public:
    [[noreturn]] void run();

  private:
#if APP_PHASE2_HIL_MODE == 0
    // Members construct in declaration order and destruct in reverse; the broker outlives all nodes.
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
