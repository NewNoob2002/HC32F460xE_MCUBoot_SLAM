#include <array>
#include <cassert>
#include <cstring>
#include "DebugService.h"
#include "FakeI2cBus.h"
#include "fw_update/protocol.h"
#include "usb_debug_link.h"

namespace {
std::array<uint8_t, debug::MaxFrame> response{};
std::size_t response_size = 0U;
std::array<uint8_t, debug::MaxFrame> event{};
std::size_t event_size = 0U;
void* service_context;
usb_debug_reset_fn reset_callback;
bool link_reset = false;
uint32_t events = 0U;
uint8_t event_slots = 3U;
class Store final : public hal::SettingsStore {
  public:
    std::array<uint8_t, dp::SettingsNode::BlobSize> blob{};
    bool exists = false;
    bool fail = false;
    unsigned writes = 0U;
    bool load(uint8_t* data, std::size_t capacity, std::size_t& length) override {
        assert(capacity >= blob.size());
        length = exists ? blob.size() : 0U;
        if (exists)
            std::memcpy(data, blob.data(), blob.size());
        return exists;
    }
    bool save(const uint8_t* data, std::size_t length) override {
        ++writes;
        assert(length == blob.size());
        if (fail)
            return false;
        std::memcpy(blob.data(), data, length);
        exists = true;
        return true;
    }
};
template <class T> class Provider final : public dp::ValueNode<T> {
  public:
    T& value() {
        return this->state_;
    }
};
class Signals final : public hal::ChargerSignals {
  public:
    bool init() override {
        return true;
    }
    bool read(hal::ChargerSample& sample) override {
        sample = {true, true};
        return true;
    }
};
void codecTests() {
    const uint8_t payload[] = {1U, 2U, 3U};
    uint8_t bytes[debug::MaxFrame]{};
    const debug::Frame original{0x30U, 0U, 0x12345678U, payload, sizeof(payload)};
    const auto size = debug::encode(original, bytes, sizeof(bytes));
    assert(size == 23U && std::memcmp(bytes, "DBG1", 4U) == 0);
    debug::Frame frame{};
    assert(debug::decode(bytes, size, frame));
    assert(frame.sequence == original.sequence && frame.length == 3U && frame.payload[2] == 3U);
    assert(!debug::decode(bytes, size - 1U, frame));
    for (std::size_t split = 1U; split < size; ++split) {
        debug::Parser parser;
        std::size_t consumed = 0U;
        assert(parser.feed(bytes, split, consumed, frame) == debug::ParseResult::More && consumed == split);
        assert(parser.feed(bytes + split, size - split, consumed, frame) == debug::ParseResult::Ready);
        assert(consumed == size - split && frame.sequence == original.sequence);
    }
    uint8_t joined[2U * debug::MaxFrame]{};
    std::memcpy(joined, bytes, size);
    std::memcpy(joined + size, bytes, size);
    debug::Parser parser;
    std::size_t consumed = 0U;
    assert(parser.feed(joined, size * 2U, consumed, frame) == debug::ParseResult::Ready && consumed == size);
    assert(parser.feed(joined + consumed, size, consumed, frame) == debug::ParseResult::Ready && consumed == size);
    bytes[size - 1U] ^= 1U;
    parser.reset();
    assert(!debug::decode(bytes, size, frame));
    assert(parser.feed(bytes, size, consumed, frame) == debug::ParseResult::BadFrame);
    bytes[size - 1U] ^= 1U;
    for (unsigned field : {4U, 5U, 7U, 12U, 14U}) {
        const uint8_t saved = bytes[field];
        bytes[field] = field == 12U ? 0xFFU : 0x7FU;
        if (field == 12U)
            bytes[13] = 1U;
        parser.reset();
        assert(!debug::decode(bytes, size, frame));
        assert(parser.feed(bytes, size, consumed, frame) == debug::ParseResult::BadFrame && consumed == 16U);
        bytes[field] = saved;
        bytes[13] = 0U;
    }
    for (uint8_t flag : {uint8_t{0}, uint8_t{1}, uint8_t{3}, uint8_t{4}}) {
        auto f = original;
        f.flags = flag;
        assert(debug::encode(f, bytes, sizeof(bytes)) == size && debug::decode(bytes, size, frame));
    }
    std::array<uint8_t, debug::MaxPayload> maximum{};
    auto large = original;
    large.payload = maximum.data();
    large.length = maximum.size();
    assert(debug::encode(large, bytes, sizeof(bytes)) == sizeof(bytes));
    parser.reset();
    assert(parser.feed(bytes, sizeof(bytes), consumed, frame) == debug::ParseResult::Ready);
    large.length = 257U;
    assert(debug::encode(large, bytes, sizeof(bytes)) == 0U);
    const uint8_t noise[] = {'D', 'D', 'B', 'D'};
    parser.reset();
    assert(parser.feed(noise, sizeof(noise), consumed, frame) == debug::ParseResult::More);
    const auto small = debug::encode(original, bytes, sizeof(bytes));
    assert(parser.feed(bytes, small, consumed, frame) == debug::ParseResult::Ready);
}
void settingsTests() {
    Store store;
    {
        DataBroker broker;
        dp::SettingsNode settings(store);
        assert(settings.init(broker));
        int32_t value = 0;
        assert(settings.get(2U, value) == dp::SettingResult::Ok && value == 500);
        assert(settings.set(2U, 500) == dp::SettingResult::Ok && store.writes == 0U);
        assert(settings.set(2U, 99) == dp::SettingResult::OutOfRange && store.writes == 0U);
        assert(settings.set(100U, 1) == dp::SettingResult::UnknownKey);
        assert(settings.set(2U, 800) == dp::SettingResult::Ok && store.writes == 1U);
        store.fail = true;
        assert(settings.set(2U, 900) == dp::SettingResult::StorageError);
        assert(settings.get(2U, value) == dp::SettingResult::Ok && value == 800);
        store.fail = false;
    }
    {
        DataBroker broker;
        dp::SettingsNode settings(store);
        assert(settings.init(broker));
        int32_t value = 0;
        assert(settings.get(2U, value) == dp::SettingResult::Ok && value == 800);
    }
    store.blob[0] ^= 1U;
    {
        DataBroker broker;
        dp::SettingsNode settings(store);
        assert(settings.init(broker));
        int32_t value = 0;
        assert(settings.get(2U, value) == dp::SettingResult::Ok && value == 500);
    }
    store.blob[0] ^= 1U;
    debug::write32(store.blob.data() + 8U, 2U); // Valid CRC, invalid bool: only that value defaults.
    debug::write32(store.blob.data() + 28U, fw_protocol_crc32(store.blob.data(), 28U));
    {
        DataBroker broker;
        dp::SettingsNode settings(store);
        Signals signals;
        dp::ChargerNode charger(signals);
        DataNode reader;
        assert(settings.init(broker) && charger.init(broker, 0U));
        assert(reader.init(broker, "Reader") == DataNode::RES_OK);
        assert(reader.subscribe(dp::names::Charger) == DataNode::RES_OK);
        dp::ChargerState state{};
        assert(dp::Charger::pull(reader, state) == DataNode::RES_OK);
        assert(state.settings.values[0] == 0 && state.settings.values[1] == 800);
        assert(settings.set(2U, 1100) == dp::SettingResult::Ok);
        assert(dp::Charger::pull(reader, state) == DataNode::RES_OK && state.settings.values[1] == 1100);
        assert(settings.reset() == dp::SettingResult::Ok);
        const auto writes = store.writes;
        assert(settings.reset() == dp::SettingResult::Ok && store.writes == writes);
    }
}
struct Fixture {
    DataBroker broker;
    Store store;
    dp::SettingsNode settings{store};
    FakeI2cBus bus;
    Provider<dp::PowerState> power;
    Provider<dp::ChargerState> charger;
    Provider<dp::UpdateState> update;
    Provider<dp::SystemState> system;
    debug::Service service{settings, bus};
    uint32_t sequence = 0U;
    Fixture() {
        response_size = 0U;
        event_size = 0U;
        events = 0U;
        assert(settings.init(broker));
        assert(power.init(broker, dp::names::Power, DataNode::EVENT_PULL) == DataNode::RES_OK);
        assert(charger.init(broker, dp::names::Charger, DataNode::EVENT_PULL) == DataNode::RES_OK);
        assert(update.init(broker, dp::names::Update, DataNode::EVENT_PULL) == DataNode::RES_OK);
        assert(system.init(broker, dp::names::System, DataNode::EVENT_PULL) == DataNode::RES_OK);
        system.value().loop_count = 0x12345678U;
        update.value().usb.errors = 321U;
        assert(service.init(broker, "SN1234", 0xAA55U));
    }
    debug::Frame request(uint8_t command, const uint8_t* data = nullptr, uint16_t length = 0U,
                         debug::Status expected = debug::Status::Ok, uint32_t now = 10U) {
        assert(response_size == 0U);
        uint8_t bytes[debug::MaxFrame]{};
        const auto size = debug::encode({command, 0U, ++sequence, data, length}, bytes, sizeof(bytes));
        service.receive(bytes, 7U, now);
        assert(response_size == 0U);
        service.receive(bytes + 7U, size - 7U, now);
        assert(response_size != 0U);
        debug::Frame result{};
        assert(debug::decode(response.data(), response_size, result));
        assert(result.command == command && result.sequence == sequence && result.length >= 2U);
        assert(debug::read16(result.payload) == static_cast<uint16_t>(expected));
        assert(result.flags == (expected == debug::Status::Ok ? 1U : 3U));
        response_size = 0U;
        return result;
    }
    void unlock(uint32_t now = 10U) {
        const uint8_t serial[] = {6U, 'S', 'N', '1', '2', '3', '4'};
        const auto r = request(0x20U, serial, sizeof(serial), debug::Status::Ok, now);
        assert(debug::read16(r.payload + 2U) == 60U);
    }
};
void dispatchTests() {
    Fixture f;
    const auto hello = f.request(1U);
    assert(hello.length == 12U && hello.payload[2] == 1U && debug::read32(hello.payload + 6U) == 63U);
    f.request(0x7FU, nullptr, 0U, debug::Status::UnknownCommand);
    uint8_t set[] = {2U, 0U, 0U, 0U, 0U, 0U};
    debug::write32(set + 2U, 900U);
    f.request(0x12U, set, sizeof(set), debug::Status::Locked);
    f.request(0x13U, nullptr, 0U, debug::Status::Locked);
    const uint8_t wrong[] = {3U, 'b', 'a', 'd'};
    f.request(0x20U, wrong, sizeof(wrong), debug::Status::Locked);
    f.unlock();
    const auto stored = f.request(0x12U, set, sizeof(set));
    assert(debug::read32(stored.payload + 2U) == 900U && f.store.writes == 1U);
    f.request(0x12U, set, sizeof(set));
    assert(f.store.writes == 1U);
    debug::write32(set + 2U, 2001U);
    f.request(0x12U, set, sizeof(set), debug::Status::OutOfRange);
    set[0] = 0xFFU;
    f.request(0x11U, set, 2U, debug::Status::UnknownKey);
    f.request(0x12U, set, sizeof(set), debug::Status::UnknownKey);
    set[0] = 2U;
    debug::write32(set + 2U, 1000U);
    f.store.fail = true;
    f.request(0x12U, set, sizeof(set), debug::Status::StorageError);
    f.request(0x13U, nullptr, 0U, debug::Status::StorageError);
    f.store.fail = false;
    f.request(0x13U);
    // Idle timeout checked on incoming requests, including without a poll in between.
    f.unlock(100U);
    f.request(0x12U, set, sizeof(set), debug::Status::Locked, 60100U);
    f.unlock();
    f.request(1U);
    f.request(0x12U, set, sizeof(set), debug::Status::Locked);
    uint8_t start = 0U;
    auto page = f.request(0x10U, &start, 1U);
    assert(page.payload[2] == 5U && page.payload[3] == 5U);
    std::size_t at = 4U;
    for (unsigned i = 0U; i < 5U; ++i) {
        assert(debug::read16(page.payload + at) == i + 1U && page.payload[at + 3U] == 3U);
        at += 22U + page.payload[at + 21U];
    }
    assert(at == page.length);
    start = 3U;
    page = f.request(0x10U, &start, 1U);
    assert(page.payload[3] == 2U && debug::read16(page.payload + 4U) == 4U);
    start = 5U;
    page = f.request(0x10U, &start, 1U);
    assert(page.payload[3] == 0U);
    start = 6U;
    f.request(0x10U, &start, 1U, debug::Status::OutOfRange);
    f.bus.raw_read = true;
    uint8_t reg[] = {0U, 0x23U, 32U};
    const uint8_t addresses[] = {0x5CU, 0x0BU, 0x0CU, 0x08U};
    for (uint8_t i = 0U; i < 4U; ++i) {
        reg[0] = i;
        f.bus.expected_address = addresses[i];
        const auto r = f.request(0x30U, reg, sizeof(reg));
        assert(r.length == 34U && r.payload[33] == 0x5AU);
    }
    const auto reads = f.bus.reads;
    reg[0] = 4U;
    f.request(0x30U, reg, sizeof(reg), debug::Status::OutOfRange);
    reg[0] = 3U;
    reg[2] = 0U;
    f.request(0x30U, reg, sizeof(reg), debug::Status::OutOfRange);
    reg[2] = 33U;
    f.request(0x30U, reg, sizeof(reg), debug::Status::OutOfRange);
    assert(reads == f.bus.reads && f.bus.writes == 0U);
    reg[2] = 1U;
    f.bus.read_result = -1;
    f.request(0x30U, reg, sizeof(reg), debug::Status::DeviceError);
    uint8_t stream[6]{};
    debug::write16(stream, 99U);
    f.request(3U, stream, sizeof(stream), debug::Status::OutOfRange);
    debug::write16(stream, 5001U);
    f.request(3U, stream, sizeof(stream), debug::Status::OutOfRange);
    debug::write16(stream, 100U);
    debug::write32(stream + 2U, 4U);
    f.request(3U, stream, sizeof(stream), debug::Status::OutOfRange);
    debug::write32(stream + 2U, 1U);
    f.request(3U, stream, sizeof(stream));
    assert(!f.service.poll(109U) && events == 0U);
    assert(!f.service.poll(110U) && events == 1U);
    debug::Frame telemetry{};
    assert(debug::decode(event.data(), event_size, telemetry) && telemetry.flags == 4U);
    assert(telemetry.command == 0x80U && telemetry.payload[0] == 1U);
    assert(debug::read32(telemetry.payload + 2U) == 110U);
    assert(telemetry.payload[6] == 2U && debug::read32(telemetry.payload + 8U) == 0x12345678U);
    // Log hook copies text, caps long lines and reports bounded events.
    char text[] = "hello";
    f.service.log(2U, 120U, text);
    text[0] = 'x';
    debug::write32(stream + 2U, 2U);
    f.request(3U, stream, sizeof(stream));
    assert(!f.service.poll(110U));
    assert(debug::decode(event.data(), event_size, telemetry) && telemetry.command == 0x81U);
    assert(telemetry.payload[0] == 2U && telemetry.payload[5] == 5U && telemetry.payload[6] == 'h');
    // EasyLogger lines lose color codes and CRLF, keep their level, and drain without waiting a period.
    static const char colored[] = "\033[32;22mD/core [130] hello\033[0m\r\n";
    f.service.log_line(colored, sizeof(colored) - 1U, 130U);
    static const char plain[] = "no level\r\n";
    f.service.log_line(plain, sizeof(plain) - 1U, 131U);
    static const char blank[] = "\r\n";
    f.service.log_line(blank, sizeof(blank) - 1U, 132U);
    const auto before = events;
    assert(!f.service.poll(111U) && events == before + 2U);
    assert(debug::decode(event.data(), event_size, telemetry) && telemetry.command == 0x81U);
    assert(telemetry.payload[0] == 3U && telemetry.payload[5] == 8U);
    assert(std::memcmp(telemetry.payload + 6U, "no level", 8U) == 0);
    // No free slot: logs stay queued; telemetry keeps one reserved slot when both streams are on.
    f.service.log_line(colored, sizeof(colored) - 1U, 133U);
    event_slots = 0U;
    assert(!f.service.poll(112U) && events == before + 2U);
    debug::write32(stream + 2U, 3U);
    f.request(3U, stream, sizeof(stream));
    // With one free slot the due telemetry is sent and the log waits for the reserved slot to clear.
    event_slots = 1U;
    assert(!f.service.poll(1000U) && events == before + 3U);
    assert(debug::decode(event.data(), event_size, telemetry) && telemetry.command == 0x80U);
    event_slots = 3U;
    assert(!f.service.poll(1001U) && events == before + 4U);
    assert(debug::decode(event.data(), event_size, telemetry) && telemetry.command == 0x81U);
    assert(telemetry.payload[0] == 4U && telemetry.payload[5] == 18U);
    assert(std::memcmp(telemetry.payload + 6U, "D/core [130] hello", 18U) == 0);
    f.unlock();
    link_reset = true;
    assert(!f.service.poll(120U));
    f.request(0x12U, set, sizeof(set), debug::Status::Locked);
    const auto old_events = events;
    assert(!f.service.poll(1000U) && events == old_events);
    uint8_t mode = 0U;
    f.request(0x40U, &mode, 1U, debug::Status::Locked);
    f.unlock();
    mode = 1U;
    f.request(0x40U, &mode, 1U, debug::Status::OutOfRange);
    mode = 0U;
    f.request(0x40U, &mode, 1U);
    // Simulate response still in USB; reboot waits and reset cancels it.
    response_size = 1U;
    assert(!f.service.poll(11U));
    response_size = 0U;
    assert(f.service.poll(12U));
    link_reset = true;
    assert(!f.service.poll(13U));
    f.request(0x30U, reg, 2U, debug::Status::BadFrame);
    // A bad CRC generates BAD_FRAME and the next valid request is accepted.
    uint8_t bytes[debug::MaxFrame]{};
    const auto size = debug::encode({1U, 0U, 90U, nullptr, 0U}, bytes, sizeof(bytes));
    bytes[size - 1U] ^= 1U;
    f.service.receive(bytes, size, 20U);
    debug::Frame error{};
    assert(debug::decode(response.data(), response_size, error) && debug::read16(error.payload) == 1U);
    response_size = 0U;
    f.request(1U);
}
} // namespace
extern "C" {
void usb_debug_link_bind(void* context, usb_debug_receive_fn, usb_debug_reset_fn reset) {
    service_context = context;
    reset_callback = reset;
}
void usb_debug_link_poll(uint32_t) {
    if (link_reset) {
        link_reset = false;
        response_size = 0U;
        reset_callback(service_context);
    }
}
bool usb_debug_link_response_available(void) {
    return response_size == 0U;
}
bool usb_debug_link_response_pending(void) {
    return response_size != 0U;
}
uint32_t usb_debug_link_dropped(void) {
    return 0U;
}
uint8_t usb_debug_link_event_slots_free(void) {
    return event_slots;
}
bool usb_debug_link_send(const uint8_t* frame, size_t length, bool is_response) {
    assert(length <= debug::MaxFrame);
    if (is_response) {
        assert(response_size == 0U);
        response_size = length;
        std::memcpy(response.data(), frame, length);
    } else {
        ++events;
        event_size = length;
        std::memcpy(event.data(), frame, length);
    }
    return true;
}
}
int main() {
    codecTests();
    settingsTests();
    dispatchTests();
    return 0;
}
