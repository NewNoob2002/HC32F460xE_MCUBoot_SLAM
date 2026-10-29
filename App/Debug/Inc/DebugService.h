#pragma once
#include "DataProc.h"
#include "DebugCodec.h"

namespace debug {
enum class Status : uint16_t {
    Ok = 0,
    BadFrame = 1,
    UnknownCommand = 2,
    Locked = 3,
    UnknownKey = 4,
    OutOfRange = 5,
    StorageError = 6,
    DeviceError = 7,
    Busy = 8,
    InternalError = 9
};
/** Main-loop service. USB ISR callbacks never call the broker, settings store or I2C bus. */
class Service final : public DataNode {
  public:
    constexpr Service(dp::SettingsNode& settings, hal::I2cBus& bus) : settings_(settings), bus_(bus) {}
    bool init(DataBroker& broker, const char* serial, uint32_t reset_cause);
    void receive(const uint8_t* data, std::size_t length, uint32_t now_ms);
    void reset();
    bool poll(uint32_t now_ms);
    /** Application log hook; copies bounded text into a fixed ring. Never call from ISR.
     *  Levels follow EasyLogger: 0 assert, 1 error, 2 warn, 3 info, 4 debug, 5 verbose. */
    void log(uint8_t level, uint32_t now_ms, const char* text);
    /** Queues one formatted EasyLogger line: drops ANSI color sequences and line endings and
     *  takes the level from its "X/" prefix (info when absent). Never call from ISR. */
    void log_line(const char* line, std::size_t length, uint32_t now_ms);

  private:
    struct Log {
        uint32_t time = 0U;
        uint8_t level = 0U;
        uint8_t length = 0U;
        std::array<uint8_t, 200> text{};
    };
    dp::SettingsNode& settings_;
    hal::I2cBus& bus_;
    Parser parser_{};
    const char* serial_ = nullptr;
    uint32_t reset_cause_ = 0U;
    uint32_t last_request_ = 0U;
    uint32_t last_stream_ = 0U;
    uint32_t event_sequence_ = 0U;
    uint16_t period_ = 0U;
    uint32_t stream_mask_ = 0U;
    bool unlocked_ = false;
    bool reboot_ = false;
    std::array<Log, 20> logs_{};
    uint8_t log_head_ = 0U;
    uint8_t log_count_ = 0U;
    uint32_t log_dropped_ = 0U;
    Log* push_log(uint8_t level, uint32_t now_ms);
    void expire(uint32_t now_ms);
    void dispatch(const Frame& request, uint32_t now_ms);
    std::size_t snapshot(uint8_t* output, uint32_t now_ms);
    bool send(uint8_t command, uint8_t flags, uint32_t sequence, const uint8_t* payload, std::size_t length);
};
} // namespace debug
