#include "DebugCodec.h"
#include <cstring>
#include "fw_update/protocol.h"

namespace {
bool validFlags(uint8_t flags) {
    return flags == 0U || flags == 1U || flags == 3U || flags == 4U;
}
bool validHeader(const uint8_t* p) {
    return std::memcmp(p, "DBG1", 4U) == 0 && p[4] == 1U && p[5] == 0U && validFlags(p[7])
           && debug::read16(p + 12U) <= debug::MaxPayload && debug::read16(p + 14U) == 0U;
}
void headerView(const uint8_t* p, debug::Frame& frame) {
    frame = {p[6], p[7], debug::read32(p + 8U), p + 16U, debug::read16(p + 12U)};
}
} // namespace
namespace debug {
std::size_t encode(const Frame& frame, uint8_t* output, std::size_t capacity) {
    const std::size_t size = 20U + frame.length;
    if (!output || frame.length > MaxPayload || size > capacity || !validFlags(frame.flags)
        || (!frame.payload && frame.length != 0U))
        return 0U;
    std::memcpy(output, "DBG1\x01\0", 6U);
    output[6] = frame.command;
    output[7] = frame.flags;
    write32(output + 8U, frame.sequence);
    write16(output + 12U, frame.length);
    write16(output + 14U, 0U);
    if (frame.length != 0U)
        std::memcpy(output + 16U, frame.payload, frame.length);
    write32(output + size - 4U, fw_protocol_crc32(output, size - 4U));
    return size;
}
bool decode(const uint8_t* input, std::size_t length, Frame& frame) {
    if (!input || length < 20U || !validHeader(input) || length != 20U + read16(input + 12U)
        || fw_protocol_crc32(input, length - 4U) != read32(input + length - 4U))
        return false;
    headerView(input, frame);
    return true;
}
void Parser::reset() {
    used_ = 0U;
    expected_ = 0U;
}
ParseResult Parser::feed(const uint8_t* data, std::size_t length, std::size_t& consumed, Frame& frame) {
    consumed = 0U;
    if (!data && length != 0U)
        return ParseResult::BadFrame;
    if (expected_ != 0U && used_ == expected_)
        reset();
    while (consumed < length) {
        bytes_[used_++] = data[consumed++];
        if (used_ <= 4U) {
            // Retain a possible overlapping magic prefix while skipping stream noise.
            while (used_ != 0U && std::memcmp(bytes_.data(), "DBG1", used_) != 0) {
                --used_;
                std::memmove(bytes_.data(), bytes_.data() + 1U, used_);
            }
        }
        if (used_ == 16U) {
            headerView(bytes_.data(), frame);
            if (!validHeader(bytes_.data())) {
                reset();
                return ParseResult::BadFrame;
            }
            expected_ = 20U + frame.length;
        }
        if (expected_ != 0U && used_ == expected_) {
            if (!decode(bytes_.data(), used_, frame)) {
                headerView(bytes_.data(), frame);
                reset();
                return ParseResult::BadFrame;
            }
            return ParseResult::Ready;
        }
    }
    return ParseResult::More;
}
} // namespace debug
