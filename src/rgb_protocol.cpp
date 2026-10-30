#include "rgb_protocol.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rgb_protocol {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint8_t ToByte(double value) {
    if (!std::isfinite(value)) {
        return 0;
    }
    const long rounded = std::lround(value);
    return static_cast<std::uint8_t>(std::clamp<long>(rounded, 0, 255));
}

}  // namespace

std::uint16_t Crc16CcittFalse(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0xFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) != 0
                      ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021u)
                      : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

Frame MakeFrame(Rgb color) {
    Frame frame{kHeader, color.red, color.green, color.blue, 0, 0};
    const std::uint16_t crc = Crc16CcittFalse(frame.data(), 4);
    frame[4] = static_cast<std::uint8_t>(crc & 0xFFu);
    frame[5] = static_cast<std::uint8_t>((crc >> 8) & 0xFFu);
    return frame;
}

Rgb CalculateBreathColor(Rgb base, double elapsed_seconds, double frequency_hz,
                         double gamma) {
    if (frequency_hz <= 0.0) {
        throw std::invalid_argument("frequency_hz must be positive");
    }
    if (gamma <= 0.0) {
        throw std::invalid_argument("gamma must be positive");
    }

    double brightness =
        0.5 * (1.0 - std::cos(2.0 * kPi * frequency_hz * elapsed_seconds));
    brightness = std::clamp(brightness, 0.0, 1.0);
    if (gamma != 1.0) {
        brightness = std::pow(brightness, gamma);
    }

    return Rgb{
        ToByte(base.red * brightness),
        ToByte(base.green * brightness),
        ToByte(base.blue * brightness),
    };
}

Decoder::Result Decoder::Push(std::uint8_t byte, Rgb& out) {
    if (size_ == 0 && byte != kHeader) {
        return Result::Rejected;
    }

    buffer_[size_++] = byte;
    if (size_ < kFrameSize) {
        return Result::NeedMore;
    }

    const std::uint16_t received =
        static_cast<std::uint16_t>(buffer_[4]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(buffer_[5]) << 8);
    const std::uint16_t calculated = Crc16CcittFalse(buffer_.data(), 4);

    if (received == calculated) {
        out = Rgb{buffer_[1], buffer_[2], buffer_[3]};
        size_ = 0;
        return Result::Accepted;
    }

    Resync();
    return Result::Rejected;
}

void Decoder::Reset() {
    size_ = 0;
}

void Decoder::Resync() {
    do {
        for (std::size_t i = 1; i < size_; ++i) {
            buffer_[i - 1] = buffer_[i];
        }
        if (size_ > 0) {
            --size_;
        }
    } while (size_ > 0 && buffer_[0] != kHeader);
}

}  // namespace rgb_protocol