#ifndef APP_HAL_STATUS_H
#define APP_HAL_STATUS_H

namespace hal {
/** HAL result codes. Values are part of the debug-link contract and keep their historical numbers. */
enum Status : int {
    Ok = 0,
    InitError = -1,
    InvalidArgument = -2,
    NotReady = -3,
    InitDeinitError = -4,
    InitDefaultsError = -5,
    BusBusy = -6,
    StartTimeout = -7,
    AddressNack = -8,
    AddressTimeout = -9,
    TransmitNack = -10,
    TransmitTimeout = -11,
    RestartTimeout = -12,
    ReceiveAddressNack = -13,
    ReceiveAddressTimeout = -14,
    ReceiveStopTimeout = -15,
    BatteryInvalidArgument = -16,
    ProtocolError = -17,
    StopTimeout = -18,
    ArbitrationLost = -19,
    VerifyError = -24,
    UnknownError = -25,
};
} // namespace hal

#endif
