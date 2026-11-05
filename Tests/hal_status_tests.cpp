#include <cassert>

#include "Status.h"

int main() {
    static_assert(hal::Ok == 0, "debug-link status changed");
    static_assert(hal::InitError == -1, "debug-link status changed");
    static_assert(hal::InvalidArgument == -2, "debug-link status changed");
    static_assert(hal::NotReady == -3, "debug-link status changed");
    static_assert(hal::InitDeinitError == -4, "debug-link status changed");
    static_assert(hal::InitDefaultsError == -5, "debug-link status changed");
    static_assert(hal::BusBusy == -6, "debug-link status changed");
    static_assert(hal::StartTimeout == -7, "debug-link status changed");
    static_assert(hal::AddressNack == -8, "debug-link status changed");
    static_assert(hal::AddressTimeout == -9, "debug-link status changed");
    static_assert(hal::TransmitNack == -10, "debug-link status changed");
    static_assert(hal::TransmitTimeout == -11, "debug-link status changed");
    static_assert(hal::RestartTimeout == -12, "debug-link status changed");
    static_assert(hal::ReceiveAddressNack == -13, "debug-link status changed");
    static_assert(hal::ReceiveAddressTimeout == -14, "debug-link status changed");
    static_assert(hal::ReceiveStopTimeout == -15, "debug-link status changed");
    static_assert(hal::BatteryInvalidArgument == -16, "debug-link status changed");
    static_assert(hal::ProtocolError == -17, "debug-link status changed");
    static_assert(hal::StopTimeout == -18, "debug-link status changed");
    static_assert(hal::ArbitrationLost == -19, "debug-link status changed");
    static_assert(hal::VerifyError == -24, "debug-link status changed");
    assert(static_cast<int>(hal::Ok) == 0);
    return 0;
}
