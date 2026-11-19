#ifndef APP_APP_H
#define APP_APP_H
#include "DataProc.h"
#include "DebugService.h"
#include "Diagnostics.h"
#include "Hc32ChargerSignals.h"
#include "Hc32ExternalWatchdog.h"
#include "Hc32I2cBus.h"
#include "Hc32PowerDevices.h"
#include "Hc32SettingsStore.h"

/** Assembles static resources and sequences startup and the cooperative loop; nodes own domain behavior. */
class App final {
  public:
    [[noreturn]] void run();

  private:
#if APP_PHASE2_HIL_MODE == 0
    // Members construct in declaration order and destruct in reverse; the broker outlives all nodes.
    DataBroker broker_{};
    hal::Hc32I2cBus bus_{};
    hal::Hc32PowerDevices power_devices_{bus_};
    hal::Hc32ExternalWatchdog watchdog_{};
    hal::Hc32ChargerSignals signals_{};
    hal::Hc32SettingsStore settings_store_{};
    dp::SettingsNode settings_{settings_store_};
    debug::Service debug_{settings_, bus_};
    dp::PowerNode power_{power_devices_};
    dp::ChargerNode charger_{signals_};
    dp::SystemNode system_{watchdog_};
    dp::UpdateNode update_{};
    app::Diagnostics diagnostics_{g_app_diagnostics};
    [[noreturn]] void runApplication();
#else
    [[noreturn]] void runPhase2Hil();
#endif
};

#endif
