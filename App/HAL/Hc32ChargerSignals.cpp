#include "Hc32ChargerSignals.h"
#include "bsp_charger_signals.h"

namespace hal {
bool Hc32ChargerSignals::init() {
    return bsp_charger_signals_init();
}
bool Hc32ChargerSignals::read(ChargerSample& sample) {
    bsp_charger_signals signals{};
    if (!bsp_charger_signals_read(&signals))
        return false;
    sample = {signals.interrupt_asserted, signals.power_good};
    return true;
}
} // namespace hal
