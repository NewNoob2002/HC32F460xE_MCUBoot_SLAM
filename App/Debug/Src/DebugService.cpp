#include "DebugService.h"
#include <cstring>
#include "usb_debug_link.h"

namespace {
void receiveUsb(void* context, const uint8_t* data, std::size_t length, uint32_t now_ms) {
    static_cast<debug::Service*>(context)->receive(data, length, now_ms);
}
void resetUsb(void* context) {
    static_cast<debug::Service*>(context)->reset();
}
debug::Status statusFor(dp::SettingResult result) {
    switch (result) {
        case dp::SettingResult::Ok:
            return debug::Status::Ok;
        case dp::SettingResult::UnknownKey:
            return debug::Status::UnknownKey;
        case dp::SettingResult::OutOfRange:
            return debug::Status::OutOfRange;
        case dp::SettingResult::StorageError:
            return debug::Status::StorageError;
    }
    return debug::Status::InternalError;
}
} // namespace
namespace debug {
bool Service::init(DataBroker& broker, const char* serial, uint32_t reset_cause) {
    if (!serial || serial[0] == '\0' || DataNode::init(broker, "Debug", EVENT_NONE) != RES_OK)
        return false;
    serial_ = serial;
    reset_cause_ = reset_cause;
    for (const char* source : {dp::names::Power, dp::names::Charger, dp::names::Update, dp::names::System}) {
        if (subscribe(source) != RES_OK)
            return false;
    }
    reset();
    usb_debug_link_bind(this, receiveUsb, resetUsb);
    return true;
}
void Service::reset() {
    parser_.reset();
    unlocked_ = false;
    period_ = 0U;
    stream_mask_ = 0U;
    reboot_ = false;
}
void Service::expire(uint32_t now_ms) {
    if (unlocked_ && now_ms - last_request_ >= 60000U)
        unlocked_ = false;
}
bool Service::send(uint8_t command, uint8_t flags, uint32_t sequence, const uint8_t* payload, std::size_t length) {
    uint8_t bytes[MaxFrame]{};
    if (length > MaxPayload)
        return false;
    const auto size = encode({command, flags, sequence, payload, static_cast<uint16_t>(length)}, bytes, sizeof(bytes));
    return size != 0U && usb_debug_link_send(bytes, size, flags != 4U);
}
void Service::receive(const uint8_t* data, std::size_t length, uint32_t now_ms) {
    expire(now_ms);
    std::size_t offset = 0U;
    while (offset < length) {
        Frame request{};
        std::size_t consumed = 0U;
        const auto result = parser_.feed(data + offset, length - offset, consumed, request);
        offset += consumed;
        if (result == ParseResult::More || consumed == 0U)
            break;
        // A nonconforming host may pipeline requests. Never execute a mutation without response capacity.
        if (!usb_debug_link_response_available() || reboot_)
            continue;
        if (result == ParseResult::BadFrame || request.flags != 0U) {
            uint8_t status[2]{};
            write16(status, static_cast<uint16_t>(Status::BadFrame));
            (void)send(request.command, 3U, request.sequence, status, sizeof(status));
        } else {
            last_request_ = now_ms;
            dispatch(request, now_ms);
        }
    }
}
std::size_t Service::snapshot(uint8_t* output, uint32_t now_ms) {
    dp::PowerState power{};
    dp::ChargerState charger{};
    dp::UpdateState update{};
    dp::SystemState system{};
    const bool have_power = dp::Power::pull(*this, power) == RES_OK;
    (void)dp::Charger::pull(*this, charger);
    (void)dp::Update::pull(*this, update);
    (void)dp::System::pull(*this, system);
    std::size_t n = 0U;
    const auto u32 = [&output, &n](uint8_t tag, uint32_t value) {
        output[n++] = tag;
        output[n++] = 4U;
        write32(output + n, value);
        n += 4U;
    };
    const auto u8 = [&output, &n](uint8_t tag, uint8_t value) {
        output[n++] = tag;
        output[n++] = 1U;
        output[n++] = value;
    };
    u32(0x01U, now_ms);
    u32(0x02U, system.loop_count);
    uint8_t mask = 0U;
    if (have_power && power.i2c_init_result == BSP_I2C2_OK) {
        const int probes[] = {power.mp2762a_probe_result, power.bq_default_probe_result,
                              power.bq_alternate_probe_result, power.husb238_probe_result};
        for (unsigned i = 0U; i < 4U; ++i)
            if (probes[i] == BSP_I2C2_OK)
                mask = static_cast<uint8_t>(mask | (1U << i));
    }
    u8(0x10U, mask);
    if ((mask & 4U) != 0U && power.bq_alternate_identity_result == hal::BQ40Z50_OK) {
        output[n++] = 0x11U;
        output[n++] = 17U;
        const auto& id = power.bq_alternate_identity;
        const auto word = [&output, &n](uint16_t value) {
            write16(output + n, value);
            n += 2U;
        };
        word(id.device_type);
        word(id.firmware.device_number);
        word(id.firmware.version);
        word(id.firmware.build);
        output[n++] = id.firmware.firmware_type;
        word(id.firmware.impedance_track_version);
        output[n++] = id.firmware.reserved;
        output[n++] = id.firmware.execution_context;
        word(id.hardware_version);
        word(id.chemistry_id);
    }
    u8(0x20U, charger.power_good ? 1U : 0U);
    u8(0x21U, charger.interrupt_asserted ? 1U : 0U);
    u32(0x22U, charger.change_count);
    u8(0x30U, static_cast<uint8_t>(update.usb.manager_state));
    u32(0x31U, update.usb.errors);
    u32(0x40U, usb_debug_link_dropped() + log_dropped_);
    u32(0x41U, reset_cause_);
    return n;
}
void Service::dispatch(const Frame& r, uint32_t now_ms) {
    uint8_t payload[MaxPayload]{};
    std::size_t length = 2U;
    Status status = Status::Ok;
    const auto require = [&r, &status](uint16_t size) {
        if (r.length != size) {
            status = Status::BadFrame;
            return false;
        }
        return true;
    };
    switch (r.command) {
        case 0x01U:
            if (require(0U)) {
                unlocked_ = false;
                period_ = 0U;
                stream_mask_ = 0U;
                payload[length++] = 1U;
                payload[length++] = 0U;
                write16(payload + length, MaxPayload);
                length += 2U;
                write32(payload + length, 0x3FU);
                length += 4U;
                write16(payload + length, 1U);
                length += 2U;
            }
            break;
        case 0x02U:
        case 0x41U:
            if (require(0U))
                length += snapshot(payload + length, now_ms);
            break;
        case 0x03U:
            if (require(6U)) {
                const auto period = read16(r.payload);
                const auto mask = read32(r.payload + 2U);
                if ((period != 0U && (period < 100U || period > 5000U)) || (mask & ~3U) != 0U)
                    status = Status::OutOfRange;
                else {
                    period_ = period;
                    stream_mask_ = mask;
                    last_stream_ = now_ms;
                }
            }
            break;
        case 0x10U:
            if (require(1U)) {
                const std::size_t start = r.payload[0];
                if (start > dp::SettingTable.size()) {
                    status = Status::OutOfRange;
                    break;
                }
                payload[length++] = static_cast<uint8_t>(dp::SettingTable.size());
                const auto count_at = length++;
                for (std::size_t i = start; i < dp::SettingTable.size(); ++i) {
                    const auto& spec = dp::SettingTable[i];
                    const auto name_len = std::strlen(spec.name);
                    if (length + 22U + name_len > sizeof(payload))
                        break;
                    int32_t current = 0;
                    (void)settings_.get(spec.key, current);
                    write16(payload + length, spec.key);
                    length += 2U;
                    payload[length++] = spec.type;
                    payload[length++] = 3U; // Persisted, unlock; hardware unapplied.
                    for (int32_t value : {spec.min, spec.max, spec.initial, current}) {
                        write32(payload + length, static_cast<uint32_t>(value));
                        length += 4U;
                    }
                    payload[length++] = spec.unit;
                    payload[length++] = static_cast<uint8_t>(name_len);
                    std::memcpy(payload + length, spec.name, name_len);
                    length += name_len;
                    ++payload[count_at];
                }
            }
            break;
        case 0x11U:
        case 0x12U:
            if (require(r.command == 0x11U ? 2U : 6U)) {
                const auto key = read16(r.payload);
                int32_t value = 0;
                if (r.command == 0x12U) {
                    if (!unlocked_) {
                        status = Status::Locked;
                        break;
                    }
                    const uint32_t raw = read32(r.payload + 2U);
                    std::memcpy(&value, &raw, sizeof(value));
                    status = statusFor(settings_.set(key, value));
                }
                if (status == Status::Ok)
                    status = statusFor(settings_.get(key, value));
                if (status == Status::Ok) {
                    write32(payload + length, static_cast<uint32_t>(value));
                    length += 4U;
                }
            }
            break;
        case 0x13U:
            if (require(0U))
                status = unlocked_ ? statusFor(settings_.reset()) : Status::Locked;
            break;
        case 0x20U:
            if (r.length < 2U || r.payload[0] == 0U || r.length != 1U + r.payload[0])
                status = Status::BadFrame;
            else if (!serial_ || std::strlen(serial_) != r.payload[0]
                     || std::memcmp(serial_, r.payload + 1U, r.payload[0]) != 0) {
                unlocked_ = false;
                status = Status::Locked;
            } else {
                unlocked_ = true;
                write16(payload + length, 60U);
                length += 2U;
            }
            break;
        case 0x30U:
            if (require(3U)) {
                static constexpr uint8_t addresses[] = {0x5CU, 0x0BU, 0x0CU, 0x08U};
                const auto device = r.payload[0];
                const auto count = r.payload[2];
                if (device >= sizeof(addresses) || count == 0U || count > 32U)
                    status = Status::OutOfRange;
                else if (bus_.readReg(addresses[device], r.payload[1], payload + length, count) != BSP_I2C2_OK)
                    status = Status::DeviceError;
                else
                    length += count;
            }
            break;
        case 0x40U:
            if (require(1U)) {
                if (!unlocked_)
                    status = Status::Locked;
                else if (r.payload[0] != 0U)
                    status = Status::OutOfRange;
                else {
                    reboot_ = true;
                    period_ = 0U;
                    stream_mask_ = 0U;
                }
            }
            break;
        default:
            status = Status::UnknownCommand;
            break;
    }
    write16(payload, static_cast<uint16_t>(status));
    if (status != Status::Ok)
        length = 2U;
    if (!send(r.command, status == Status::Ok ? 1U : 3U, r.sequence, payload, length))
        reboot_ = false;
}
Service::Log* Service::push_log(uint8_t level, uint32_t now_ms) {
    if (log_count_ == logs_.size()) {
        ++log_dropped_;
        return nullptr;
    }
    auto& entry = logs_[(log_head_ + log_count_) % logs_.size()];
    entry.time = now_ms;
    entry.level = level;
    entry.length = 0U;
    ++log_count_;
    return &entry;
}
void Service::log(uint8_t level, uint32_t now_ms, const char* text) {
    if (!text)
        return;
    auto* entry = push_log(level, now_ms);
    if (!entry)
        return;
    while (entry->length < entry->text.size() && text[entry->length] != '\0') {
        entry->text[entry->length] = static_cast<uint8_t>(text[entry->length]);
        ++entry->length;
    }
}
void Service::log_line(const char* line, std::size_t length, uint32_t now_ms) {
    if (!line)
        return;
    static constexpr char levels[] = "AEWIDV";
    uint8_t level = 3U;
    bool level_known = false;
    Log* entry = nullptr;
    for (std::size_t i = 0U; i < length; ++i) {
        const char c = line[i];
        if (c == '\033') {
            // CSI color sequence: ESC '[' parameters, ended by a byte in '@'..'~'.
            for (++i; i < length && (line[i] < '@' || line[i] > '~' || line[i] == '['); ++i) {}
            continue;
        }
        if (c == '\r' || c == '\n')
            continue;
        if (!entry) {
            if (!level_known && i + 1U < length && line[i + 1U] == '/') {
                for (uint8_t index = 0U; index < sizeof(levels) - 1U; ++index) {
                    if (levels[index] == c)
                        level = index;
                }
            }
            level_known = true;
            entry = push_log(level, now_ms);
            if (!entry)
                return;
        }
        if (entry->length == entry->text.size())
            break;
        entry->text[entry->length++] = static_cast<uint8_t>(c);
    }
}
bool Service::poll(uint32_t now_ms) {
    usb_debug_link_poll(now_ms);
    expire(now_ms);
    if (reboot_)
        return !usb_debug_link_response_pending();
    if (period_ == 0U)
        return false;
    uint8_t payload[MaxPayload]{};
    if (now_ms - last_stream_ >= period_) {
        last_stream_ = now_ms;
        if ((stream_mask_ & 1U) != 0U) {
            const auto length = snapshot(payload, now_ms);
            (void)send(0x80U, 4U, event_sequence_++, payload, length);
        }
    }
    // Logs stream like a console: drain whenever IN event slots are free instead of one per period.
    // One slot stays reserved so the next telemetry event is never starved by a log burst.
    const uint8_t reserved = (stream_mask_ & 1U) != 0U ? 1U : 0U;
    while ((stream_mask_ & 2U) != 0U && log_count_ != 0U && usb_debug_link_event_slots_free() > reserved) {
        const auto& entry = logs_[log_head_];
        payload[0] = entry.level;
        write32(payload + 1U, entry.time);
        payload[5] = entry.length;
        std::memcpy(payload + 6U, entry.text.data(), entry.length);
        if (!send(0x81U, 4U, event_sequence_++, payload, 6U + entry.length))
            break;
        log_head_ = static_cast<uint8_t>((log_head_ + 1U) % logs_.size());
        --log_count_;
    }
    return false;
}
} // namespace debug
