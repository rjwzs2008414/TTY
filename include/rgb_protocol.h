#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rgb_protocol {

constexpr std::uint8_t kHeader = 0xA5;
constexpr std::size_t kFrameSize = 6;

struct Rgb {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

using Frame = std::array<std::uint8_t, kFrameSize>;

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
// Check value: "123456789" -> 0x29B1.
std::uint16_t Crc16CcittFalse(const std::uint8_t* data, std::size_t length);

// Frame layout: A5 R G B CRC_L CRC_H (CRC over the first four bytes).
Frame MakeFrame(Rgb color);

// brightness = (1 - cos(2*pi*f*t)) / 2, optionally gamma-corrected.
Rgb CalculateBreathColor(Rgb base, double elapsed_seconds, double frequency_hz,
                         double gamma = 1.0);

// Byte-stream decoder with header resynchronisation.  Useful for tests and as
// a reference for the STM32 receiver.
class Decoder {
public:
    enum class Result {
        NeedMore,
        Accepted,
        Rejected,
    };

    Result Push(std::uint8_t byte, Rgb& out);
    void Reset();

private:
    void Resync();

    std::array<std::uint8_t, kFrameSize> buffer_{};
    std::size_t size_ = 0;
};

}  // namespace rgb_protocol